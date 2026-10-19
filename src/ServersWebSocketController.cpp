#include "ServersWebSocketController.hpp"

namespace
{
std::string serialize(const Json::Value &value)
{
    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";
    return (Json::writeString(writer, value));
}
}

ServersWebSocketController::ServersWebSocketController(
    std::shared_ptr<std::vector<MinecraftServer>> servers)
    : m_servers(std::move(servers))
{
}

void ServersWebSocketController::handleNewConnection(const drogon::HttpRequestPtr &,
    const drogon::WebSocketConnectionPtr &conn)
{
    auto self = shared_from_this();
    drogon::app().getLoop()->queueInLoop([self, conn]
    {
        if (!conn->connected())
            return ;
        Json::Value message;
        message["type"] = "servers.snapshot";
        message["servers"] = Json::Value(Json::arrayValue);
        for (const auto &server : *self->m_servers)
        {
            Json::Value entry;
            entry["id"] = Json::Int64(server.m_id);
            entry["name"] = server.m_name;
            entry["status"] = server.statusToString();
            message["servers"].append(std::move(entry));
        }
        conn->send(serialize(message));
        self->m_connections.insert(conn);
    });
}

void ServersWebSocketController::handleNewMessage(const drogon::WebSocketConnectionPtr &,
    std::string &&, const drogon::WebSocketMessageType &)
{
    // This endpoint only publishes state; commands use the HTTP API.
}

void ServersWebSocketController::handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn)
{
    auto self = shared_from_this();
    drogon::app().getLoop()->queueInLoop([self, conn]
    {
        self->m_connections.erase(conn);
    });
}

void ServersWebSocketController::broadcastStatus(std::int64_t id, const char *status)
{
    Json::Value message;
    message["type"] = "server.status";
    message["serverId"] = Json::Int64(id);
    message["status"] = status;
    const auto payload = serialize(message);
    for (const auto &conn : m_connections)
        if (conn->connected())
            conn->send(payload);
}
