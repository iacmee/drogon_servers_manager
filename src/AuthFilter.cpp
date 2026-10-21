#include "AuthFilter.hpp"
#include "SessionAccess.hpp"

void AuthFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&reject, drogon::FilterChainCallback &&next)
{
    const auto session = req->session();

    const auto access = SessionAccess::from(session);
    const bool authenticated = access && access->valid();

    if (!authenticated)
    {
        Json::Value json;
        json["error"] = "Authentication required";

        auto resp = drogon::HttpResponse::newHttpJsonResponse(json);
        resp->setStatusCode(drogon::k401Unauthorized);

        reject(resp);
        return ;
    }

    next();
}