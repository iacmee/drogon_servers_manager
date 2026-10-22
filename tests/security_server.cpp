#include "AuthLimits.hpp"
#include <cstdlib>
#include <fstream>
#include "HttpSecurity.hpp"
#include "ConsoleLimits.hpp"
#include "SessionAccess.hpp"
#include "ServerController.hpp"
#include "ServersWebSocketController.hpp"
#include "ConsoleWebSocketController.hpp"

// Exercise the real authentication and WebSocket controllers without systemd
// or production data. The caller supplies an empty temporary database.
int main(int argc, char **argv)
{
    if (argc != 3 && argc != 5)
        return 2;
    SessionAccess::m_lifetime = std::chrono::seconds(3);
    WebSocketOriginFilter::m_allowedOrigins = {"https://panel.example"};
    SdBusProvider provider;
    auto servers = std::make_shared<std::vector<MinecraftServer>>();
    servers->emplace_back(1, "test", "/tmp", "unused.service", "/tmp/unused.log",
                          "/tmp/unused.jar", "/usr/bin/java", provider);
    auto &app = drogon::app();
    if (const auto *profile = std::getenv("PAPERPILOT_TEST_PROXY_PROFILE"))
    {
        std::ifstream file(profile);
        Json::Value config;
        file >> config;
        WebSocketOriginFilter::m_allowedOrigins.clear();
        for (const auto &origin : config["app"]["allowed_origins"])
            WebSocketOriginFilter::m_allowedOrigins.push_back(origin.asString());
        app.addPlugins(config["plugins"]);
        SessionAccess::m_lifetime = std::chrono::seconds(60);
    }
    configureHttpSecurity(app, WebSocketOriginFilter::m_allowedOrigins);
    app.setDocumentRoot("static");
    app.setClientMaxBodySize(auth_limits::bodyBytes);
    app.setClientMaxWebSocketMessageSize(console_limits::commandBytes);
    app.setThreadNum(2);
    app.enableSession(60, drogon::Cookie::SameSite::kLax);
    app.addDbClient(drogon::orm::Sqlite3Config{1, argv[1], "default", -1});
    app.registerController(std::make_shared<ServerController>(servers));
    app.registerController(std::make_shared<ServersWebSocketController>(servers));
    app.registerController(std::make_shared<ConsoleWebSocketController>(servers));
    if (argc == 5)
        app.setSSLFiles(argv[3], argv[4]);
    app.addListener("127.0.0.1", std::stoi(argv[2]), argc == 5);
    app.run();
}
