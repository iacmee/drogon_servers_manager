#pragma once
#include <drogon/drogon.h>
#include <string>
#include <vector>

// Register once before app.run(); origins are the externally visible panel URLs.
void configureHttpSecurity(drogon::HttpAppFramework &app,
                           const std::vector<std::string> &allowedOrigins);
