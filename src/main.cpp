#include "AuthLimits.hpp"
#include "HttpSecurity.hpp"
#include "ConsoleLimits.hpp"
#include "SessionAccess.hpp"
#include "config.h"
#include "UserController.hpp"
#include "ServerController.hpp"
#include "ConsoleWebSocketController.hpp"
#include "ServersWebSocketController.hpp"

#include <filesystem>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>

void validatePaths(const paperpilot::Config &config)
{
    if (!std::filesystem::exists(config.documentRoot))
        throw std::runtime_error("document_root inesistente: " + config.documentRoot);
    if (!std::filesystem::exists(config.databaseFile))
        throw std::runtime_error("database_file inesistente: " + config.databaseFile);

    if (config.https.enabled)
    {
        if (!std::filesystem::exists(config.https.cert))
            throw std::runtime_error("Certificato inesistente: " + config.https.cert);
        if (!std::filesystem::exists(config.https.key))
            throw std::runtime_error("Chiave privata inesistente: " + config.https.key);
    }
}

void configureApplication(drogon::HttpAppFramework &app, const paperpilot::Config &config)
{
    SessionAccess::m_lifetime = std::chrono::seconds(config.session.timeout);
    WebSocketOriginFilter::m_allowedOrigins = config.allowedOrigins;
    configureHttpSecurity(app, config.allowedOrigins);
    app.setClientMaxBodySize(auth_limits::bodyBytes);
    app.setClientMaxWebSocketMessageSize(console_limits::commandBytes);
    app.setThreadNum(config.threads);
    app.setDocumentRoot(config.documentRoot);
    app.addDbClient(drogon::orm::Sqlite3Config{1, config.databaseFile, "default", -1});

    if (config.session.enabled)
        app.enableSession(config.session.timeout, drogon::Cookie::SameSite::kLax);
    if (!config.plugins.empty())
        app.addPlugins(config.plugins);
}

void registerControllers(drogon::HttpAppFramework &app,
    std::shared_ptr<std::vector<MinecraftServer>> servers,
    SdBusProvider &provider)
{
    auto serverController = std::make_shared<ServerController>(servers);
    app.registerController(serverController);

    auto webSocketController = std::make_shared<ConsoleWebSocketController>(servers);
    app.registerController(webSocketController);

    auto statusController = std::make_shared<ServersWebSocketController>(servers);
    app.registerController(statusController);
    app.registerBeginningAdvice([servers, &provider, weak = std::weak_ptr<ServersWebSocketController>(statusController)]
    {
        if (servers->empty())
            return;
        // Subscribe once on the main event loop's connection.
        auto &bus = provider.current();
        const int r = bus.callMethod(
            "org.freedesktop.systemd1",
            "/org/freedesktop/systemd1",
            "org.freedesktop.systemd1.Manager",
            "Subscribe",
            [](sd_bus_message *) {},
            nullptr);

        if (r < 0)
        {
            LOG_ERROR << "systemd Subscribe: " << std::strerror(-r);
            return;
        }

        for (auto &server : *servers)
        {
            server.setStateCallback([weak, id = server.m_id](const char *status)
            {
                if (auto controller = weak.lock())
                    controller->broadcastStatus(id, status);
            });
            if (server.startMonitorOnState() < 0)
                LOG_ERROR << "Cannot monitor server " << server.m_id;
        }
    });
}

std::optional<std::string> getLoginUsername(const drogon::HttpRequestPtr &req)
{
    if (req->path() != "/api/auth/login")
        return std::nullopt;

    auto json = req->getJsonObject();
    if (!json || !json->isObject() || !json->isMember("username") ||
        !(*json)["username"].isString())
    {
        return (std::nullopt);
    }

    const std::string username = (*json)["username"].asString();
    if (username.empty() || username.size() > 128)
        return (std::nullopt);

    return (username);
}

void configureHodor()
{
    auto *hodor = drogon::app().getPlugin<drogon::plugin::Hodor>();
    if (!hodor)
        throw std::runtime_error("Hodor plugin not available");

    hodor->setUserIdGetter(getLoginUsername);
}


void configureListener(drogon::HttpAppFramework &app, const paperpilot::Config &config)
{
    if (config.https.enabled)
    {
        app.setSSLFiles(config.https.cert, config.https.key);
        app.addListener(config.bind, config.port, true);
    }
    else
        app.addListener(config.bind, config.port, false);
}

int main(int argc, char *argv[])
{
    try
    {
        SdBusProvider SdBusProvider;
        const std::string configFile = argc > 1 ? argv[1] : "config/config.json";
        auto config = paperpilot::Config::load(configFile, SdBusProvider);
        validatePaths(config);
        auto servers = std::make_shared<std::vector<MinecraftServer>>(std::move(config.servers));

        auto &app = drogon::app();
        configureApplication(app, config);
        SdBusProvider.initialize();
        registerControllers(app, servers, SdBusProvider);
        app.registerBeginningAdvice(configureHodor);
        configureListener(app, config);

        std::cout << "Bind: " << (config.https.enabled ? "https://" : "http://") << config.bind << ":" << config.port << "\n";
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return (1);
    }

    return (0);
}
