#pragma once
#include <chrono>
#include <drogon/WebSocketConnection.h>
#include <drogon/drogon.h>
#include <mutex>

// One grant per successful login. WebSockets never extend its absolute lifetime.
class SessionAccess
{
  public:
	explicit SessionAccess(std::chrono::seconds lifetime);
	bool valid();
	bool attach(const drogon::WebSocketConnectionPtr &connection);
	void revoke();
	static std::shared_ptr<SessionAccess> from(const drogon::SessionPtr &session);
	static bool allowed(const drogon::WebSocketConnectionPtr &connection);
	static bool bind(const drogon::HttpRequestPtr &request, const drogon::WebSocketConnectionPtr &connection);
  public:
	static std::chrono::seconds lifetime;

  private:
    const std::chrono::steady_clock::time_point expires;
	std::mutex mutex;
	bool revoked{false};
	std::vector<std::weak_ptr<drogon::WebSocketConnection>> connections;
};

class WebSocketOriginFilter : public drogon::HttpFilter<WebSocketOriginFilter>
{
  public:
	static std::vector<std::string> allowedOrigins;
	static bool accepts(const std::string &origin);
	void doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback
		&&reject, drogon::FilterChainCallback &&next) override;
};
