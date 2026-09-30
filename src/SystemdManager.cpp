#include "SystemdManager.hpp"

#include <trantor/utils/Logger.h>

#include <cerrno>
#include <climits>
#include <ctime>
#include <poll.h>
#include <stdexcept>
#include <utility>
#include <vector>


// ============================================================
// Destructor
// ============================================================

SystemdManager::~SystemdManager()
{
    /*
     * Il DBusManager deve vivere almeno quanto il Drogon EventLoop.
     *
     * stopInLoop() viene registrato con runOnQuit(), quindi normalmente
     * quando arriviamo qui m_bus è già nullptr.
     *
     * NON facciamo sd_bus_unref() qui perché il destructor potrebbe
     * essere eseguito da un thread diverso dal proprietario del bus.
     */

    if (m_bus != nullptr)
    {
        LOG_ERROR
            << "SystemdManager destroyed while D-Bus is still active. "
               "The manager must outlive the Drogon EventLoop.";
    }
}


// ============================================================
// Result helpers
// ============================================================

SystemdManager::StringResult
SystemdManager::makeStringSuccess(std::string value)
{
    StringResult result;

    result.ok = true;
    result.value = std::move(value);

    return result;
}


SystemdManager::StringResult
SystemdManager::makeStringError(
    int errorCode,
    std::string message)
{
    StringResult result;

    result.ok = false;
    result.errorCode = errorCode;
    result.error = std::move(message);

    return result;
}


SystemdManager::UnitInfoResult
SystemdManager::makeUnitInfoSuccess(UnitInfo value)
{
    UnitInfoResult result;

    result.ok = true;
    result.value = std::move(value);

    return result;
}


SystemdManager::UnitInfoResult
SystemdManager::makeUnitInfoError(
    int errorCode,
    std::string message)
{
    UnitInfoResult result;

    result.ok = false;
    result.errorCode = errorCode;
    result.error = std::move(message);

    return result;
}


// ============================================================
// Startup
// ============================================================

void SystemdManager::start(
    trantor::EventLoop *loop)
{
    if (loop == nullptr)
    {
        throw std::invalid_argument(
            "SystemdManager::start(): loop is nullptr");
    }

    /*
     * registerBeginningAdvice() gira nel main EventLoop di Drogon.
     *
     * Voglio volutamente che tutta l'inizializzazione del bus
     * venga fatta dal thread che ne sarà proprietario.
     */
    loop->assertInLoopThread();

    if (m_bus != nullptr)
        throw std::logic_error("SystemdManager already started");

    if (m_loop.load(std::memory_order_acquire) != nullptr)
        throw std::logic_error("SystemdManager already has an EventLoop");

    int r = sd_bus_open_system(&m_bus);

    if (r < 0)
    {
        m_bus = nullptr;

        throw std::runtime_error(
            "Unable to connect to system D-Bus: " +
            std::string(strerror(-r)));
    }

    /*
     * Da questo momento il bus appartiene a questo EventLoop.
     */
    m_loop.store(loop, std::memory_order_release);

    /*
     * Quando Drogon termina, il cleanup verrà eseguito
     * nello stesso thread del bus.
     */
    loop->runOnQuit(
        [this]()
        {
            stopInLoop();
        });

    /*
     * sd_bus_open_system() ha avviato la connessione,
     * ma potrebbe esserci già del lavoro da processare.
     */
    processBus();

    LOG_INFO << "SystemdManager connected to system D-Bus";
}


// ============================================================
// Shutdown
// ============================================================

