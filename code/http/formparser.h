#pragma once

#include <string>
#include <unordered_map>

using FormFields = std::unordered_map<std::string, std::string>;

bool ParseUrlEncoded(const std::string& body, FormFields& fields);