#include "AuthLimits.hpp"
#include "ConsoleLimits.hpp"
#include "SessionAccess.hpp"
#include "ServerController.hpp"
#include "ServersWebSocketController.hpp"
#include "ConsoleWebSocketController.hpp"

// Exercise the real authentication and WebSocket controllers without systemd
// or production data. The caller supplies an empty temporary database.
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    SessionAccess::m_lifetime = std::chrono::seconds(3);
    WebSocketOriginFilter::m_allowedOrigins = {"https://panel.example"};
    SdBusProvider provider;
    auto servers = std::make_shared<std::vector<MinecraftServer>>();
    servers->emplace_back(1, "test", "/tmp", "unused.service", "/tmp/unused.log",
                          "/tmp/unused.jar", "/usr/bin/java", provider);
    auto &app = drogon::app();
    app.setClientMaxBodySize(auth_limits::bodyBytes);
    app.setClientMaxWebSocketMessageSize(console_limits::commandBytes);
    app.setThreadNum(2);
    app.enableSession(60, drogon::Cookie::SameSite::kLax);
    app.addDbClient(drogon::orm::Sqlite3Config{1, argv[1], "default", -1});
    app.registerController(std::make_shared<ServerController>(servers));
    app.registerController(std::make_shared<ServersWebSocketController>(servers));
    app.registerController(std::make_shared<ConsoleWebSocketController>(servers));
    app.addListener("127.0.0.1", std::stoi(argv[2]));
    app.run();
}
