#include "HttpSecurity.hpp"
#include <algorithm>

void configureHttpSecurity(drogon::HttpAppFramework &app,
                           const std::vector<std::string> &allowedOrigins)
{
    app.registerPreRoutingAdvice(
        [allowedOrigins](const drogon::HttpRequestPtr &req,
                         drogon::AdviceCallback &&reject,
                         drogon::AdviceChainCallback &&next)
        {
            const auto method = req->method();
            if (method != drogon::Get && method != drogon::Head && method != drogon::Options)
            {
                const auto &origin = req->getHeader("origin");
                if (origin.empty() || origin == "null" ||
                    std::find(allowedOrigins.begin(), allowedOrigins.end(), origin) == allowedOrigins.end())
                {
                    Json::Value body;
                    body["error"] = "Request origin not allowed";
                    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
                    response->setStatusCode(drogon::k403Forbidden);
                    reject(response);
                    return ;
                }
            }
            next();
        });

    // Explicit WebSocket origins also cover browsers where CSP 'self' does not
    // match the corresponding ws/wss scheme.
    std::string connectSources = "'self'";
    for (const auto &origin : allowedOrigins)
    {
        if (origin.starts_with("https://"))
            connectSources += " wss://" + origin.substr(8);
        else if (origin.starts_with("http://"))
            connectSources += " ws://" + origin.substr(7);
    }
    const std::string policy = "default-src 'none'; script-src 'self'; style-src 'self'; "
        "img-src 'self'; font-src 'self'; connect-src " + connectSources +
        "; object-src 'none'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'";
    app.registerPreSendingAdvice(
        [policy](const drogon::HttpRequestPtr &req, const drogon::HttpResponsePtr &response)
        {
            // Drogon adds/rotates its session cookie before pre-sending advice.
            auto cookie = response->getCookie("JSESSIONID");
            if (!cookie.key().empty())
            {
                cookie.setSecure(true);
                cookie.setHttpOnly(true);
                cookie.setSameSite(drogon::Cookie::SameSite::kLax);
                response->addCookie(std::move(cookie));
            }
            response->addHeader("Content-Security-Policy", policy);
            response->addHeader("X-Content-Type-Options", "nosniff");
            response->addHeader("X-Frame-Options", "DENY");
            response->addHeader("Referrer-Policy", "no-referrer");
            response->addHeader("Permissions-Policy", "camera=(), microphone=(), geolocation=()");
            if (req->path().starts_with("/api/"))
                response->addHeader("Cache-Control", "no-store");
            // For TLS terminated at a proxy, configure HSTS there. Never trust
            // an arbitrary X-Forwarded-Proto header here.
            if (req->isOnSecureConnection())
                response->addHeader("Strict-Transport-Security", "max-age=31536000");
        });
}
