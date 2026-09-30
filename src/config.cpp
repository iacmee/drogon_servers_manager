#include "config.h"
#include <fstream>
#include <jsoncpp/json/json.h>
#include <stdexcept>

namespace paperpilot
{
    namespace
    {
        std::string requireString(const Json::Value &obj, const char *name)
        {
            if (!obj.isMember(name) || !obj[name].isString())
                throw std::runtime_error(std::string("Parametro mancante/non valido: ") + name);
            return (obj[name].asString());
        }
        
        std::int64_t requireInt64(const Json::Value &obj, const char *name)
        {
            if (!obj.isMember(name) || !obj[name].isInt64())
                throw std::runtime_error(std::string("Parametro mancante/non valido: ") + name);

            return (obj[name].asInt64());
        }

        bool getBool(const Json::Value &obj, const char *name, bool defaultValue)
        {
            if (!obj.isMember(name))
                return (defaultValue);
            if (!obj[name].isBool())
                throw std::runtime_error(std::string("Parametro booleano non valido: ") + name);
            return (obj[name].asBool());
        }

        std::size_t getSize(const Json::Value &obj, const char *name,
                            std::size_t defaultValue)
        {
            if (!obj.isMember(name))
                return (defaultValue);
            if (!obj[name].isUInt())
                throw std::runtime_error(std::string("Parametro numerico non valido: ") + name);
            return (obj[name].asUInt());
        }

    } // namespace

    Config Config::load(const std::string &filename)
    {
        std::ifstream file(filename);
        if (!file)
            throw std::runtime_error("Impossibile aprire configurazione: " + filename); 

        Config cfg;
        Json::CharReaderBuilder builder;
        Json::Value root;
        std::string errors;

        if (!Json::parseFromStream(builder, file, &root, &errors))
            throw std::runtime_error("Errore parsing JSON:\n" + errors);
        if (!root.isMember("app") || !root["app"].isObject())
            throw std::runtime_error("Sezione 'app' mancante");

        const auto &app = root["app"];
        const int port = app.get("port", 8443).asInt();
        // -------------------------
        // APP
        // -------------------------
        cfg.bind = app.get("bind", "127.0.0.1").asString();
        if (port < 1 || port > 65535)
        {
            throw std::runtime_error("Porta non valida");
        }
        cfg.port = static_cast<std::uint16_t>(port);
        cfg.threads = getSize(app, "threads", 2);
        cfg.documentRoot = requireString(app, "document_root");
        cfg.backupRoot = requireString(app, "backup_root");
        cfg.databaseFile = requireString(app, "database_file");
        // -------------------------
        // HTTPS
        // -------------------------
        if (app.isMember("https"))
        {
            const auto &https = app["https"];
            cfg.https.enabled = getBool(https, "enabled", true);
            if (cfg.https.enabled)
            {
                cfg.https.cert = requireString(https, "cert");
                cfg.https.key = requireString(https, "key");
            }
        }
        // -------------------------
        // SESSION
        // -------------------------
        if (app.isMember("session"))
        {
            const auto &session = app["session"];
            cfg.session.enabled = getBool(session, "enabled", true);
            cfg.session.timeout = getSize(session, "timeout", 3600);
        }
        // -------------------------
        // SERVERS
        // -------------------------
        if (root.isMember("servers"))
        {
            if (!root["servers"].isArray())
            {
                throw std::runtime_error("'servers' deve essere un array");
            }
            for (const auto &item : root["servers"])
            {
                cfg.servers.emplace_back(requireInt64(item, "id"),
                    requireString(item, "name"), requireString(item, "directory"),
                    requireString(item, "service"), requireString(item, "logPath"));
            }
        }
        // -------------------------
        // PLUGINS
        // -------------------------
        if (root.isMember("plugins"))
        {
            if (!root["plugins"].isArray())
                throw std::runtime_error(
                    "'plugins' deve essere un array");

            cfg.plugins = root["plugins"];
        }
        return (cfg);
    }

} // namespace paperpilot