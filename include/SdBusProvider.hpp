#pragma once

#include <drogon/drogon.h>
#include "SdBusConnection.hpp"

class SdBusProvider
{
  public:
	SdBusConnection &current();

  private:
	drogon::IOThreadStorage<std::unique_ptr<SdBusConnection>> m_connection;
};