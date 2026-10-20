#include "../code/http/formparser.h"

#include <cassert>
#include <cstdio>

int main()
{
    FormFields fields;

    assert(ParseUrlEncoded(
        "name=jason&message=hello+world", fields));

    assert(fields.at("name") == "jason");
    assert(fields.at("message") == "hello world");

    // 编码后的分隔符属于值，不是字段分隔符。
    assert(ParseUrlEncoded(
        "message=a%26b%3Dc", fields));
    assert(fields.at("message") == "a&b=c");

    // 字段名也需要解码。
    assert(ParseUrlEncoded("na%6De=jason", fields));
    assert(fields.at("name") == "jason");

    // 失败不应覆盖上一次成功的结果。
    assert(!ParseUrlEncoded("name=%", fields));
    assert(fields.at("name") == "jason");

    assert(!ParseUrlEncoded("name=%2", fields));
    assert(!ParseUrlEncoded("name=%GG", fields));
    assert(!ParseUrlEncoded("name", fields));
    assert(!ParseUrlEncoded("=jason", fields));

    // 解码后名字相同，也算重复。
    assert(!ParseUrlEncoded(
        "name=a&na%6De=b", fields));

    // 不接受空字段段。
    assert(!ParseUrlEncoded("name=jason&", fields));
    assert(!ParseUrlEncoded("name=jason&&x=1", fields));

    // 空值、空表单允许。
    assert(ParseUrlEncoded("message=", fields));
    assert(fields.at("message").empty());

    assert(ParseUrlEncoded("", fields));
    assert(fields.empty());

    // 字段数量上限。
    std::string many;
    for (int i = 0; i < 32; ++i)
    {
        if (i != 0)
        {
            many += '&';
        }

        many += "k" + std::to_string(i) + "=v";
    }

    assert(ParseUrlEncoded(many, fields));
    assert(fields.size() == 32);

    assert(!ParseUrlEncoded(many + "&extra=v", fields));
    assert(fields.size() == 32);

    std::puts("表单解析测试通过");
}