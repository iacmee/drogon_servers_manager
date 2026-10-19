#include "SdBusProvider.hpp"
#include <stdexcept>

void SdBusProvider::initialize()
{
	if (m_connection)
		throw std::logic_error("SdBusProvider already initialized");

	m_connection = std::make_unique<Storage>();
}

SdBusConnection &SdBusProvider::current()
{
	if (!m_connection)
		throw std::logic_error("SdBusProvider not initialized!");

	auto &bus = m_connection->getThreadData();

	if (!bus)
	{
		auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();

		bus = std::make_unique<SdBusConnection>(loop,
				SdBusConnection::BusType::User,
				"sd_bus system one for thread");
	}

	return (*bus);
}
