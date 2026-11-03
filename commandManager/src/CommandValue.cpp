/* 文件描述：命令值的稳定文本表示与输入错误详情。 */
#include "commandManager/CommandResult.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
namespace rm
{
std::string valueType(const Value &value)
{
    static constexpr const char *names[]{"null", "bool", "int", "uint", "float", "string", "array", "object"};
    return names[value.index()];
}
std::string formatValue(const Value &value)
{
    std::ostringstream out;
    if (value.is<std::monostate>())
    {
        return "null";
    }
    if (value.is<bool>())
    {
        return value.as<bool>() ? "true" : "false";
    }
    if (value.is<std::int64_t>())
    {
        return std::to_string(value.as<std::int64_t>());
    }
    if (value.is<std::uint64_t>())
    {
        return std::to_string(value.as<std::uint64_t>());
    }
    if (value.is<double>())
    {
        out << std::setprecision(17) << value.as<double>();
        return out.str();
    }
    if (value.is<std::string>())
    {
        out << '"';
        for (const unsigned char c : value.as<std::string>())
        {
            if (c == '"' || c == '\\')
            {
                out << '\\' << c;
            }
            else if (c < 32)
            {
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << int(c);
            }
            else
            {
                out << c;
            }
        }
        out << '"';
        return out.str();
    }
    bool first = true;
    if (value.is<Array>())
    {
        out << '[';
        for (const auto &item : value.as<Array>())
        {
            if (!first)
            {
                out << ',';
            }
            first = false;
            out << formatValue(item);
        }
        out << ']';
    }
    else
    {
        out << '{';
        for (const auto &[key, item] : value.as<Arguments>())
        {
            if (!first)
            {
                out << ',';
            }
            first = false;
            out << formatValue(Value(key)) << ':' << formatValue(item);
        }
        out << '}';
    }
    return out.str();
}
CommandResult CommandResult::error(std::string code, std::string message)
{
    return {ResultKind::Error, std::move(code), std::move(message), {}, false};
}
namespace
{
std::string excerpt(std::string_view value)
{
    std::size_t end = std::min<std::size_t>(value.size(), 240);
    while (end < value.size() && end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80)
    {
        --end;
    }
    std::string result(value.substr(0, end));
    for (auto &ch : result)
    {
        if (ch == '\n' || ch == '\r' || ch == '\t')
        {
            ch = ' ';
        }
    }
    return result + (end < value.size() ? "…" : "");
}
std::string inputMessage(const std::string &code, const std::string &path, const std::string &expected,
                         const std::string &actual)
{
    const auto parameter = "参数「" + excerpt(path) + "」";
    if (code == "missing_argument" || code == "missing_field")
    {
        return "缺少" + parameter + "；要求 " + excerpt(expected) + "。";
    }
    if (code == "missing_option_value")
    {
        return parameter + "缺少值。";
    }
    if (code == "unknown_option" || code == "unknown_argument" || code == "unknown_field")
    {
        return "未知" + parameter + "。";
    }
    if (code == "duplicate_option")
    {
        return parameter + "重复，只能填写一次。";
    }
    if (code == "extra_positional")
    {
        return "多余的位置参数「" + excerpt(path) + "」：" + excerpt(actual) + "。";
    }
    if (code == "unclosed_quote")
    {
        return "输入中的引号未闭合。";
    }
    if (code == "input_limit")
    {
        return "命令输入超过 1 MiB。";
    }
    return parameter + "的值 " + excerpt(actual) + " 无效；要求 " + excerpt(expected) + "。";
}
} // namespace
InputError::InputError(std::string code, std::string path, std::string expected, std::string actual)
    : std::runtime_error(inputMessage(code, path, expected, actual)), code(std::move(code)), path(std::move(path)),
      expected(std::move(expected)), actual(std::move(actual))
{
}
} // namespace rm
