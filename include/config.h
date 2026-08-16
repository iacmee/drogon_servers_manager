#pragma once

#include <cstdint>
#include <string>
#include <vector>

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


struct ManagedServer
{
    std::string id;
    std::string name;

    std::string directory;
    std::string service;
    std::string log;
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


    static Config load(const std::string &filename);
};

}