void SystemdManager::stopInLoop()
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop != nullptr)
        loop->assertInLoopThread();

    /*
     * Impedisce a nuovi callers di accodare richieste.
     */
    m_loop.store(nullptr, std::memory_order_release);

    // --------------------------------------------------------
    // Timer
    // --------------------------------------------------------

    if (loop != nullptr && m_timerId != trantor::InvalidTimerId)
    {
        loop->invalidateTimer(m_timerId);

        m_timerId = trantor::InvalidTimerId;
    }

    // --------------------------------------------------------
    // Channel
    // --------------------------------------------------------

    if (m_channel)
    {
        /*
         * Channel::remove() richiede che non siano abilitati eventi.
         */
        if (!m_channel->isNoneEvent())
        {
            m_channel->disableAll();
        }

        m_channel->remove();

        m_channel.reset();
    }

    // --------------------------------------------------------
    // Pending asynchronous calls
    // --------------------------------------------------------

    std::vector<InternalStringCallback>
        callbacks;

    callbacks.reserve(
        m_pendingCalls.size());

    for (auto &item : m_pendingCalls)
    {
        if (item.second->callback)
        {
            callbacks.emplace_back(
                std::move(item.second->callback));
        }
    }

    /*
     * Distruggendo PendingStringCall vengono unref-fatti
     * anche gli sd_bus_slot, cancellando le call pendenti.
     */
    m_pendingCalls.clear();

    // --------------------------------------------------------
    // Bus
    // --------------------------------------------------------

    m_bus =
        sd_bus_unref(m_bus);

    /*
     * Notifica eventuali operazioni ancora pendenti.
     *
     * A shutdown dell'app i loop HTTP potrebbero già essere
     * in chiusura, ma questo mantiene comunque semanticamente
     * corretto il manager.
     */
    for (auto &callback : callbacks)
    {
        callback(
            makeStringError(
                -ECANCELED,
                "SystemdManager shutting down"));
    }

    LOG_INFO << "SystemdManager disconnected from system D-Bus";
}


// ============================================================
// D-Bus processing
// ============================================================

void SystemdManager::processBus()
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr || m_bus == nullptr)
        return;

    loop->assertInLoopThread();

    /*
     * Limitiamo il numero di operazioni per giro.
     *
     * Normalmente saranno 1-2, ma così un flood di messaggi D-Bus
     * non monopolizza completamente il main EventLoop.
     */
    constexpr int MAX_OPERATIONS_PER_TURN = 64;

    for (int i = 0; i < MAX_OPERATIONS_PER_TURN; ++i)
    {
        const int r = sd_bus_process(m_bus, nullptr);

        if (r < 0)
        {
            const std::string message =
                "sd_bus_process() failed: " +
                std::string(strerror(-r));

            LOG_ERROR << message;

            /*
             * Non facciamo stopInLoop() direttamente dentro
             * un eventuale callback Channel.
             *
             * Lo rimandiamo al prossimo giro dell'EventLoop.
             */
            loop->queueInLoop(
                [this]()
                {
                    stopInLoop();
                });

            return;
        }

        if (r == 0)
        {
            /*
             * Non c'è altro lavoro immediato.
             *
             * Chiediamo nuovamente a sd-bus:
             *   - quale fd
             *   - quali eventi
             *   - quale timeout
             */
            updateBusWatch();
            return;
        }
    }

    /*
     * C'è ancora lavoro, ma cediamo temporaneamente il controllo
     * all'EventLoop.
     */
    loop->queueInLoop(
        [this]()
        {
            processBus();
        });
}


// ============================================================
// Channel / fd
// ============================================================

void SystemdManager::createOrUpdateChannel(int fd)
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    loop->assertInLoopThread();

    /*
     * Normalmente l'fd non cambia.
     *
     * Gestiamo comunque il caso per robustezza.
     */
    if (m_channel && m_channel->fd() == fd)
        return ;

    if (m_channel)
    {
        if (!m_channel->isNoneEvent())
            m_channel->disableAll();

        m_channel->remove();
        m_channel.reset();
    }

    m_channel = std::make_unique<trantor::Channel>(loop, fd);

    /*
     * setEventCallback() viene eseguita una sola volta
     * qualunque combinazione di READ/WRITE/error sia arrivata.
     *
     * Tutto converge su processBus().
     */
    m_channel->setEventCallback(
        [this]()
        {
            processBus();
        });
}


void SystemdManager::updateBusWatch()
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr || m_bus == nullptr)
        return ;

    loop->assertInLoopThread();

    // --------------------------------------------------------
    // fd
    // --------------------------------------------------------

    const int fd = sd_bus_get_fd(m_bus);

    if (fd < 0)
    {
        LOG_ERROR << "sd_bus_get_fd(): " << strerror(-fd);
        return ;
    }

    createOrUpdateChannel(fd);

    // --------------------------------------------------------
    // events
    // --------------------------------------------------------

    const int busEvents = sd_bus_get_events(m_bus);

    if (busEvents < 0)
    {
        LOG_ERROR << "sd_bus_get_events(): " << strerror(-busEvents);
        return;
    }

    int channelEvents = trantor::Channel::kNoneEvent;

    if (busEvents & (POLLIN | POLLPRI))
        channelEvents |= trantor::Channel::kReadEvent;
    if (busEvents & POLLOUT)
        channelEvents |= trantor::Channel::kWriteEvent;

    /*
     * Evitiamo una epoll_ctl() inutile se nulla è cambiato.
     */
    if (m_channel->events() != channelEvents)
        m_channel->updateEvents(channelEvents);

    updateBusTimeout();
}


