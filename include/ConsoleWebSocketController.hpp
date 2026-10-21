#pragma once
#include <drogon/WebSocketController.h>
#include "MinecraftServer.hpp"
#include "ConsoleOutput.hpp"

class ConsoleWebSocketController : public drogon::WebSocketController<ConsoleWebSocketController, false>
{
  public:

    explicit ConsoleWebSocketController(std::shared_ptr<std::vector<MinecraftServer>> servers);
    ~ConsoleWebSocketController() override;

	void handleNewConnection(const drogon::HttpRequestPtr &req,
		const drogon::WebSocketConnectionPtr &conn) override;

	void handleNewMessage(
        const drogon::WebSocketConnectionPtr &conn,
		std::string &&message,
		const drogon::WebSocketMessageType &type) override;

	void handleConnectionClosed(const drogon::WebSocketConnectionPtr &conn) override;

    void broadcast(const std::int64_t id, std::string_view data);
    MinecraftServer *findServer(std::int64_t id);

    WS_PATH_LIST_BEGIN
    WS_ADD_PATH_VIA_REGEX(R"(^/ws/console/[0-9]+$)", "AuthFilter", "WebSocketOriginFilter");
    WS_PATH_LIST_END


  private:
    std::mutex m_mutex;
    struct Client
    {
        std::int64_t m_serverId;
        std::shared_ptr<ConsoleOutput> m_output;
        bool m_acceptingCommands{true};
    };
    std::unordered_map<drogon::WebSocketConnectionPtr, Client> m_connections;
    std::shared_ptr<std::vector<MinecraftServer>> m_servers;
    
};
