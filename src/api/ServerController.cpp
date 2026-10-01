#include "ServerController.hpp"

ServerController::ServerController(std::vector<MinecraftServer> servers) : m_servers(std::move(servers))
{
}

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


MinecraftServer *ServerController::findServer(std::int64_t id)
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

    server->start(std::move(callback));
    (void)req;
}


void ServerController::stopServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id)
{
    auto server = findServer(id);
    if (server == nullptr)
        return (callback(jsonError(drogon::k404NotFound, "Invalid server id")));

    server->stop(std::move(callback));
    (void)req;
}

void ServerController::restartServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id)
{
    auto server = findServer(id);
    if (server == nullptr)
        return (callback(jsonError(drogon::k404NotFound, "Invalid server id")));

    server->restart(std::move(callback));
    (void)req;
}