// ============================================================
// sd_bus timeout -> Trantor timer
// ============================================================

uint64_t SystemdManager::monotonicUsec()
{
    timespec ts{};

    if (::clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return (0);

    return
        static_cast<uint64_t>(ts.tv_sec) * 1'000'000ULL +
        static_cast<uint64_t>(ts.tv_nsec) / 1'000ULL;
}


void SystemdManager::updateBusTimeout()
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr || m_bus == nullptr)
        return ;

    loop->assertInLoopThread();

    /*
     * Cancella il timeout precedentemente installato.
     */
    if (m_timerId != trantor::InvalidTimerId)
    {
        loop->invalidateTimer(m_timerId);
        m_timerId = trantor::InvalidTimerId;
    }

    uint64_t timeoutUsec = UINT64_MAX;

    const int r = sd_bus_get_timeout(m_bus, &timeoutUsec);

    if (r < 0)
    {
        LOG_ERROR << "sd_bus_get_timeout(): " << strerror(-r);
        return ;
    }

    /*
     * UINT64_MAX significa:
     *
     * "nessun timeout interno attualmente necessario".
     */
    if (timeoutUsec == UINT64_MAX)
        return ;

    const uint64_t nowUsec = monotonicUsec();

    uint64_t delayUsec = 0;

    if (timeoutUsec > nowUsec)
        delayUsec = timeoutUsec - nowUsec;

    const double delaySeconds =
        static_cast<double>(delayUsec) / 1'000'000.0;

    m_timerId =
        loop->runAfter(
            delaySeconds,
            [this]()
            {
                m_timerId = trantor::InvalidTimerId;
                processBus();
            });
}


// ============================================================
// Generic async method call
// ============================================================

void SystemdManager::callAsyncStringInLoop(
    const char *destination,
    const char *path,
    const char *interface,
    const char *method,
    ReplyType replyType,
    std::string operation,
    MessageBuilder builder,
    InternalStringCallback callback)
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr || m_bus == nullptr)
    {
        callback(makeStringError(
                -ENOTCONN,
                "SystemdManager is not connected"));
        return ;
    }

    loop->assertInLoopThread();

    sd_bus_message *rawMessage = nullptr;

    int r =
        sd_bus_message_new_method_call(
            m_bus,
            &rawMessage,
            destination,
            path,
            interface,
            method);

    if (r < 0)
    {
        callback(
            makeStringError(
                r,
                operation +
                    ": unable to create D-Bus message: " +
                    strerror(-r)));

        return;
    }

    /*
     * RAII per il messaggio outgoing.
     */
    std::unique_ptr<sd_bus_message, decltype(&sd_bus_message_unref)>
        message(rawMessage, &sd_bus_message_unref);

    if (builder)
    {
        r = builder(message.get());

        if (r < 0)
        {
            callback(
                makeStringError(
                    r,
                    operation +
                        ": unable to build D-Bus message: " +
                        strerror(-r)));

            return;
        }
    }

    auto pending = std::make_unique<PendingStringCall>();

    pending->owner = this;
    pending->replyType = replyType;
    pending->callback = std::move(callback);
    pending->operation = std::move(operation);

    PendingStringCall *pendingPtr = pending.get();

    sd_bus_slot *slot = nullptr;

    r = sd_bus_call_async(
            m_bus,
            &slot,
            message.get(),
            &SystemdManager::onStringReply,
            pendingPtr,
            0);

    if (r < 0)
    {
        auto cb = std::move(pending->callback);

        cb(makeStringError(r,
                pending->operation +
                ": unable to queue D-Bus call: " +
                strerror(-r)));

        return ;
    }

    pending->slot =slot;

    m_pendingCalls.emplace(pendingPtr, std::move(pending));

    /*
     * La call potrebbe aver aggiunto dati alla write queue,
     * quindi POLLOUT/timeout potrebbero essere cambiati.
     */
    updateBusWatch();
}


// ============================================================
// D-Bus async reply
// ============================================================

