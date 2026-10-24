#pragma once

#include <string>

std::string HashPwd(const std::string& pwd);

bool VerifyPwd(const std::string& pwd, const std::string& encoded);

