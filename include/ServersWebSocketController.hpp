#pragma once

#include "MinecraftServer.hpp"
#include <drogon/WebSocketController.h>
#include <unordered_set>

class ServersWebSocketController : public drogon::WebSocketController<ServersWebSocketController, false>,
    public std::enable_shared_from_this<ServersWebSocketController>
{
  public:
    explicit ServersWebSocketController(std::shared_ptr<std::vector<MinecraftServer>> servers);
    void handleNewConnection(const drogon::HttpRequestPtr &req,
        const drogon::WebSocketConnectionPtr &conn) override;
    void handleNewMessage(const drogon::WebSocketConnectionPtr &conn,
        std::string &&message, const drogon::WebSocketMessageType &type) override;
    void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;
    void broadcastStatus(std::int64_t id, const char *status);

    WS_PATH_LIST_BEGIN
    WS_PATH_ADD("/ws/servers", "AuthFilter");
    WS_PATH_LIST_END

  private:
    // Connections, snapshots and state events are serialized on the main loop.
    std::shared_ptr<std::vector<MinecraftServer>> m_servers;
    std::unordered_set<drogon::WebSocketConnectionPtr> m_connections;
};