int SystemdManager::onStringReply(sd_bus_message *message, void *userdata, sd_bus_error *)
{
    auto *call = static_cast<PendingStringCall *>(userdata);

    if (call == nullptr || call->owner == nullptr)
        return (1);

    StringResult result;

    // --------------------------------------------------------
    // D-Bus method error
    // --------------------------------------------------------

    if (sd_bus_message_is_method_error(message, nullptr) > 0)
    {
        const int errorNumber = sd_bus_message_get_errno(message);

        result = makeStringError((errorNumber > 0) ? -errorNumber : -EIO,
            messageError(message, call->operation));
    }

    // --------------------------------------------------------
    // Normal reply
    // --------------------------------------------------------

    else
    {
        switch (call->replyType)
        {
            // ------------------------------------------------
            // "o"
            // ------------------------------------------------

            case ReplyType::ObjectPath:
            {
                const char *value = nullptr;
                const int r = sd_bus_message_read(message, "o", &value);

                if (r <= 0 || value == nullptr)
                {
                    result = makeStringError(
                        (r < 0) ? r : -EBADMSG,
                        call->operation + ": invalid object path reply");
                }
                else
                    result = makeStringSuccess(value);

                break;
            }

            // ------------------------------------------------
            // variant<string>
            // ------------------------------------------------

            case ReplyType::VariantString:
            {
                int r = sd_bus_message_enter_container(
                    message,
                    SD_BUS_TYPE_VARIANT,
                    "s");

                if (r <= 0)
                {
                    result = makeStringError(
                        (r < 0) ? r : -EBADMSG,
                        call->operation + ": expected variant<string>");

                    break;
                }

                const char *value = nullptr;

                r = sd_bus_message_read(message, "s", &value);

                if (r <= 0 || value == nullptr)
                {
                    result = makeStringError(
                        (r < 0) ? r : -EBADMSG,
                        ": unable to read string property");
                }
                else
                    result = makeStringSuccess(value);

                /*
                 * Siamo entrati nel variant.
                 * Proviamo comunque a chiuderlo.
                 */
                const int exitResult =
                    sd_bus_message_exit_container(message);

                if (result.ok && exitResult < 0)
                {
                    result = makeStringError(
                        exitResult,
                        call->operation + ": malformed variant reply");
                }

                break;
            }
        }
    }

    /*
     * IMPORTANTE:
     *
     * questa funzione viene chiamata DENTRO sd_bus_process().
     *
     * Non vogliamo quindi eseguire immediatamente altra logica
     * che possa generare nuove elaborazioni complesse del bus.
     *
     * Rimandiamo il completamento al prossimo giro
     * dell'EventLoop.
     */
    auto *owner = call->owner;
    auto *loop = owner->m_loop.load(std::memory_order_acquire);

    if (loop != nullptr)
    {
        loop->queueInLoop(
            [owner,
             call,
             result = std::move(result)]() mutable
            {
                owner->completePendingCall(
                    call,
                    std::move(result));
            });
    }

    /*
     * Questa risposta è gestita da noi.
     */
    return (1);
}


// ============================================================
// Completion outside sd_bus_process()
// ============================================================

void SystemdManager::completePendingCall(
    PendingStringCall *call,
    StringResult result)
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr)
        return;

    loop->assertInLoopThread();

    auto it = m_pendingCalls.find(call);

    if (it == m_pendingCalls.end())
        return ;

    auto callback = std::move(it->second->callback);

    /*
     * Distrugge PendingStringCall e fa unref dello slot.
     *
     * Siamo ormai fuori dalla callback sd_bus.
     */
    m_pendingCalls.erase(it);

    if (callback)
        callback(std::move(result));
}


// ============================================================
// Error extraction
// ============================================================

std::string SystemdManager::messageError(
    sd_bus_message *message,
    const std::string &operation)
{
    const sd_bus_error *error =
        sd_bus_message_get_error(
            message);

    if (error != nullptr &&
        error->message != nullptr)
    {
        return operation +
            ": " +
            error->message;
    }

    if (error != nullptr &&
        error->name != nullptr)
    {
        return operation +
            ": " +
            error->name;
    }

    return operation +
        ": D-Bus method error";
}


// ============================================================
// Internal systemd calls
// ============================================================

void SystemdManager::callUnitMethodInLoop(
    const char *method,
    const std::string &unit,
    InternalStringCallback callback)
{
    auto operation = std::string(method) + "(" + unit + ")";

    callAsyncStringInLoop(
        SYSTEMD_DESTINATION,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        method,
        ReplyType::ObjectPath,
        std::move(operation),

        [unit](sd_bus_message *message)
        {
            return sd_bus_message_append(
                message,
                "ss",
                unit.c_str(),
                "replace");
        },

        std::move(callback));
}


