#pragma once

#include <cstdint>
#include <json/json.h>
#include <string>
#include <vector>
#include "SystemdManager.hpp"

class ManagedServer
{
//   public:
// 	enum class ServerState
// 	{
// 		Stopped,
// 		Starting,
// 		Running,
// 		Stopping,
// 		Failed
// 	};

  public:


	ManagedServer(std::int64_t id, const std::string &name,
		const std::string &directory, const std::string &service,
		const std::string &logPath);

	// ServerState state() const;

	// bool start(SystemdManager &systemd);
	// bool stop(SystemdManager &systemd);
	// bool restart(SystemdManager &systemd);

  public:
	const std::int64_t m_id;
	const std::string m_name;
	const std::string m_directory;
	const std::string m_service;
	const std::string m_logPath;
};

