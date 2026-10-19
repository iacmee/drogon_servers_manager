#pragma once

#include "SdBusProvider.hpp"
#include <atomic>
#include <cstdint>
#include <json/json.h>
#include <string>
#include <vector>
#include "MinecraftConsole.hpp"

class MinecraftServer
{
	using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

	static constexpr const char *SYSTEMD_DESTINATION = "org.freedesktop.systemd1";
	static constexpr const char *SYSTEMD_PATH = "/org/freedesktop/systemd1";
	static constexpr const char *SYSTEMD_MANAGER_INTERFACE = "org.freedesktop.systemd1.Manager";
	static constexpr const char *SYSTEMD_UNIT_INTERFACE = "org.freedesktop.systemd1.Unit";
	static constexpr const char *DBUS_PROPERTIES_INTERFACE = "org.freedesktop.DBus.Properties";

  public:
	enum class ServerState
	{
		Stopped,
		Starting,
		Running,
		Stopping,
		Failed,
        Unknown
	};

  public:
	MinecraftServer(std::int64_t id, const std::string &name,
		const std::string &directory, const std::string &service,
		const std::string &logPath, const std::string &jarPath,
		const std::string &javaPath, SdBusProvider &sdBusProvider);
    ~MinecraftServer();
	MinecraftServer(MinecraftServer &&other);
	MinecraftServer(const MinecraftServer &) = delete;
	MinecraftServer &operator=(const MinecraftServer &) = delete;
	MinecraftServer &operator=(MinecraftServer &&) = delete;

	void start(Callback &&callback);
	void stop(Callback &&callback);
	void restart(Callback &&callback);

    int startMonitorOnState();
    // Called on the main event loop, like state monitoring.
    void setStateCallback(std::function<void(const char *)> callback);
    const char *statusToString() const;

  private:
    enum class UnitAction
    {
        Start,
        Stop,
        Restart
    };

  private:
    void startOnMainLoop(Callback &&callback);
    void startTransient(Callback &&callback);
    void checkStartState(std::uint64_t attempt);
	int updateObjectPath(bool performInAsync);
    void executeUnitAction(UnitAction action, Callback &&callback);
    void updateStateFromString(const char *state);



  public:
	const std::int64_t m_id;
	const std::string m_name;
	const std::string m_directory;
	const std::string m_service;
	const std::string m_logPath;
    const std::string m_jarPath;
    const std::string m_javaPath;
    const std::vector<std::string> m_argv = {m_javaPath, "-jar", m_jarPath, "--nogui"};
	std::atomic<ServerState> m_state{ServerState::Stopped}; // deve essere atomico
    MinecraftConsole m_console;

  private:
    std::string m_objectPath;
    SdBusProvider &m_sdBusProvider;
    std::mutex m_mutex; // usato per aggiornare m_objectPath
    std::atomic<bool> monitorStarted{false};
    std::function<void(const char *)> m_stateCallback;
    std::uint64_t m_stateRevision{0};
    bool m_startInProgress{false}; // Main loop only.
    std::uint64_t m_startAttempt{0};
    std::shared_ptr<int> m_lifetime{std::make_shared<int>(0)};
};
