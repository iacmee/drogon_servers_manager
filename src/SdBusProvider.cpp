#include "SdBusProvider.hpp"

SdBusConnection &SdBusProvider::current()
{
	auto &bus = m_connection.getThreadData();

	if (!bus)
	{
		auto *loop = trantor::EventLoop::getEventLoopOfCurrentThread();

		bus = std::make_unique<SdBusConnection>(loop,
				SdBusConnection::BusType::System,
				"sd_bus system one for thread");
	}

	return (*bus);
}