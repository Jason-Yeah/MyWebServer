cmake_minimum_required(VERSION 3.16)

project(MyWebServer LANGUAGES CXX)

add_executable(server
    code/main.cpp
    code/server/epoller.cpp
    code/server/server.cpp
    code/buffer/buffer.cpp
    code/http/httprequest.cpp
    code/http/httpparser.cpp
    code/http/formparser.cpp
    code/http/httphandler.cpp
)

set_target_properties(server PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(server PRIVATE -Wall -Wextra)

# 同一实现的 LT 对照入口，仅用于回归；正式 server 仍由 main 选择 ET。
get_target_property(SERVER_TEST_SOURCES server SOURCES)
list(REMOVE_ITEM SERVER_TEST_SOURCES code/main.cpp)
add_executable(server_lt_test tests/lt_server_main.cpp ${SERVER_TEST_SOURCES})
set_target_properties(server_lt_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(server_lt_test PRIVATE -Wall -Wextra)

enable_testing()

add_executable(buffer_test
    tests/buffer_test.cpp
    code/buffer/buffer.cpp
)

set_target_properties(buffer_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(buffer_test PRIVATE -Wall -Wextra)

add_test(NAME buffer_basic COMMAND buffer_test)

add_executable(httprequest_test
    tests/httprequest_test.cpp
    code/http/httprequest.cpp
    code/buffer/buffer.cpp
   )

set_target_properties(httprequest_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(httprequest_test PRIVATE -Wall -Wextra)

add_test(NAME httprequest_basic COMMAND httprequest_test)

add_executable(httpparser_test
    tests/httpparser_test.cpp
    code/http/httpparser.cpp
    code/http/httprequest.cpp
    code/buffer/buffer.cpp
)

set_target_properties(httpparser_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(httpparser_test PRIVATE -Wall -Wextra)

add_test(NAME httpparser_basic COMMAND httpparser_test)

add_executable(formparser_test
    tests/formparser_test.cpp
    code/http/formparser.cpp
)

set_target_properties(formparser_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(formparser_test PRIVATE -Wall -Wextra)

add_test(NAME formparser_basic COMMAND formparser_test)

# Linux 资源所有权测试；epoll_wait 故障注入仅链接到这个测试目标。
add_executable(server_lifecycle_test
    tests/server_lifecycle_test.cpp
    code/server/epoller.cpp
    code/server/server.cpp
    code/buffer/buffer.cpp
    code/http/httprequest.cpp
    code/http/httpparser.cpp
    code/http/formparser.cpp
    code/http/httphandler.cpp
)
set_target_properties(server_lifecycle_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(server_lifecycle_test PRIVATE -Wall -Wextra -UNDEBUG)
target_link_options(server_lifecycle_test PRIVATE -Wl,--wrap=epoll_wait)
add_test(NAME server_lifecycle COMMAND server_lifecycle_test)
add_test(NAME server_lifecycle_et COMMAND server_lifecycle_test ET)
# 临时绑定 127.0.0.1:8080；避免和其他 CTest 并行，失败时也由进程退出回收。
set_tests_properties(server_lifecycle server_lifecycle_et PROPERTIES RUN_SERIAL TRUE TIMEOUT 10)

add_executable(nonblocking_demo
    code/experiments/nonblocking_demo.cpp
)

set_target_properties(nonblocking_demo PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(nonblocking_demo PRIVATE -Wall -Wextra)

find_package(Threads REQUIRED)

add_library(logger
    code/log/logger.cpp
)

set_target_properties(logger PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(logger PRIVATE -Wall -Wextra)

target_link_libraries(logger PUBLIC Threads::Threads)

add_library(threadpool
    code/pool/threadpool.cpp
)

set_target_properties(threadpool PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(threadpool PRIVATE -Wall -Wextra)

target_link_libraries(threadpool PUBLIC Threads::Threads)

# 三个入口共用 Server 实现，都需要线程池符号及线程链接选项。
target_link_libraries(server PRIVATE threadpool)
target_link_libraries(server_lt_test PRIVATE threadpool)
target_link_libraries(server_lifecycle_test PRIVATE threadpool)

add_executable(threadpool_test tests/threadpool_test.cpp)
set_target_properties(threadpool_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(threadpool_test PRIVATE -Wall -Wextra -UNDEBUG)
target_link_libraries(threadpool_test PRIVATE threadpool)
add_test(NAME threadpool_basic COMMAND threadpool_test)
set_tests_properties(threadpool_basic PROPERTIES TIMEOUT 20)

# 定向并发集成测试：仅测试目标访问内部状态，并替换 HTTP 构造函数为屏障替身。
# 不改正式服务器的接口、编译选项或 handler 实现。
set(ASYNC_TEST_SOURCES ${SERVER_TEST_SOURCES})
list(REMOVE_ITEM ASYNC_TEST_SOURCES code/http/httphandler.cpp)
add_executable(server_async_test tests/server_async_test.cpp ${ASYNC_TEST_SOURCES})
set_target_properties(server_async_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(server_async_test PRIVATE -Wall -Wextra -UNDEBUG -fno-access-control)
target_link_libraries(server_async_test PRIVATE threadpool)
target_link_options(server_async_test PRIVATE -Wl,--wrap=epoll_wait)
add_test(NAME server_async_lt COMMAND server_async_test LT)
add_test(NAME server_async_et COMMAND server_async_test ET)
set_tests_properties(server_async_lt server_async_et PROPERTIES TIMEOUT 20)

target_link_libraries(server PRIVATE logger)
target_link_libraries(server_lt_test PRIVATE logger)
target_link_libraries(server_lifecycle_test PRIVATE logger)
target_link_libraries(server_async_test PRIVATE logger)

add_executable(logger_test tests/logger_test.cpp)
set_target_properties(logger_test PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)
target_compile_options(logger_test PRIVATE -Wall -Wextra -UNDEBUG)
target_link_libraries(logger_test PRIVATE logger)
add_test(NAME logger_basic COMMAND logger_test)
set_tests_properties(logger_basic PROPERTIES TIMEOUT 15)

option(BUILD_DB_SMOKE "Build MySQL experiment" OFF)

if(BUILD_DB_SMOKE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MYSQLCLIENT REQUIRED
        IMPORTED_TARGET mysqlclient
    )

    add_executable(db_smoke
        code/experiments/db_smoke.cpp
    )

    set_target_properties(db_smoke PROPERTIES
        CXX_STANDARD 14
        CXX_STANDARD_REQUIRED YES
        CXX_EXTENSIONS NO
    )

    target_compile_options(db_smoke PRIVATE -Wall -Wextra)
    target_link_libraries(db_smoke PRIVATE PkgConfig::MYSQLCLIENT)
endif()
