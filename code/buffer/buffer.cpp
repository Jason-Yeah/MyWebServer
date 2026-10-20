#include "buffer.h"
#include <cassert>

void Buffer::Append(const char* data, size_t len)
{
    data_.append(data, len);
}

size_t Buffer::ReadableBytes() const
{
    return data_.size() - readPos_;
}

const char* Buffer::Peek() const
{
    return data_.data() + readPos_;
}

void Buffer::Retrieve(size_t len)
{
    assert(len <= ReadableBytes());
    readPos_ += len;

    // 数据都消费完后就清空，防止data_一直膨胀
    if (readPos_ == data_.size())
    {
        data_.clear();
        readPos_ = 0;
    }
}
