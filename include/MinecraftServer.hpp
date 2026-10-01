#pragma once

#include "SdBusProvider.hpp"
#include <atomic>
#include <cstdint>
#include <json/json.h>
#include <string>
#include <vector>

class MinecraftServer
{
	using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

	static constexpr const char *SYSTEMD_DESTINATION = "org.freedesktop.systemd1";
	static constexpr const char *SYSTEMD_PATH = "/org/freedesktop/systemd1";
	static constexpr const char *SYSTEMD_MANAGER_INTERFACE = "org.freedesktop.systemd1.Manager";

  public:
	enum class ServerState
	{
		Stopped,
		Starting,
		Running,
		Stopping,
		Failed
	};

  public:
	MinecraftServer(std::int64_t id, const std::string &name,
		const std::string &directory, const std::string &service,
		const std::string &logPath, SdBusProvider &sdBusProvider);
	MinecraftServer(MinecraftServer &&other);
	MinecraftServer(const MinecraftServer &) = delete;
	MinecraftServer &operator=(const MinecraftServer &) = delete;
	MinecraftServer &operator=(MinecraftServer &&) = delete;

	void start(Callback &&callback);
	void stop(Callback &&callback);
	void restart(Callback &&callback);

  private:
    enum class UnitAction
    {
        Start,
        Stop,
        Restart
    };

  private:
	void updateObjectPath();
    static int callbackStart(sd_bus_message *m, void *userdata, sd_bus_error *ret_error);
    void executeUnitAction(UnitAction action, Callback &&callback);



  public:
	const std::int64_t m_id;
	const std::string m_name;
	const std::string m_directory;
	const std::string m_service;
	const std::string m_logPath;
	std::atomic<ServerState> m_state{ServerState::Stopped}; // deve essere atomico

  private:
	std::string m_objectPath;
	SdBusProvider &m_sdBusProvider;
};
