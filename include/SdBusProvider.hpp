#pragma once

#include <drogon/drogon.h>
#include "SdBusConnection.hpp"
#include <memory>

class SdBusProvider
{
  public:
	void initialize();
	SdBusConnection &current();

  private:
	using Storage = drogon::IOThreadStorage<std::unique_ptr<SdBusConnection>>;
	std::unique_ptr<Storage> m_connection;
};
