#include "ManagedServer.hpp"

ManagedServer::ManagedServer(std::int64_t id, const std::string &name,
	const std::string &directory, const std::string &service,
	const std::string &logPath) : m_id(id), m_name(name), m_directory(directory),
	m_service(service), m_logPath(logPath)
{
}


// bool ManagedServer::start()
// {
//     systemd.startUnit(m_service);
// }

// bool ManagedServer::stop()
// {
//     systemd.stopUnit(m_service);
// }

// bool ManagedServer::restart()
// {
//     systemd.restartUnit(m_service);
// }

// ManagedServer::ServerState ManagedServer::state() const
// {
//     return (ServerState::Stopped);
// }
