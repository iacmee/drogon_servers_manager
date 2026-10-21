#include "SessionAccess.hpp"
#include <algorithm>

std::chrono::seconds SessionAccess::lifetime{3600};
std::vector<std::string> WebSocketOriginFilter::allowedOrigins;

SessionAccess::SessionAccess(std::chrono::seconds lifetime) :
    expires(std::chrono::steady_clock::now() + lifetime) {}

bool SessionAccess::valid()
{
    std::lock_guard lock(mutex);
    return (!revoked && std::chrono::steady_clock::now() < expires);
}

bool SessionAccess::attach(const drogon::WebSocketConnectionPtr &connection)
{
    std::lock_guard lock(mutex);
    if (revoked || std::chrono::steady_clock::now() >= expires)
        return (false);
    std::erase_if(connections,
    [](const auto &weak)
    {
        return (weak.expired());
    });
    connections.emplace_back(connection);
    return (true);
}

void SessionAccess::revoke()
{
    std::vector<std::weak_ptr<drogon::WebSocketConnection>> closing;
    {
        std::lock_guard lock(mutex);
        revoked = true;
        closing.swap(connections);
    }
    for (const auto &weak : closing)
        if (auto connection = weak.lock())
            connection->forceClose();
}

std::shared_ptr<SessionAccess> SessionAccess::from(const drogon::SessionPtr &session)
{
    if (session)
        return (session->get<std::shared_ptr<SessionAccess>>("access"));
    return (nullptr);
}

bool SessionAccess::allowed(const drogon::WebSocketConnectionPtr &connection)
{
    const auto access = connection->getContext<SessionAccess>();
    return (access && access->valid() && connection->connected());
}

bool SessionAccess::bind(const drogon::HttpRequestPtr &request,
                         const drogon::WebSocketConnectionPtr &connection)
{
    auto access = from(request->session());
    connection->setContext(access);
    if (!access || !access->attach(connection))
    {
        connection->forceClose();
        return (false);
    }
    return (true);
}

bool WebSocketOriginFilter::accepts(const std::string &origin)
{
    return !origin.empty() && origin != "null" &&
        std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) != allowedOrigins.end();
}

void WebSocketOriginFilter::doFilter(const drogon::HttpRequestPtr &req,
    drogon::FilterCallback &&reject, drogon::FilterChainCallback &&next)
{
    if (!accepts(req->getHeader("origin")))
    {
        auto response = drogon::HttpResponse::newHttpResponse();
        response->setStatusCode(drogon::k403Forbidden);
        reject(response);
        return;
    }
    next();
}
