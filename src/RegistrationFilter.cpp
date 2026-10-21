#include "RegistrationFilter.hpp"
#include "AuthLimits.hpp"

bool RegistrationLimiter::allow(const std::string &ip, Clock::time_point now)
{
    std::lock_guard lock(mutex);
    const auto expired = [now](const Window &window)
    {
        return window.count == 0 || now - window.start >= std::chrono::minutes(1);
    };
    if (expired(global))
    {
        global = {now, 0};
        std::erase_if(addresses, [&](const auto &item) { return expired(item.second); });
    }
    if (global.count >= 30) return false;
    auto it = addresses.find(ip);
    if (it == addresses.end())
    {
        // Explicit bound even when clients rotate addresses between windows.
        if (addresses.size() >= 4096) return false;
        it = addresses.emplace(ip, Window{now, 0}).first;
    }
    if (expired(it->second)) it->second = {now, 0};
    if (it->second.count >= 5) return false;
    ++it->second.count;
    ++global.count;
    return true;
}

void RegistrationFilter::doFilter(const drogon::HttpRequestPtr &req,
    drogon::FilterCallback &&reject, drogon::FilterChainCallback &&next)
{
    // Never trust caller-supplied forwarding headers. Proxy trust is configured
    // separately at deployment; until then proxy clients share its IP budget.
    if (!limiter.allow(req->peerAddr().toIp()))
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
