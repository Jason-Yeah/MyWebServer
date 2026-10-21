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

add_executable(nonblocking_demo
    code/experiments/nonblocking_demo.cpp
)

set_target_properties(nonblocking_demo PROPERTIES
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS NO
)

target_compile_options(nonblocking_demo PRIVATE -Wall -Wextra)