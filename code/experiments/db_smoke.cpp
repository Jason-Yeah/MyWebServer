#include <mysql.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

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

static int RunQuery(const char* user, const char* password)
{
    using ConnectionPtr = std::unique_ptr<MYSQL, decltype(&mysql_close)>;
    ConnectionPtr conn(mysql_init(nullptr), &mysql_close);

    if (!conn)
    {
        std::fputs("mysql_init failed\n", stderr);
        return 1;
    }

    const unsigned int timeout = 3;

    if (mysql_options(conn.get(), MYSQL_OPT_CONNECT_TIMEOUT, &timeout) != 0
        || mysql_options(conn.get(), MYSQL_OPT_READ_TIMEOUT, &timeout) != 0
        || mysql_options(conn.get(), MYSQL_OPT_WRITE_TIMEOUT, &timeout) != 0)
    {
        return DbError(conn.get(), "options");
    }

    if (!mysql_real_connect(
        conn.get(), "127.0.0.1", user, password, nullptr, 3306, nullptr, 0    
    ))
        return DbError(conn.get(), "connect");
    
    if (mysql_query(conn.get(), "SELECT 1") != 0)
        return DbError(conn.get(), "query");

    using ResultPtr = std::unique_ptr<MYSQL_RES, decltype(&mysql_free_result)>;

    ResultPtr result(mysql_store_result(conn.get()), &mysql_free_result);

    if (!result)
        return DbError(conn.get(), "store_result");

    if (mysql_num_fields(result.get()) != 1 || mysql_num_rows(result.get()) != 1)
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

    std::puts("PASS database SELECT 1");
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

    const int result = RunQuery(user, password);

    // RunQuery 已返回，结果集和连接都已释放。
    mysql_library_end();

    return result;
}
