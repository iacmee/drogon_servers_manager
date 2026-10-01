
#include "SdBusConnection.hpp"

#include <trantor/utils/Logger.h>

#include <poll.h>
#include <time.h>

#include <cstring>
#include <stdexcept>
#include <system_error>

namespace
{

[[noreturn]]
void throwSdBusError(int error, const char *operation)
{
    throw std::system_error(
        -error,
        std::generic_category(),
        operation);
}

} // namespace


SdBusConnection::SdBusConnection(
    trantor::EventLoop *loop,
    BusType type,
    std::string description)
    : m_loop(loop)
{
    if (m_loop == nullptr)
        throw std::invalid_argument("SdBusConnection: null EventLoop");

    m_loop->assertInLoopThread();

    int result = -1;

    switch (type)
    {
        case BusType::System:
        {
            if (description.empty())
            {
                result = sd_bus_open_system(&m_bus);
            }
            else
            {
                result = sd_bus_open_system_with_description(
                    &m_bus,
                    description.c_str());
            }

            break;
        }

        case BusType::User:
        {
            if (description.empty())
            {
                result = sd_bus_open_user(&m_bus);
            }
            else
            {
                result = sd_bus_open_user_with_description(
                    &m_bus,
                    description.c_str());
            }

            break;
        }

        default:
        {
            throw std::invalid_argument("BusType argument not valid");
        }
    }

    if (result < 0)
        throwSdBusError(result, "sd_bus_open");

    try
    {
        result = updateEventSources();

        if (result < 0)
            throwSdBusError(result, "sd_bus event-loop integration");

        /*
         * IOThreadStorage potrebbe essere distrutto dopo che gli EventLoop
         * Drogon sono già terminati.
         *
         * Facciamo quindi il cleanup del Channel mentre siamo ancora
         * nel thread corretto dell'EventLoop.
         */
        std::weak_ptr<uint8_t> weakToken = m_lifetimeToken;

        m_loop->runOnQuit(
            [this, weakToken]()
            {
                if (weakToken.lock())
                    shutdownInLoop();
            });
    }
    catch (...)
    {
        shutdownInLoop();
        throw;
    }
}


SdBusConnection::~SdBusConnection()
{
    /*
     * Da questo momento eventuali callback accodate non useranno più this.
     */
    m_lifetimeToken.reset();

    /*
     * Caso normale con Drogon:
     * runOnQuit() ha già fatto cleanup nel relativo I/O thread.
     */
    if (m_bus == nullptr && m_channel == nullptr)
        return;

    /*
     * Se viene distrutta prima della chiusura dell'EventLoop,
     * deve essere distrutta dal proprio thread.
     */
    if (m_loop == nullptr || !m_loop->isInLoopThread())
    {
        LOG_FATAL
            << "SdBusConnection destroyed outside its EventLoop thread";

        std::terminate();
    }

    shutdownInLoop();
}


void SdBusConnection::refresh()
{
    m_loop->assertInLoopThread();

    if (m_bus == nullptr)
        return;

    const int result = updateEventSources();

    if (result < 0)
        scheduleShutdown(result);
}


void SdBusConnection::process()
{
    m_loop->assertInLoopThread();

    if (m_bus == nullptr)
        return;

    /*
     * sd_bus_process() esegue al massimo una operazione per chiamata.
     *
     * Mettiamo un limite per evitare che molto traffico D-Bus
     * monopolizzi l'event loop HTTP.
     */
    for (std::size_t i = 0;
         i < kMaxProcessIterations;
         ++i)
    {
        const int result =
            sd_bus_process(m_bus, nullptr);

        if (result < 0)
        {
            scheduleShutdown(result);
            return;
        }

        if (result == 0)
        {
            /*
             * Non c'è altro lavoro immediatamente disponibile.
             * Chiediamo nuovamente a sd-bus cosa dobbiamo aspettare.
             */
            refresh();
            return;
        }

        /*
         * Una callback sd-bus potrebbe teoricamente aver chiuso
         * la connessione.
         */
        if (m_bus == nullptr)
            return;
    }

    /*
     * C'è ancora lavoro, ma restituiamo il controllo a Trantor
     * per non monopolizzare l'I/O thread.
     */
    scheduleProcess();
}


void SdBusConnection::scheduleProcess()
{
    std::weak_ptr<uint8_t> weakToken = m_lifetimeToken;

    m_loop->queueInLoop(
        [this, weakToken]()
        {
            auto guard = weakToken.lock();

            if (!guard)
                return;

            process();
        });
}


