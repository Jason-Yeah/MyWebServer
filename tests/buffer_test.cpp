#include "../code/buffer/buffer.h"

   #include <cassert>
   #include <cstdio>
   #include <string>

   int main()
   {
       Buffer buffer;
       assert(buffer.ReadableBytes() == 0);

       // 分两次追加，验证旧数据没有丢失。
       buffer.Append("GET /ind", 8);
       buffer.Append("ex.html", 7);

       assert(buffer.ReadableBytes() == 15);
       assert(std::string(buffer.Peek(), buffer.ReadableBytes())
              == "GET /index.html");

       // 消费 "GET "，剩余路径。
       buffer.Retrieve(4);

       assert(buffer.ReadableBytes() == 11);
       assert(std::string(buffer.Peek(), buffer.ReadableBytes())
              == "/index.html");

       // 消费全部数据，再追加新数据。
       buffer.Retrieve(11);
       assert(buffer.ReadableBytes() == 0);

       buffer.Append("OK", 2);
       assert(buffer.ReadableBytes() == 2);
       assert(std::string(buffer.Peek(), buffer.ReadableBytes()) == "OK");

       buffer.Retrieve(2);
       assert(buffer.ReadableBytes() == 0);

       // 多次执行追加、全部消费。
       for (int i = 0; i < 100; ++i)
       {
              buffer.Append("abc", 3);

              assert(buffer.ReadableBytes() == 3);
              assert(std::string(buffer.Peek(), buffer.ReadableBytes()) == "abc");

              buffer.Retrieve(3);
              assert(buffer.ReadableBytes() == 0);
       }

       std::puts("Buffer 测试通过");
   }
