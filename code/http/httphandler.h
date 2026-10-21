#pragma once

#include "httpparser.h"
#include "httprequest.h"
#include <string>

std::string BuildResponse(const HttpRequest& request, HttpParseResult parseResult);