#include "ConsoleWebSocketController.hpp"
#include <algorithm>
#include <charconv>

ConsoleWebSocketController::ConsoleWebSocketController(std::shared_ptr<std::vector<MinecraftServer>> servers) : m_servers(std::move(servers))
{
    for (auto &server : *m_servers)
    {
        const std::int64_t id = server.m_id;

        server.m_console.setOutputCallback(
            [this, id](std::string_view lines)
            {
                broadcast(id, lines);
            }
        );
    }
}

ConsoleWebSocketController::~ConsoleWebSocketController()
{
    for (auto &server : *m_servers)
    {
        server.m_console.setOutputCallback({});
    }
}

void ConsoleWebSocketController::broadcast(std::int64_t id, std::string_view lines)
{
    std::vector<drogon::WebSocketConnectionPtr> clients;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        for (const auto &[conn, serverId] : m_connections)
        {
            if (serverId == id)
                clients.push_back(conn);
        }
    }

    std::string message(lines);

    for (const auto &conn : clients)
    {
        if (conn->connected())
            conn->send(message);
    }
}

MinecraftServer *ConsoleWebSocketController::findServer(std::int64_t id)
{
    for (auto& server : *m_servers)
    {
        if (server.m_id == id)
            return (&server);
    }
    return (nullptr);
}


void ConsoleWebSocketController::handleNewConnection(
    const drogon::HttpRequestPtr &req,
    const drogon::WebSocketConnectionPtr &conn)
{
    const std::string path = req->path();

    const auto pos = path.find_last_of('/');
    std::int64_t serverId;
    const auto idText = std::string_view(path).substr(pos + 1);
    const auto [end, error] = std::from_chars(idText.data(),
        idText.data() + idText.size(), serverId);
    if (error != std::errc{} || end != idText.data() + idText.size())
    {
        conn->forceClose();
        return;
    }

    auto server = findServer(serverId);
    if (!server)
    {
        // devo rifiutare la connessione ??
        conn->forceClose();
        return ;
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.emplace(conn, serverId);
    }

    LOG_INFO << "WebSocket connected to server " << serverId;
    conn->send("Connected to Minecraft server " + std::to_string(serverId) + "\n");
    auto servers = m_servers;
    drogon::app().getLoop()->queueInLoop([servers, server, conn]
    {
        if (conn->connected())
            conn->send(server->m_console.getLast100Lines());
    });
}

void ConsoleWebSocketController::handleNewMessage(
    const drogon::WebSocketConnectionPtr &conn,
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if (type != drogon::WebSocketMessageType::Text)
        return ;

    std::int64_t serverId;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_connections.find(conn);
        if (it == m_connections.end())
            return ;
        serverId = it->second;
    }

    LOG_INFO << "Server " << serverId << " command: " << message;
    MinecraftServer *server = findServer(serverId);

    if (server)
        server->m_console.sendCommand(message);
    else
        conn->send("Minecraft server not found");
}

void ConsoleWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_connections.erase(conn);

    LOG_INFO << "WebSocket disconnected";
}
