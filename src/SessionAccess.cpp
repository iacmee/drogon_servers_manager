#include "SessionAccess.hpp"
#include <algorithm>

std::chrono::seconds SessionAccess::m_lifetime{3600};
std::vector<std::string> WebSocketOriginFilter::m_allowedOrigins;

SessionAccess::SessionAccess(std::chrono::seconds lifetime) :
    m_expires(std::chrono::steady_clock::now() + lifetime) {}

bool SessionAccess::valid()
{
    std::lock_guard lock(m_mutex);
    return (!m_revoked && std::chrono::steady_clock::now() < m_expires);
}

bool SessionAccess::attach(const drogon::WebSocketConnectionPtr &connection)
{
    std::lock_guard lock(m_mutex);
    if (m_revoked || std::chrono::steady_clock::now() >= m_expires)
        return (false);
    std::erase_if(m_connections,
    [](const auto &weak)
    {
        return (weak.expired());
    });
    m_connections.emplace_back(connection);
    return (true);
}

void SessionAccess::revoke()
{
    std::vector<std::weak_ptr<drogon::WebSocketConnection>> closing;
    {
        std::lock_guard lock(m_mutex);
        m_revoked = true;
        closing.swap(m_connections);
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
        std::find(m_allowedOrigins.begin(), m_allowedOrigins.end(), origin) != m_allowedOrigins.end();
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

// Shared across all console sockets belonging to the same login.
bool SessionAccess::allowCommand()
{
    std::lock_guard lock(m_mutex);
    const auto now = std::chrono::steady_clock::now();

    if (m_revoked || now >= m_expires)
        return (false);

    m_commandTokens = std::min(console_limits::commandBurst, m_commandTokens + std::chrono::duration<double>(now - m_lastCommand).count() * console_limits::commandRate);
    m_lastCommand = now;
    if (m_commandTokens < 1.0)
        return (false);
    m_commandTokens -= 1.0;
    return (true);
}
