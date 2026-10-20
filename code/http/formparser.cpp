#include "formparser.h"
#include <cstddef>

namespace
{
int HexValue(char ch)
{
    if (ch >= '0' && ch <= '9') return ch - '0';

    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;

    if (ch >= 'A' && ch <= 'F')
    {
        return ch - 'A' + 10;
    }

return -1;
}

bool DecodePart(const std::string& input, std::string& output)
{
    output.clear();

    auto n = input.size();
    for (size_t i = 0; i < n; ++ i)
    {
        const char ch = input[i];

        if (ch == '+') output.push_back(' ');
        else if (ch == '%')
        {
            // 百分号后必须有两个字符 %xx 形式
            if (n - i < 3) return false;

            const int high = HexValue(input[i + 1]);
            const int low = HexValue(input[i + 2]);
            if (high < 0 || low < 0) return false;

            output.push_back(static_cast<char>(high * 16 + low));

            i += 2;
        }
        else output.push_back(ch);
    }
    return true;
}

} // namespace

bool ParseUrlEncoded(const std::string& body, FormFields& fields)
{
    FormFields parsed;
    if (body.empty())
    {
        fields.clear();
        return true;
    }

    size_t st = 0;
    auto n = body.size();
    while (st <= n)
    {
        // name=jason&message=hello+world
        const auto ed = body.find('&', st);
        
        // name=jason
        const std::string pair = 
            ed == std::string::npos ? 
            body.substr(st) : body.substr(st, ed - st);

        const auto equal = pair.find('=');
        // k 不能为空
        if (equal == std::string::npos || equal == 0) return false;

        std::string k;
        std::string v;
        if (!DecodePart(pair.substr(0, equal), k) || 
            !DecodePart(pair.substr(equal + 1), v))
            return false;

        // 暂定上限是 32
        if (k.empty() || parsed.size() >= 32) return false;

        // KEY is unique in unordered_map
        if (parsed.find(k) != parsed.end()) return false;

        parsed.emplace(k, v);

        if (ed == std::string::npos) break;

        st = ed + 1;
    }

    fields = parsed;
    return true;
}