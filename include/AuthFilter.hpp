#pragma once

#include "InviteTokens.h"
#include "Users.h"
#include <drogon/HttpFilter.h>

class AuthFilter : public drogon::HttpFilter<AuthFilter>
{
public:
    void doFilter(
        const drogon::HttpRequestPtr &req,
        drogon::FilterCallback &&reject,
        drogon::FilterChainCallback &&next) override;
};