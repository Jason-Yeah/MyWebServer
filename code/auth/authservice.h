#pragma once

#include "../pool/sqlconnpool.h"

#include <string>

enum class AuthResult
{
    Registered,
    Verified,
    Conflict,
    Unauthorized,
    InvalidInput,
    Unavailable
};

bool ValidCredentials(const std::string& username, const std::string& password);

class AuthService
{
public:
    AuthService(const char* user, const char* password, const char* database);

    AuthResult Register(const std::string& username, const std::string& password);

    AuthResult Login(const std::string& username, const std::string& password);

private:
    struct Library
    {
        Library();
        ~Library();
    };

    Library Library_;

    SqlConnPool pool_;

    std::string dummyHash_;
};
