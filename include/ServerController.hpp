#pragma once

#include <drogon/drogon.h>
#include "config.h"
#include "ManagedServer.hpp"

class ServerController : public drogon::HttpController<ServerController, false>
{
  public:
	using Callback = std::function<void(const drogon::HttpResponsePtr &)>;

    ServerController(const std::vector<ManagedServer> &servers, std::shared_ptr<SystemdManager>);

	METHOD_LIST_BEGIN

	ADD_METHOD_TO(ServerController::listServers, "/api/servers", drogon::Get, "AuthFilter");
    ADD_METHOD_TO(ServerController::startServer, "/api/servers/{1}/start", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ServerController::stopServer, "/api/servers/{1}/stop", drogon::Post, "AuthFilter");
    ADD_METHOD_TO(ServerController::restartServer, "/api/servers/{1}/restart", drogon::Post, "AuthFilter");
    
    METHOD_LIST_END
    
    void listServers(const drogon::HttpRequestPtr &req, Callback &&callback);
    void startServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id);
    void stopServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id);
    void restartServer(const drogon::HttpRequestPtr &req, Callback &&callback, std::int64_t id);

  private:
    ManagedServer *findServer(std::int64_t id);

  private:
    std::vector<ManagedServer> m_servers;
    std::shared_ptr<SystemdManager> m_systemdManager;
};