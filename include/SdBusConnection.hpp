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
    sd_bus *getSdBus();

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
        Args &&...args);

    template <typename Handler, typename... Args>
    int callMethod(
        const char *destination,
        const char *path,
        const char *interface,
        const char *member,
        Handler &&handler,
        const char *types,
        Args &&...args);

    template <typename Handler>
    int matchSignal(
        const char *sender,
        const char *path,
        const char *interface,
        const char *member,
        Handler &&handler);

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
    std::shared_ptr<uint8_t> m_lifetimeToken{std::make_shared<uint8_t>(0)};

    
};

#include "SdBusConnection.tpp"
