#include "MinecraftServer.hpp"
#include <json/json.h>
#include <stdexcept>
#include <utility>

MinecraftServer::MinecraftServer(std::int64_t id, const std::string &name,
	const std::string &directory, const std::string &service,
	const std::string &logPath, SdBusProvider &sdBusProvider) : m_id(id),
	m_name(name), m_directory(directory), m_service(service),
	m_logPath(logPath), m_sdBusProvider(sdBusProvider)
{
}

MinecraftServer::MinecraftServer(MinecraftServer &&other)
    : m_id(other.m_id), m_name(other.m_name),
      m_directory(other.m_directory), m_service(other.m_service),
      m_logPath(other.m_logPath), m_state(other.m_state.load()),
      m_objectPath(std::move(other.m_objectPath)),
      m_sdBusProvider(other.m_sdBusProvider)
{
}

void MinecraftServer::start(Callback &&callback)
{
    executeUnitAction(UnitAction::Start, std::move(callback));
}

void MinecraftServer::stop(Callback &&callback)
{
    executeUnitAction(UnitAction::Stop, std::move(callback));
}

void MinecraftServer::restart(Callback &&callback)
{
    executeUnitAction(UnitAction::Restart, std::move(callback));
}

void MinecraftServer::executeUnitAction(UnitAction action, Callback &&callback)
{
    auto &bus = m_sdBusProvider.current();

    const char *method;
    const char *actionName;

    switch (action)
    {
        case UnitAction::Start:
            method = "StartUnit";
            actionName = "start";
            break;

        case UnitAction::Stop:
            method = "StopUnit";
            actionName = "stop";
            break;

        case UnitAction::Restart:
            method = "RestartUnit";
            actionName = "restart";
            break;

        default:
            throw std::invalid_argument("Invalid UnitAction");
    }

    auto callbackPtr = std::make_shared<Callback>(std::move(callback));

    auto functionCallback =
    [serverName = m_name,
     actionName = std::string(actionName),
     callbackPtr]
    (sd_bus_message *message)
    {
        Json::Value json;

        json["server_name"] = serverName;
        json["action"] = actionName;

        auto respond =
        [&](bool success, drogon::HttpStatusCode status)
        {
            json["success"] = success;

            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(status);

            (*callbackPtr)(response);
        };

        const int r = sd_bus_message_is_method_error(message, nullptr);

        if (r < 0)
        {
            LOG_ERROR << "sd_bus_message_is_method_error: "
                << std::strerror(-r);

            respond(false, drogon::k500InternalServerError);
            return ;
        }

        if (r > 0)
        {
            const sd_bus_error *error = sd_bus_message_get_error(message);

            if (error)
            {
                LOG_ERROR << "D-Bus error: "
                    << (error->name ? error->name : "") << " - "
                    << (error->message ? error->message : "");
            }

            respond(false, drogon::k500InternalServerError);
            return ;
        }

        respond(true, drogon::k200OK);
    };

    const int r = bus.callMethodAsync(
        SYSTEMD_DESTINATION,
        SYSTEMD_PATH,
        SYSTEMD_MANAGER_INTERFACE,
        method,
        std::move(functionCallback),
        "ss",
        m_service.c_str(),
        "replace");

    if (r < 0)
    {
        LOG_ERROR << "MinecraftServer::" << actionName << " : "
            << std::strerror(-r);

        Json::Value json;

        json["server_name"] = m_name;
        json["action"] = actionName;
        json["success"] = false;

        auto response = drogon::HttpResponse::newHttpJsonResponse(json);
        response->setStatusCode(drogon::k500InternalServerError);

        (*callbackPtr)(response);
    }
}