void SystemdManager::loadUnitPathInLoop(
    const std::string &unit,
    InternalStringCallback callback)
{
    callAsyncStringInLoop(
        SYSTEMD_DESTINATION,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        "LoadUnit",
        ReplyType::ObjectPath,
        "LoadUnit(" + unit + ")",

        [unit](
            sd_bus_message *message)
        {
            return sd_bus_message_append(
                message,
                "s",
                unit.c_str());
        },

        std::move(callback));
}


void SystemdManager::getStringPropertyInLoop(
    const std::string &objectPath,
    const char *interface,
    const char *property,
    InternalStringCallback callback)
{
    const std::string interfaceString(interface);

    const std::string propertyString(property);

    callAsyncStringInLoop(
        SYSTEMD_DESTINATION,
        objectPath.c_str(),
        DBUS_PROPERTIES_INTERFACE,
        "Get",
        ReplyType::VariantString,
        "Get(" +
            interfaceString +
            "." +
            propertyString +
            ")",

        [interfaceString,
         propertyString](
            sd_bus_message *message)
        {
            return sd_bus_message_append(
                message,
                "ss",
                interfaceString.c_str(),
                propertyString.c_str());
        },

        std::move(callback));
}


// ============================================================
// Public startUnit()
// ============================================================

void SystemdManager::startUnit(
    std::string unit,
    StringCallback callback)
{
    trantor::EventLoop *replyLoop = trantor::EventLoop::getEventLoopOfCurrentThread();

    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr)
    {
        deliverStringResult(
            replyLoop,
            std::move(callback),
            makeStringError(
                -ENOTCONN,
                "SystemdManager is not started"));

        return ;
    }

    loop->queueInLoop(
        [this,
         unit = std::move(unit),
         replyLoop,
         callback = std::move(callback)]() mutable
        {
            callUnitMethodInLoop("StartUnit", unit,
                [replyLoop,
                 callback = std::move(callback)]
                (StringResult result) mutable
                {
                    deliverStringResult(
                        replyLoop,
                        std::move(callback),
                        std::move(result));
                });
        });
}


// ============================================================
// Public stopUnit()
// ============================================================

void SystemdManager::stopUnit(
    std::string unit,
    StringCallback callback)
{
    trantor::EventLoop *replyLoop = trantor::EventLoop::getEventLoopOfCurrentThread();

    auto *loop = m_loop.load(std::memory_order_acquire);

    if (loop == nullptr)
    {
        deliverStringResult(
            replyLoop,
            std::move(callback),
            makeStringError(
                -ENOTCONN,
                "SystemdManager is not started"));

        return;
    }

    loop->queueInLoop(
        [this,
         unit = std::move(unit),
         replyLoop,
         callback =
             std::move(callback)]() mutable
        {
            callUnitMethodInLoop(
                "StopUnit",
                unit,

                [replyLoop,
                 callback =
                     std::move(callback)]
                (StringResult result) mutable
                {
                    deliverStringResult(
                        replyLoop,
                        std::move(callback),
                        std::move(result));
                });
        });
}


// ============================================================
// Public restartUnit()
// ============================================================

void SystemdManager::restartUnit(
    std::string unit,
    StringCallback callback)
{
    trantor::EventLoop *replyLoop =
        trantor::EventLoop::
            getEventLoopOfCurrentThread();

    auto *loop =
        m_loop.load(
            std::memory_order_acquire);

    if (loop == nullptr)
    {
        deliverStringResult(
            replyLoop,
            std::move(callback),
            makeStringError(
                -ENOTCONN,
                "SystemdManager is not started"));

        return;
    }

    loop->queueInLoop(
        [this,
         unit = std::move(unit),
         replyLoop,
         callback =
             std::move(callback)]() mutable
        {
            callUnitMethodInLoop(
                "RestartUnit",
                unit,

                [replyLoop,
                 callback =
                     std::move(callback)]
                (StringResult result) mutable
                {
                    deliverStringResult(
                        replyLoop,
                        std::move(callback),
                        std::move(result));
                });
        });
}


// ============================================================
// getUnitInfo
// ============================================================