int SdBusConnection::updateEventSources()
{
    m_loop->assertInLoopThread();

    if (m_bus == nullptr)
        return -ENOTCONN;

    /*
     * La documentazione sd-bus richiede di rivalutare tutti e tre:
     *
     *   fd
     *   events
     *   timeout
     *
     * prima di tornare ad attendere eventi.
     */

    const int fd = sd_bus_get_fd(m_bus);

    if (fd < 0)
        return fd;

    const int events = sd_bus_get_events(m_bus);

    if (events < 0)
        return events;

    uint64_t timeoutUsec;

    const int timeoutResult =
        sd_bus_get_timeout(m_bus, &timeoutUsec);

    if (timeoutResult < 0)
        return timeoutResult;

    int result = updateChannel(fd, events);

    if (result < 0)
        return result;

    return updateTimer(timeoutUsec);
}


int SdBusConnection::updateChannel(
    int fd,
    int events)
{
    /*
     * Normalmente l'fd non cambia, ma sd-bus documenta che va
     * richiesto nuovamente. Gestiamo quindi anche questa possibilità.
     */
    if (m_channel == nullptr || m_channel->fd() != fd)
    {
        removeChannel();

        m_channel =
            std::make_unique<trantor::Channel>(
                m_loop,
                fd);

        /*
         * Per noi non importa quale evento abbia svegliato il Channel:
         * read, write, error, hup...
         *
         * In ogni caso chiediamo a sd-bus di fare progress.
         */
        m_channel->setEventCallback(
            [this]()
            {
                process();
            });
    }

    int channelEvents =
        trantor::Channel::kNoneEvent;

    if (events & (POLLIN | POLLPRI))
        channelEvents |= trantor::Channel::kReadEvent;

    if (events & POLLOUT)
        channelEvents |= trantor::Channel::kWriteEvent;

    if (!m_channelRegistered)
    {
        /*
         * Evitiamo di registrare nel poller un Channel senza eventi.
         */
        if (channelEvents != trantor::Channel::kNoneEvent)
        {
            m_channel->updateEvents(channelEvents);
            m_channelRegistered = true;
        }
    }
    else if (m_channel->events() != channelEvents)
    {
        m_channel->updateEvents(channelEvents);
    }

    return 0;
}


int SdBusConnection::updateTimer(
    uint64_t timeoutUsec)
{
    if (m_timerId != trantor::InvalidTimerId)
    {
        m_loop->invalidateTimer(m_timerId);
        m_timerId = trantor::InvalidTimerId;
    }

    /*
     * UINT64_MAX significa che sd-bus non richiede alcun timeout.
     */
    if (timeoutUsec == UINT64_MAX)
        return 0;

    timespec now{};

    if (::clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return -errno;

    const uint64_t nowUsec =
        static_cast<uint64_t>(now.tv_sec) * 1'000'000ULL +
        static_cast<uint64_t>(now.tv_nsec) / 1'000ULL;

    uint64_t delayUsec = 0;

    if (timeoutUsec > nowUsec)
        delayUsec = timeoutUsec - nowUsec;

    const double delaySeconds =
        static_cast<double>(delayUsec) /
        1'000'000.0;

    std::weak_ptr<uint8_t> weakToken = m_lifetimeToken;

    m_timerId =
        m_loop->runAfter(
            delaySeconds,
            [this, weakToken]()
            {
                auto guard = weakToken.lock();

                if (!guard)
                    return;

                m_timerId =
                    trantor::InvalidTimerId;

                process();
            });

    return 0;
}


void SdBusConnection::removeChannel()
{
    if (m_channel == nullptr)
        return;

    if (m_channelRegistered)
    {
        if (!m_channel->isNoneEvent())
            m_channel->disableAll();

        m_channel->remove();

        m_channelRegistered = false;
    }

    m_channel.reset();
}


void SdBusConnection::scheduleShutdown(int error)
{
    LOG_ERROR
        << "sd-bus error: "
        << std::strerror(-error)
        << " (" << error << ")";

    std::weak_ptr<uint8_t> weakToken = m_lifetimeToken;

    /*
     * Non distruggiamo il Channel mentre siamo dentro la sua callback.
     * Rimandiamo il cleanup al giro successivo dell'EventLoop.
     */
    m_loop->queueInLoop(
        [this, weakToken]()
        {
            auto guard = weakToken.lock();

            if (!guard)
                return;

            shutdownInLoop();
        });
}


void SdBusConnection::shutdownInLoop()
{
    m_loop->assertInLoopThread();

    if (m_timerId != trantor::InvalidTimerId)
    {
        m_loop->invalidateTimer(m_timerId);
        m_timerId = trantor::InvalidTimerId;
    }

    removeChannel();

    if (m_bus != nullptr)
    {
        /*
         * NON usiamo sd_bus_flush():
         * può essere bloccante.
         */
        sd_bus_close(m_bus);
        m_bus = sd_bus_unref(m_bus);
    }
}


