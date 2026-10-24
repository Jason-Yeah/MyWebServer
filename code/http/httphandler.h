#pragma once

#include "httpparser.h"
#include "httprequest.h"
#include <string>

class AuthService;

std::string BuildResponse(const HttpRequest& request, 
                          HttpParseResult parseResult,
                          bool keepAlive = false,
                          AuthService* auth = nullptr);