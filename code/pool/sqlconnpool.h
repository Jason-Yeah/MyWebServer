#pragma once

#include <mysql/mysql.h>

#include <cstddef>
#include <memory>
#include <mutex>
#include <vector>

class SqlConnLease;

class SqlConnPool
{
public:
    SqlConnPool(const char* user, const char* password, size_t cnt,
                const char* database = nullptr);

    ~SqlConnPool();

private:
    friend class SqlConnLease;

    MYSQL* Take();
    void Return(MYSQL* conn);

    using ConnectionPtr = std::unique_ptr<MYSQL, decltype(&mysql_close)>;

    std::vector<ConnectionPtr> connections_;
    std::vector<MYSQL*> idle_; // free connections

    std::mutex mtx_;

private:
    SqlConnPool(const SqlConnPool&) = delete;
    SqlConnPool& operator= (const SqlConnPool&) = delete;
};

class SqlConnLease
{
public:
    explicit SqlConnLease(SqlConnPool& pool);
    ~SqlConnLease();

    MYSQL* Get() const;
private:
    SqlConnPool& pool_;
    MYSQL* conn_;

private:
    SqlConnLease(const SqlConnLease&) = delete;
    SqlConnLease& operator= (const SqlConnLease&) = delete;
};
