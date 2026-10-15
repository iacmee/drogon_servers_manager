#include "WebSocketTest.hpp"
#include <algorithm>

WebSocketTest::WebSocketTest(std::shared_ptr<std::vector<MinecraftServer>> servers) : m_servers(std::move(servers))
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

WebSocketTest::~WebSocketTest()
{
    for (auto &server : *m_servers)
    {
        server.m_console.setOutputCallback({});
    }
}

void WebSocketTest::broadcast(std::int64_t id, std::string_view lines)
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

MinecraftServer *WebSocketTest::findServer(std::int64_t id)
{
    for (auto& server : *m_servers)
    {
        if (server.m_id == id)
            return (&server);
    }
    return (nullptr);
}


void WebSocketTest::handleNewConnection(
    const drogon::HttpRequestPtr &req,
    const drogon::WebSocketConnectionPtr &conn)
{
    const std::string path = req->path();

    const auto pos = path.find_last_of('/');
    const std::int64_t serverId = std::stoll(path.substr(pos + 1));

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
    conn->send(server->m_console.getLast100Lines());
}

void WebSocketTest::handleNewMessage(
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

void WebSocketTest::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_connections.erase(conn);

    LOG_INFO << "WebSocket disconnected";
}
