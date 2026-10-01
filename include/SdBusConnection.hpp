#pragma once

#include <drogon/drogon.h>

#pragma once

#include <systemd/sd-bus.h>

#include <trantor/net/Channel.h>
#include <trantor/net/EventLoop.h>

#include <cerrno>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>

class SdBusConnection final
{
public:
    enum class BusType
    {
        System,
        User
    };

    explicit SdBusConnection(
        trantor::EventLoop *loop,
        BusType type = BusType::System,
        std::string description = {});

    ~SdBusConnection();

    SdBusConnection(const SdBusConnection &) = delete;
    SdBusConnection &operator=(const SdBusConnection &) = delete;
    SdBusConnection(SdBusConnection &&) = delete;
    SdBusConnection &operator=(SdBusConnection &&) = delete;

    [[nodiscard]]
    sd_bus *nativeHandle() noexcept
    {
        return m_bus;
    }

    [[nodiscard]]
    trantor::EventLoop *eventLoop() const noexcept
    {
        return m_loop;
    }

    /**
     * Da chiamare se si usa nativeHandle() direttamente con una funzione
     * sd-bus che può cambiare gli eventi/timeout da monitorare.
     */
    void refresh();

    /**
     * Convenience wrapper per sd_bus_call_method_async().
     *
     * Aggiorna automaticamente Channel e timer dopo aver accodato
     * la chiamata.
     */
    template <typename Handler, typename... Args>
    int callMethodAsync(
        const char *destination,
        const char *path,
        const char *interface,
        const char *member,
        Handler &&handler,
        const char *types,
        Args &&...args)
    {
        m_loop->assertInLoopThread();

        using HandlerType = std::decay_t<Handler>;

        struct Context
        {
            HandlerType handler;
        };

        auto context = std::make_unique<Context>(Context{std::forward<Handler>(handler)});

        sd_bus_slot *slot = nullptr;

        /*
        * Questa è la callback C chiamata da sd-bus.
        */
        constexpr sd_bus_message_handler_t trampoline =
            [](sd_bus_message *message,
            void *userdata,
            sd_bus_error * /*retError*/) -> int
            {
                auto *ctx = static_cast<Context *>(userdata);

                try
                {
                    std::invoke(ctx->handler, message);
                }
                catch (const std::exception &e)
                {
                    LOG_ERROR
                        << "Exception in D-Bus callback: "
                        << e.what();
                }
                catch (...)
                {
                    LOG_ERROR << "Unknown exception in D-Bus callback";
                }

                return (0);
            };

        /*
        * Chiamata automaticamente quando lo slot viene distrutto.
        */
        constexpr sd_bus_destroy_t destroy =
            [](void *userdata) -> void
            {
                delete static_cast<Context *>(userdata);
            };

        const int callResult =
            sd_bus_call_method_async(
                m_bus,
                &slot,
                destination,
                path,
                interface,
                member,
                trampoline,
                context.get(),
                types,
                std::forward<Args>(args)...);

        if (callResult < 0)
            return (callResult);

        /*
        * Da qui vogliamo che lo slot possieda il Context.
        */
        int r = sd_bus_slot_set_destroy_callback(slot, destroy);

        if (r < 0)
        {
            sd_bus_slot_unref(slot);
            return (r);
        }

        /*
        * Da questo momento il Context NON appartiene più
        * allo unique_ptr locale.
        */
        context.release();

        /*
        * Trasformiamo lo slot in floating:
        * la sua lifetime viene ora gestita dal bus.
        */
        r = sd_bus_slot_set_floating(slot, 1);

        if (r < 0)
        {
            /*
            * Il destroy callback è già installato:
            * l'unref distruggerà anche Context.
            */
            sd_bus_slot_unref(slot);
            return (r);
        }

        /*
        * Importante:
        *
        * set_floating() aggiunge il riferimento posseduto dal bus.
        * Noi dobbiamo mollare il riferimento restituito originariamente
        * da sd_bus_call_method_async().
        */
        sd_bus_slot_unref(slot);

        refresh();

        return (callResult);
    }

private:
    static constexpr std::size_t kMaxProcessIterations = 64;

    void process();

    int updateEventSources();
    int updateChannel(int fd, int events);
    int updateTimer(uint64_t timeoutUsec);

    void removeChannel();

    void scheduleProcess();
    void scheduleShutdown(int error);

    void shutdownInLoop();

private:
    trantor::EventLoop *m_loop{nullptr};

    sd_bus *m_bus{nullptr};

    std::unique_ptr<trantor::Channel> m_channel;
    bool m_channelRegistered{false};

    trantor::TimerId m_timerId{trantor::InvalidTimerId};

    /*
     * Non possiede SdBusConnection.
     *
     * Serve esclusivamente per rendere innocue eventuali callback
     * già accodate quando la connessione viene distrutta.
     */
    std::shared_ptr<uint8_t> m_lifetimeToken{
        std::make_shared<uint8_t>(0)};
};

