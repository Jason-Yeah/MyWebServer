#include <mysql/mysql.h>
#include "../pool/sqlconnpool.h"
#include <sys/socket.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <exception>
#include <atomic>
#include <thread>
#include <vector>

static int DbError(MYSQL* conn, const char* stage)
{
    std::fprintf(
        stderr,
        "database stage=%s mysql_errno=%u\n",
        stage,
        mysql_errno(conn)
    );
    return 1;
}

static int CheckQuery(MYSQL* conn)
{
    if (mysql_query(conn, "SELECT 1") != 0) return DbError(conn, "query");
    
    using ResultPtr = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;

    ResultPtr result(
        mysql_store_result(conn),
        &mysql_free_result
    );

    if (!result) return DbError(conn, "store_result");
    if (mysql_num_fields(result.get()) != 1
        || mysql_num_rows(result.get()) != 1)
    {
        std::fputs("unexpected result shape\n", stderr);
        return 1;
    }

    MYSQL_ROW row = mysql_fetch_row(result.get());

    if (!row || !row[0] || std::strcmp(row[0], "1") != 0)
    {
        std::fputs("unexpected result value\n", stderr);
        return 1;
    }
    return 0;
}

static int CheckConcurrent(SqlConnPool& pool)
{
    std::atomic<bool> ok{true};
    std::vector<std::thread> workers;

    auto work = [&] {
        if (mysql_thread_init() != 0)
        {
            ok.store(false);
            return;
        } 

        try 
        {
            SqlConnLease lease(pool);
            if (!lease.Get()) ok.store(false);
            else 
                for (int i = 0; i < 10; ++ i)
                    if (CheckQuery(lease.Get()) != 0)
                    {
                        ok.store(false);
                        break;
                    }
        } 
        catch (...) 
        {
            ok.store(false);
        }
        mysql_thread_end();
    };

    try
    {
        for (int i = 0; i < 2; ++ i)
            workers.emplace_back(work);
    }
    catch(...)
    {
        for (auto& worker: workers)
            worker.join();
        throw ;
    }

    for (auto& worker: workers)
        worker.join();
    
    if (!ok.load()) 
    {
        std::fputs(
            "database concurrent query failed\n",
            stderr
        );
        return 1;
    }

    std::puts("PASS database two-thread queries");
    return 0;
}

static int RunQuery(const char* user, const char* password)
{
    SqlConnPool pool(user, password, 2);
    {
        SqlConnLease first(pool);

        if (!first.Get())
            return 1;
        {
            SqlConnLease second(pool);

            if (!second.Get()
                || first.Get() == second.Get())
            {
                std::fputs(
                    "connections are not exclusive\n",
                    stderr
                );
                return 1;
            }

            SqlConnLease third(pool);

            if (third.Get())
            {
                std::fputs(
                    "pool capacity check failed\n",
                    stderr
                );
                return 1;
            }

            if (CheckQuery(first.Get()) != 0
                || CheckQuery(second.Get()) != 0)
            {
                return 1;
            }
        }
        // second 离开作用域，自动归还。

        SqlConnLease again(pool);

        if (!again.Get())
        {
            std::fputs("connection not returned\n", stderr);
            return 1;
        }

        if (CheckQuery(again.Get()) != 0)
            return 1;
    }
    // 全部借用对象已销毁，pool 才能安全销毁。

    if (CheckConcurrent(pool) != 0)
       return 1;
    std::puts(
        "PASS pool exclusive/exhausted/returned/query"
    );

    return 0;
}

int main()
{
    const char* user = std::getenv("MYWEB_DB_USER");
    const char* password = std::getenv("MYWEB_DB_PASSWORD");

    if (!user || !*user || !password || !*password)
    {
        std::fputs(
            "MYWEB_DB_USER and MYWEB_DB_PASSWORD are required\n",
            stderr
        );
        return 1;
    }

    if (mysql_library_init(0, nullptr, nullptr) != 0)
    {
        std::fputs("mysql_library_init failed\n", stderr);
        return 1;
    }

    int result = 1;

   try
   {
       result = RunQuery(user, password);
   }
   catch (const std::exception& e)
   {
       std::fprintf(
           stderr,
           "database experiment failed: %s\n",
           e.what()
       );
   }

    // RunQuery 已返回，结果集和连接都已释放。
    mysql_library_end();

    return result;
}
