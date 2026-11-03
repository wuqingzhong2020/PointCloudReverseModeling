/* 文件描述：解析文本参数中的 JSON 数组和对象，纯标准库且限制深度。 */
#include "commandManager/CommandResult.h"
#include <cctype>
#include <charconv>
namespace rm
{
namespace
{
class Reader
{
  public:
    explicit Reader(std::string_view text) : m_text(text)
    {
    }
    Value read(int depth = 0)
    {
        if (depth > 32)
        {
            fail();
        }
        space();
        if (take('"'))
        {
            return string();
        }
        if (take('['))
        {
            Array values;
            if (take(']'))
            {
                return values;
            }
            do
            {
                values.push_back(read(depth + 1));
            } while (take(','));
            if (!take(']'))
            {
                fail();
            }
            return values;
        }
        if (take('{'))
        {
            Arguments values;
            if (take('}'))
            {
                return values;
            }
            do
            {
                if (!take('"'))
                {
                    fail();
                }
                auto key = string();
                if (!take(':'))
                {
                    fail();
                }
                if (!values.emplace(key, read(depth + 1)).second)
                {
                    fail();
                }
            } while (take(','));
            if (!take('}'))
            {
                fail();
            }
            return values;
        }
        for (const auto &[token, value] : {std::pair{"true", Value(true)}, {"false", Value(false)}, {"null", Value{}}})
        {
            if (m_text.substr(m_index).starts_with(token))
            {
                m_index += std::char_traits<char>::length(token);
                return value;
            }
        }
        const auto start = m_index;
        while (m_index < m_text.size() &&
               std::string_view("0123456789+-.eE").find(m_text[m_index]) != std::string_view::npos)
        {
            ++m_index;
        }
        const auto number = m_text.substr(start, m_index - start);
        const auto *begin = number.data();
        const auto *end = begin + number.size();
        if (number.empty())
        {
            fail();
        }
        if (number.find_first_of(".eE") != std::string_view::npos)
        {
            double result{};
            const auto parsed = std::from_chars(begin, end, result);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
            {
                fail();
            }
            return result;
        }
        if (number.front() == '-')
        {
            std::int64_t result{};
            const auto parsed = std::from_chars(begin, end, result);
            if (parsed.ec != std::errc{} || parsed.ptr != end)
            {
                fail();
            }
            return result;
        }
        std::uint64_t result{};
        const auto parsed = std::from_chars(begin, end, result);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
        {
            fail();
        }
        return result;
    }
    void finish()
    {
        space();
        if (m_index != m_text.size())
        {
            fail();
        }
    }

  private:
    std::string_view m_text;
    std::size_t m_index{};
    [[noreturn]] void fail() const
    {
        throw InputError("invalid_json_value", "offset " + std::to_string(m_index), "JSON array/object",
                         std::string(m_text));
    }
    void space()
    {
        while (m_index < m_text.size() && std::isspace(static_cast<unsigned char>(m_text[m_index])))
        {
            ++m_index;
        }
    }
    bool take(char c)
    {
        space();
        if (m_index < m_text.size() && m_text[m_index] == c)
        {
            ++m_index;
            return true;
        }
        return false;
    }
    unsigned hex()
    {
        if (m_index + 4 > m_text.size())
        {
            fail();
        }
        unsigned result{};
        const auto parsed = std::from_chars(m_text.data() + m_index, m_text.data() + m_index + 4, result, 16);
        if (parsed.ec != std::errc{} || parsed.ptr != m_text.data() + m_index + 4)
        {
            fail();
        }
        m_index += 4;
        return result;
    }
    std::string string()
    {
        std::string result;
        while (m_index < m_text.size())
        {
            const char c = m_text[m_index++];
            if (c == '"')
            {
                return result;
            }
            if (static_cast<unsigned char>(c) < 32)
            {
                fail();
            }
            if (c != '\\')
            {
                result += c;
                continue;
            }
            if (m_index == m_text.size())
            {
                fail();
            }
            const char escape = m_text[m_index++];
            const auto index = std::string_view("\"\\/bfnrt").find(escape);
            if (index != std::string_view::npos)
            {
                result += std::string_view("\"\\/\b\f\n\r\t")[index];
                continue;
            }
            if (escape != 'u')
            {
                fail();
            }
            unsigned code = hex();
            if (code >= 0xd800 && code <= 0xdbff)
            {
                if (m_text.substr(m_index, 2) != "\\u")
                {
                    fail();
                }
                m_index += 2;
                const auto low = hex();
                if (low < 0xdc00 || low > 0xdfff)
                {
                    fail();
                }
                code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
            }
            else if (code >= 0xdc00 && code <= 0xdfff)
            {
                fail();
            }
            if (code < 0x80)
            {
                result += static_cast<char>(code);
            }
            else
            {
                if (code >= 0x10000)
                {
                    result += static_cast<char>(0xf0 | (code >> 18));
                    result += static_cast<char>(0x80 | ((code >> 12) & 63));
                }
                else if (code >= 0x800)
                {
                    result += static_cast<char>(0xe0 | (code >> 12));
                }
                else
                {
                    result += static_cast<char>(0xc0 | (code >> 6));
                }
                if (code >= 0x800)
                {
                    result += static_cast<char>(0x80 | ((code >> 6) & 63));
                }
                result += static_cast<char>(0x80 | (code & 63));
            }
        }
        fail();
    }
};
} // namespace
Value parseTextValue(std::string_view text)
{
    Reader reader(text);
    auto result = reader.read();
    reader.finish();
    return result;
}
} // namespace rm
