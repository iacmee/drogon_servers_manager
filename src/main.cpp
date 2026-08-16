#include "config.h"
#include "UserController.hpp"
#include <drogon/drogon.h>
#include <filesystem>
#include <iostream>
#include <sodium.h>

int	main(int argc, char *argv[])
{
	try
	{
        if (sodium_init() < 0)
            throw std::runtime_error("Cannot initialize libsodium");
    
		const std::string configFile = argc > 1 ? argv[1] : "/config/config.json";
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
        auto controller = std::make_shared<UserController>(config.databaseFile);
        app.registerController(controller);

        // insert db
        app.addDbClient(drogon::orm::Sqlite3Config{1, config.databaseFile, "default", -1});

		// Sessioni

		if (config.session.enabled)
			app.enableSession(config.session.timeout, drogon::Cookie::SameSite::kLax);

		// HTTPS

		if (config.https.enabled)
		{
			app.setSSLFiles(config.https.cert, config.https.key);
			app.addListener(config.bind, config.port, true);
		}
		else
			app.addListener(config.bind, config.port, false);


		std::cout << "PaperPilot\n"
					<< "Bind: " << config.bind << ":" << config.port << "\n"
					<< "HTTPS: " << (config.https.enabled ? "enabled" : "disabled") << "\n"
					<< "Document root: " << config.documentRoot << "\n"
					<< "Managed servers: " << config.servers.size() << "\n";

		app.run();
	}
	catch (const std::exception &e)
	{
		std::cerr << "Fatal error: " << e.what() << '\n';
		return (1);
	}

	return (0);
}