#include "AuthFilter.hpp"

void AuthFilter::doFilter(const drogon::HttpRequestPtr &req, drogon::FilterCallback &&reject, drogon::FilterChainCallback &&next)
{
    const auto session = req->session();

    const bool authenticated = session && session->getOptional<bool>("authenticated").value_or(false);

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