#include "SessionAccess.hpp"
#include "ConsoleLimits.hpp"
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
    std::vector<std::shared_ptr<ConsoleOutput>> clients;

    {
        std::lock_guard lock(m_mutex);

        for (const auto &[conn, client] : m_connections)
        {
            if (client.m_serverId == id)
                clients.push_back(client.m_output);
        }
    }

    for (const auto &output : clients)
        output->enqueue(lines);
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
    if (!SessionAccess::bind(req, conn))
        return;

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

    auto output = std::make_shared<ConsoleOutput>(conn);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_connections.emplace(conn, Client{serverId, output});
    }

    LOG_INFO << "WebSocket connected to server " << serverId;
    output->enqueue("Connected to Minecraft server " + std::to_string(serverId) + "\n");
    auto servers = m_servers;
    drogon::app().getLoop()->queueInLoop([servers, server, conn, output]
    {
        if (SessionAccess::allowed(conn))
            output->enqueue(server->m_console.getLast100Lines());
    });
}

void ConsoleWebSocketController::handleNewMessage(
    const drogon::WebSocketConnectionPtr &conn,
    std::string &&message,
    const drogon::WebSocketMessageType &type)
{
    if (!SessionAccess::allowed(conn))
    {
        conn->forceClose();
        return ;
    }
    Client client;
    {
        std::lock_guard lock(m_mutex);
        const auto it = m_connections.find(conn);
        if (it == m_connections.end() || !it->second.m_acceptingCommands)
            return ;
        client = it->second;
    }
    if (type == drogon::WebSocketMessageType::Pong)
    {
        client.m_output->acknowledge(message);
        return ;
    }
    if (type == drogon::WebSocketMessageType::Ping || type == drogon::WebSocketMessageType::Close)
        return ;
    const auto reject = [&](const std::string &reason)
    {
        {
            std::lock_guard lock(m_mutex);
            if (auto it = m_connections.find(conn); it != m_connections.end())
                it->second.m_acceptingCommands = false;
        }
        conn->shutdown(drogon::CloseCode::kViolation, reason);
        drogon::app().getLoop()->runAfter(1, [weak = std::weak_ptr<drogon::WebSocketConnection>(conn)]
        {
            if (auto connection = weak.lock())
                connection->forceClose();
        });
    };
    const auto access = conn->getContext<SessionAccess>();
    if (!access->allowCommand())
    {
        // Closing also bounds error replies during a flood.
        reject("Command rate exceeded");
        return ;
    }
    if (type != drogon::WebSocketMessageType::Text || !console_limits::validCommand(message))
    {
        reject("Expected one command, maximum 4096 bytes");
        return;
    }
    auto *server = findServer(client.m_serverId);
    if (!server)
        return ;
    server->m_console.sendCommand(std::move(message),
        [weak = std::weak_ptr<drogon::WebSocketConnection>(conn)]
        {
            auto connection = weak.lock();
            return (connection && SessionAccess::allowed(connection));
        },
        [weak = std::weak_ptr<ConsoleOutput>(client.m_output)](std::string_view error)
        {
            if (auto output = weak.lock())
                output->enqueue("[console error: " + std::string(error) + "]\n");
        });
}

void ConsoleWebSocketController::handleConnectionClosed(
    const drogon::WebSocketConnectionPtr &conn)
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_connections.erase(conn);

    LOG_INFO << "WebSocket disconnected";
}
