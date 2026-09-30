#include "ServerController.hpp"

ServerController::ServerController(const std::vector<ManagedServer> &servers,
	std::shared_ptr<SystemdManager> systemdManager) : m_servers(servers),
	m_systemdManager(std::move(systemdManager))
{
}

// static std::string_view stateToString(ManagedServer::ServerState state)
// {
//     switch (state)
//     {
//         case ManagedServer::ServerState::Stopped:
//             return "stopped";
//         case ManagedServer::ServerState::Starting:
//             return "starting";
//         case ManagedServer::ServerState::Running:
//             return "running";
//         case ManagedServer::ServerState::Stopping:
//             return "stopping";
//         case ManagedServer::ServerState::Failed:
//             return "failed";
//     }
//     return "unknown";
// }

void ServerController::listServers(const drogon::HttpRequestPtr &req, Callback &&callback)
{
    Json::Value responseJson;
    Json::Value serversJson(Json::arrayValue);

    responseJson["status"] = "ok";

    for (const auto &server : m_servers)
    {
        Json::Value serverJson;

        serverJson["id"] = static_cast<Json::Int64>(server.m_id);
        serverJson["name"] = server.m_name;

        serversJson.append(serverJson);
    }

    responseJson["servers"] = std::move(serversJson);

    auto response = drogon::HttpResponse::newHttpJsonResponse(responseJson);
    response->setStatusCode(drogon::k200OK);
    callback(response);
    (void)req;
}


ManagedServer *ServerController::findServer(std::int64_t id)
{
    for (auto& server : m_servers)
    {
        if (server.m_id == id)
            return (&server);
    }
    return (nullptr);
}


void ServerController::startServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id)
{
    auto server = findServer(id);
    if (server == nullptr)
        return (callback(jsonError(drogon::k404NotFound, "Invalid server id")));

    m_systemdManager->startUnit(server->m_service,
        [callback = std::move(callback), server]
        (SystemdManager::StringResult result)
    {
        Json::Value json;

        if (result.ok)
        {
            json["server_name"] = server->m_name;
            json["action"] = "start";
            json["success"] = true;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k200OK);
            callback(response);
        }
        else
        {
            json["server_name"] = server->m_name;
            json["action"] = "start";
            json["success"] = false;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
        }
    });
    (void)req;
}


void ServerController::stopServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id)
{
    auto server = findServer(id);
    if (server == nullptr)
        return (callback(jsonError(drogon::k404NotFound, "Invalid server id")));

    m_systemdManager->stopUnit(server->m_service,
        [callback = std::move(callback), server]
        (SystemdManager::StringResult result)
    {
        Json::Value json;

        if (result.ok)
        {
            json["server_name"] = server->m_name;
            json["action"] = "stop";
            json["success"] = true;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k200OK);
            callback(response);
        }
        else
        {
            json["server_name"] = server->m_name;
            json["action"] = "stop";
            json["success"] = false;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
        }
    });
    (void)req;
}

void ServerController::restartServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id)
{
    auto server = findServer(id);
    if (server == nullptr)
        return (callback(jsonError(drogon::k404NotFound, "Invalid server id")));

    m_systemdManager->restartUnit(server->m_service,
        [callback = std::move(callback), server]
        (SystemdManager::StringResult result)
    {
        Json::Value json;

        if (result.ok)
        {
            json["server_name"] = server->m_name;
            json["action"] = "restart";
            json["success"] = true;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k200OK);
            callback(response);
        }
        else
        {
            json["server_name"] = server->m_name;
            json["action"] = "restart";
            json["success"] = false;
            auto response = drogon::HttpResponse::newHttpJsonResponse(json);
            response->setStatusCode(drogon::k500InternalServerError);
            callback(response);
        }
    });
    (void)req;
}