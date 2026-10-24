#include "sqlconnpool.h"

#include <cstddef>
#include <exception>
#include <mutex>
#include <mysql/mysql.h>
#include <stdexcept>
#include <string>
#include <utility>

SqlConnPool::SqlConnPool(const char* user, const char* password, size_t cnt,
                        const char* database)
{
    if (!user || !password || !*user || !*password || cnt == 0)
        throw std::invalid_argument("invalid database pool configuration");

    connections_.reserve(cnt);
    idle_.reserve(cnt);

    for (size_t i = 0; i < cnt; ++ i)
    {
        ConnectionPtr pconn(mysql_init(nullptr), &mysql_close);

        if (!pconn) throw std::runtime_error("mysql_init failed");

        const unsigned int timeout = 3;

        if (mysql_options(pconn.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0
            || mysql_options(pconn.get(), MYSQL_OPT_READ_TIMEOUT, &timeout) != 0
            || mysql_options(pconn.get(), MYSQL_OPT_WRITE_TIMEOUT, &timeout) != 0)
            throw std::runtime_error("database options failed, code=" + 
                    std::to_string(mysql_errno(pconn.get())));
        
        if (!mysql_real_connect(
            pconn.get(), "127.0.0.1", user, password, database, 3306, nullptr, 0))
                throw std::runtime_error("database connection failed, code=" + 
                        std::to_string(mysql_errno(pconn.get())));

        MYSQL* raw = pconn.get();

        connections_.push_back(std::move(pconn));
        idle_.push_back(raw);
    }
}

SqlConnPool::~SqlConnPool()
{
    if (idle_.size() != connections_.size()) // still have using conn.
        std::terminate();
}

MYSQL* SqlConnPool::Take()
{
    std::lock_guard<std::mutex> lock(mtx_);
    if (idle_.empty()) return nullptr;
    auto pconn = idle_.back();
    idle_.pop_back();

    return pconn;
}

void SqlConnPool::Return(MYSQL* conn)
{
    std::lock_guard<std::mutex> lock(mtx_);
    idle_.push_back(conn);
}

SqlConnLease::SqlConnLease(SqlConnPool& pool): pool_(pool), conn_(pool.Take()) {}

SqlConnLease::~SqlConnLease()
{
    if (conn_) pool_.Return(conn_);
}

MYSQL* SqlConnLease::Get() const
{
    return conn_;
}
