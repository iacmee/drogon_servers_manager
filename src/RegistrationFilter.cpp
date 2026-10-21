#include "RegistrationFilter.hpp"
#include "AuthLimits.hpp"

bool RegistrationLimiter::allow(const std::string &ip, Clock::time_point now)
{
    std::lock_guard lock(m_mutex);
    const auto expired = [now](const Window &window)
    {
        return window.m_count == 0 || now - window.m_start >= std::chrono::minutes(1);
    };
    if (expired(m_global))
    {
        m_global = {now, 0};
        std::erase_if(m_addresses, [&](const auto &item) { return expired(item.second); });
    }
    if (m_global.m_count >= 30) return false;
    auto it = m_addresses.find(ip);
    if (it == m_addresses.end())
    {
        // Explicit bound even when clients rotate addresses between windows.
        if (m_addresses.size() >= 4096) return false;
        it = m_addresses.emplace(ip, Window{now, 0}).first;
    }
    if (expired(it->second)) it->second = {now, 0};
    if (it->second.m_count >= 5) return false;
    ++it->second.m_count;
    ++m_global.m_count;
    return true;
}

void RegistrationFilter::doFilter(const drogon::HttpRequestPtr &req,
    drogon::FilterCallback &&reject, drogon::FilterChainCallback &&next)
{
    // Never trust caller-supplied forwarding headers. Proxy trust is configured
    // separately at deployment; until then proxy clients share its IP budget.
    if (!m_limiter.allow(req->peerAddr().toIp()))
    {
        Json::Value json;
        json["error"] = "Too many registration attempts";
        auto response = drogon::HttpResponse::newHttpJsonResponse(json);
        response->setStatusCode(drogon::k429TooManyRequests);
        response->addHeader("Retry-After", "60");
        reject(response);
        return;
    }
    next();
}
