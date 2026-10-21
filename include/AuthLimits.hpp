#pragma once
#include <string_view>
#include <cstddef>
#include <cctype>
namespace auth_limits
{
inline constexpr std::size_t bodyBytes = 4096;
inline bool username(std::string_view value)
{
    if (value.empty() || value.size() > 128 || value.front() == ' ' || value.back() == ' ')
        return (false);
    for (unsigned char ch : value)
        if (!std::isprint(ch))
            return (false);
    return (true);
}
inline bool password(std::string_view value, bool registering)
{
    if (value.empty() || value.size() > 128)
        return (false);
    if (!registering)
        return (true);

    std::size_t characters = 0;
    bool special = false;
    // Count Unicode code points and reject malformed UTF-8.
    for (std::size_t i = 0; i < value.size(); ++characters)
    {
        const unsigned char ch = value[i++];
        if (ch < 0x80)
        {
            special |= (ch >= '!' && ch <= '/') || (ch >= ':' && ch <= '@') ||
                       (ch >= '[' && ch <= '`') || (ch >= '{' && ch <= '~');
            continue;
        }
        const unsigned extra = ch >= 0xC2 && ch <= 0xDF ? 1 :
                               ch >= 0xE0 && ch <= 0xEF ? 2 :
                               ch >= 0xF0 && ch <= 0xF4 ? 3 : 0;
        if (!extra || i + extra > value.size())
            return (false);
        unsigned codepoint = ch & (0x7F >> extra);
        for (unsigned j = 0; j < extra; ++j)
        {
            const unsigned char next = value[i++];
            if ((next & 0xC0) != 0x80)
                return (false);
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((extra == 2 && codepoint < 0x800) ||
            (extra == 3 && codepoint < 0x10000) || codepoint > 0x10FFFF ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF))
            return (false);
    }
    return characters >= 12 && special;
}
inline bool invite(std::string_view value)
{
    if (value.size() != 43)
        return (false);
    for (unsigned char ch : value)
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-' || ch == '_'))
                return (false);
    return (true);
}
}