void SystemdManager::getUnitInfo(
    std::string unit,
    UnitInfoCallback callback)
{
    trantor::EventLoop *replyLoop =
        trantor::EventLoop::
            getEventLoopOfCurrentThread();

    auto *loop =
        m_loop.load(
            std::memory_order_acquire);

    if (loop == nullptr)
    {
        deliverUnitInfoResult(
            replyLoop,
            std::move(callback),
            makeUnitInfoError(
                -ENOTCONN,
                "SystemdManager is not started"));

        return;
    }

    loop->queueInLoop(
        [this,
         unit = std::move(unit),
         replyLoop,
         callback =
             std::move(callback)]() mutable
        {
            getUnitInfoInLoop(
                unit,
                [replyLoop,
                 callback =
                     std::move(callback)]
                (UnitInfoResult result) mutable
                {
                    deliverUnitInfoResult(
                        replyLoop,
                        std::move(callback),
                        std::move(result));
                });
        });
}


// ============================================================
// Internal getUnitInfo()
// ============================================================

void SystemdManager::getUnitInfoInLoop(
    const std::string &unit,
    UnitInfoCallback callback)
{
    auto *loop = m_loop.load(std::memory_order_acquire);

    loop->assertInLoopThread();

    /*
     * Prima dobbiamo ottenere l'object path della unit.
     */
    loadUnitPathInLoop(
        unit,
        [this,
         callback = std::move(callback)]
        (StringResult pathResult) mutable
        {
            if (!pathResult.ok)
            {
                callback(
                    makeUnitInfoError(
                        pathResult.errorCode,
                        std::move(
                            pathResult.error)));

                return;
            }

            const std::string objectPath =
                std::move(
                    pathResult.value);

            /*
             * Dopo LoadUnit possiamo leggere le 5 proprietà
             * IN PARALLELO.
             *
             * Non c'è nessun mutex: tutte le callback girano
             * nello stesso EventLoop.
             */
            auto state =
                std::make_shared<UnitInfoState>();

            state->remaining = 5;

            state->callback =
                std::move(callback);

            auto requestProperty =
                [this,
                 objectPath,
                 state](
                    const char *property,
                    std::string UnitInfo::*member)
                {
                    getStringPropertyInLoop(
                        objectPath,
                        SYSTEMD_UNIT_INTERFACE,
                        property,

                        [state,
                         member]
                        (StringResult result) mutable
                        {
                            /*
                             * Una proprietà precedente
                             * ha già fallito.
                             */
                            if (state->completed)
                            {
                                return;
                            }

                            if (!result.ok)
                            {
                                state->completed =
                                    true;

                                auto callback =
                                    std::move(
                                        state->callback);

                                callback(
                                    makeUnitInfoError(
                                        result.errorCode,
                                        std::move(
                                            result.error)));

                                return;
                            }

                            state->info.*member =
                                std::move(
                                    result.value);

                            --state->remaining;

                            if (state->remaining == 0)
                            {
                                state->completed =
                                    true;

                                auto callback =
                                    std::move(
                                        state->callback);

                                callback(
                                    makeUnitInfoSuccess(
                                        std::move(
                                            state->info)));
                            }
                        });
                };

            requestProperty(
                "Id",
                &UnitInfo::id);

            requestProperty(
                "Description",
                &UnitInfo::description);

            requestProperty(
                "LoadState",
                &UnitInfo::loadState);

            requestProperty(
                "ActiveState",
                &UnitInfo::activeState);

            requestProperty(
                "SubState",
                &UnitInfo::subState);
        });
}


// ============================================================
// Return result to caller EventLoop
// ============================================================

void SystemdManager::deliverStringResult(
    trantor::EventLoop *destinationLoop,
    StringCallback callback,
    StringResult result)
{
    if (!callback)
    {
        return;
    }

    /*
     * Se il caller proveniva da un Drogon EventLoop,
     * torniamo esattamente su quel loop.
     */
    if (destinationLoop != nullptr)
    {
        destinationLoop->queueInLoop(
            [callback =
                 std::move(callback),
             result =
                 std::move(result)]() mutable
            {
                callback(
                    std::move(result));
            });

        return;
    }

    /*
     * Se la funzione era stata chiamata da un normale thread
     * non associato ad EventLoop, la callback viene eseguita
     * sul loop D-Bus.
     */
    callback(
        std::move(result));
}


void SystemdManager::deliverUnitInfoResult(
    trantor::EventLoop *destinationLoop,
    UnitInfoCallback callback,
    UnitInfoResult result)
{
    if (!callback)
    {
        return;
    }

    if (destinationLoop != nullptr)
    {
        destinationLoop->queueInLoop(
            [callback =
                 std::move(callback),
             result =
                 std::move(result)]() mutable
            {
                callback(
                    std::move(result));
            });

        return;
    }

    callback(
        std::move(result));
}