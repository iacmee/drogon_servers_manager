#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <json/json.h>
#include "ManagedServer.hpp"
#include <drogon/drogon.h>

namespace paperpilot
{

struct HttpsConfig
{
    bool enabled{true};

    std::string cert;
    std::string key;
};


struct SessionConfig
{
    bool enabled{true};
    std::size_t timeout{3600};
};


struct Config
{
    std::string bind{"127.0.0.1"};
    std::uint16_t port{8443};
    std::size_t threads{2};
    std::string documentRoot;
    std::string backupRoot;
    std::string databaseFile;

    HttpsConfig https;
    SessionConfig session;

    std::vector<ManagedServer> servers;
    Json::Value plugins{Json::arrayValue};
    static Config load(const std::string &filename);
};


}

drogon::HttpResponsePtr jsonError(drogon::HttpStatusCode status, std::string_view message);