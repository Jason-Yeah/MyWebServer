#pragma once

#include <cassert>
#include <cstddef>
#include <string>

class Buffer
{
public:
    void Append(const char* data, size_t len);

    size_t ReadableBytes() const;

    const char* Peek() const;

    void Retrieve(size_t len);

private:
    std::string data_;
    size_t readPos_{0}; // 未处理数据相对位置
};