#include "authservice.h"
#include "passwordhash.h"

#include <cstdio>
#include <memory>
#include <mysql/mysql.h>
#include <stdexcept>

namespace
{
const char FIND_USER[] = "SELECT password_hash FROM users WHERE username = ? LIMIT 1";

const char INSERT_USER[] = "INSERT INTO users (username, password_hash) VALUES (?, ?)";

using Statement = std::unique_ptr<MYSQL_STMT, decltype(&mysql_stmt_close)>;

struct ThreadScope
{
    ThreadScope()
    {
        if (mysql_thread_init() != 0)
            throw std::runtime_error("database thread initialization failed");
    }
    ~ThreadScope()
    {
        mysql_thread_end();
    }
};

MYSQL_BIND BindString(const std::string& value, unsigned long& length)
{
    length = static_cast<unsigned long>(value.size());

    MYSQL_BIND bind{};
    bind.buffer_type = MYSQL_TYPE_STRING;

    bind.buffer = const_cast<char*>(value.data());
    bind.buffer_length = length;
    bind.length = &length;

    return bind;
}

AuthResult StatementError(MYSQL_STMT* stmt, const char* operation)
{
    std::fprintf(
        stderr,
        "database operation=%s mysql_errno=%u\n",
        operation,
        mysql_stmt_errno(stmt)
    );

    return AuthResult::Unavailable;
}
}

bool ValidCredentials(const std::string& username, const std::string& password)
{
    if (username.empty() || username.size() > 64 || password.size() < 8 || password.size() > 128)
        return false;

    for (unsigned char c : username)
        if (c < 0x20 || c == 0x7f)
            return false;

    return password.find('\0') == std::string::npos;
}

AuthService::Library::Library()
{
    if (mysql_library_init(0, nullptr, nullptr) != 0)
        throw std::runtime_error(
            "database library initialization failed"
        );
}

AuthService::Library::~Library()
{
    mysql_library_end();
}

AuthService::AuthService(
    const char* user,
    const char* password,
    const char* database
): pool_(user, password, 4, database),
    dummyHash_(HashPwd("non-user-dummy-password"))
{
    SqlConnLease lease(pool_);

    if (!lease.Get())
        throw std::runtime_error("database pool unavailable");

    Statement stmt(
        mysql_stmt_init(lease.Get()),
        &mysql_stmt_close
    );

    if (!stmt
        || mysql_stmt_prepare(
            stmt.get(),
            FIND_USER,
            sizeof(FIND_USER) - 1) != 0)
    {
        throw std::runtime_error(
            "database users schema is unavailable"
        );
    }
}

AuthResult AuthService::Register(
    const std::string& username,
    const std::string& password
)
{
    if (!ValidCredentials(username, password))
        return AuthResult::InvalidInput;

    try
    {
        const auto hash = HashPwd(password);

        ThreadScope thread;
        SqlConnLease lease(pool_);

        if (!lease.Get())
            return AuthResult::Unavailable;

        Statement stmt(
            mysql_stmt_init(lease.Get()),
            &mysql_stmt_close
        );

        if (!stmt)
            return AuthResult::Unavailable;

        if (mysql_stmt_prepare(
                stmt.get(),
                INSERT_USER,
                sizeof(INSERT_USER) - 1) != 0)
        {
            return StatementError(
                stmt.get(), "register_prepare"
            );
        }

        unsigned long lengths[2]{};

        MYSQL_BIND params[2] = {
            BindString(username, lengths[0]),
            BindString(hash, lengths[1])
        };

        if (mysql_stmt_bind_param(
                stmt.get(), params) != 0)
        {
            return StatementError(
                stmt.get(), "register_bind"
            );
        }

        if (mysql_stmt_execute(stmt.get()) != 0)
        {
            if (mysql_stmt_errno(stmt.get()) == 1062)
                return AuthResult::Conflict;

            return StatementError(
                stmt.get(), "register_execute"
            );
        }

        if (mysql_stmt_affected_rows(stmt.get()) != 1)
            return AuthResult::Unavailable;

        return AuthResult::Registered;
    }
    catch (...)
    {
        std::fputs("auth operation=register failed\n", stderr);
        return AuthResult::Unavailable;
    }
}

AuthResult AuthService::Login(
    const std::string& username,
    const std::string& password
)
{
    if (!ValidCredentials(username, password))
        return AuthResult::InvalidInput;

    try
    {
        std::string hash;
        bool found = false;

        {
            ThreadScope thread;
            SqlConnLease lease(pool_);

            if (!lease.Get())
                return AuthResult::Unavailable;

            Statement stmt(
                mysql_stmt_init(lease.Get()),
                &mysql_stmt_close
            );

            if (!stmt)
                return AuthResult::Unavailable;

            if (mysql_stmt_prepare(
                    stmt.get(),
                    FIND_USER,
                    sizeof(FIND_USER) - 1) != 0)
            {
                return StatementError(
                    stmt.get(), "login_prepare"
                );
            }

            unsigned long inputLength = 0;
            MYSQL_BIND input =
                BindString(username, inputLength);

            if (mysql_stmt_bind_param(
                    stmt.get(), &input) != 0)
            {
                return StatementError(
                    stmt.get(), "login_bind"
                );
            }

            if (mysql_stmt_execute(stmt.get()) != 0
                || mysql_stmt_store_result(stmt.get()) != 0)
            {
                return StatementError(
                    stmt.get(), "login_execute"
                );
            }

            char output[128]{};
            unsigned long length = 0;
            bool isNull = false;
            bool truncated = false;

            MYSQL_BIND result{};
            result.buffer_type = MYSQL_TYPE_STRING;
            result.buffer = output;
            result.buffer_length = sizeof(output);
            result.length = &length;
            result.is_null = &isNull;
            result.error = &truncated;

            if (mysql_stmt_bind_result(
                    stmt.get(), &result) != 0)
            {
                return StatementError(
                    stmt.get(), "login_result_bind"
                );
            }

            const int status =
                mysql_stmt_fetch(stmt.get());

            if (status == 0)
            {
                if (isNull
                    || truncated
                    || length > sizeof(output))
                {
                    return AuthResult::Unavailable;
                }

                hash.assign(output, length);
                found = true;
            }
            else if (status != MYSQL_NO_DATA)
            {
                return StatementError(
                    stmt.get(), "login_fetch"
                );
            }
        }

        const bool verified = VerifyPwd(
            password,
            found ? hash : dummyHash_
        );

        return found && verified
            ? AuthResult::Verified
            : AuthResult::Unauthorized;
    }
    catch (...)
    {
        std::fputs("auth operation=login failed\n", stderr);
        return AuthResult::Unavailable;
    }
}
