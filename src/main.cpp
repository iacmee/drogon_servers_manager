#include "config.h"
#include "UserController.hpp"
#include "ServerController.hpp"
#include "SystemdManager.hpp"

#include <filesystem>
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
    app.setThreadNum(config.threads);
    app.setDocumentRoot(config.documentRoot);
    app.addDbClient(drogon::orm::Sqlite3Config{1, config.databaseFile, "default", -1});

    if (config.session.enabled)
        app.enableSession(config.session.timeout, drogon::Cookie::SameSite::kLax);
    if (!config.plugins.empty())
        app.addPlugins(config.plugins);
}

void registerControllers(drogon::HttpAppFramework &app,
    const paperpilot::Config &config,
    std::shared_ptr<SystemdManager> systemdManager)
{
    auto serverController = std::make_shared<ServerController>(config.servers, systemdManager);
    app.registerController(serverController);
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
        const std::string configFile = argc > 1 ? argv[1] : "config/config.json";
        const auto config = paperpilot::Config::load(configFile);
        validatePaths(config);

        auto &app = drogon::app();
        auto systemdManager = std::make_shared<SystemdManager>();
        configureApplication(app, config);
        registerControllers(app, config, systemdManager);
        app.registerBeginningAdvice(configureHodor);
        app.registerBeginningAdvice([systemdManager]()
        {
            systemdManager->start(drogon::app().getLoop());
        });
        configureListener(app, config);

        std::cout << "Bind: https://" << config.bind << ":" << config.port << "\n";
        app.run();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Fatal error: " << e.what() << '\n';
        return (1);
    }

    return (0);
}
