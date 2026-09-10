#include "config.h"
#include "UserController.hpp"

int	main(int argc, char *argv[])
{
	try
	{
    
		const std::string configFile = argc > 1 ? argv[1] : "config/config.json";
		paperpilot::Config config = paperpilot::Config::load(configFile);

		// Validazioni filesystem

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

		// Drogon
        
		auto &app = drogon::app();
		app.setThreadNum(config.threads);
		app.setDocumentRoot(config.documentRoot);
        app.addDbClient(drogon::orm::Sqlite3Config{1, config.databaseFile, "default", -1});

		if (config.session.enabled)
			app.enableSession(config.session.timeout, drogon::Cookie::SameSite::kLax);
        if (!config.plugins.empty())
            app.addPlugins(config.plugins);

        app.registerBeginningAdvice([]()
        {
            auto *hodor = drogon::app().getPlugin<drogon::plugin::Hodor>();

            if (!hodor)
                throw std::runtime_error("Hodor plugin not available");

            hodor->setUserIdGetter(
                [](const drogon::HttpRequestPtr &req) -> std::optional<std::string>
                {
                    if (req->path() != "/api/auth/login")
                        return (std::nullopt);

                    auto json = req->getJsonObject();

                    if (!json ||
                        !json->isObject() ||
                        !json->isMember("username") ||
                        !(*json)["username"].isString())
                    {
                        return (std::nullopt);
                    }

                    const std::string username = (*json)["username"].asString();

                    if (username.empty() ||
                        username.size() > 128)
                    {
                        return (std::nullopt);
                    }

                    return (username);
                });
        });

		if (config.https.enabled)
		{
			app.setSSLFiles(config.https.cert, config.https.key);
			app.addListener(config.bind, config.port, true);
		}
		else
			app.addListener(config.bind, config.port, false);


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