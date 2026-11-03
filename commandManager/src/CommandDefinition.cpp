/* 文件描述：严格参数类型、区间并集、递归结构及定义自身的合法性校验。 */
#include "commandManager/CommandDefinition.h"
#include "commandManager/CommandResult.h"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace rm
{
Value parseTextValue(std::string_view text);
namespace
{
bool utf8(std::string_view text)
{
    for (std::size_t i = 0; i < text.size();)
    {
        const auto c = static_cast<unsigned char>(text[i++]);
        if (c < 0x80)
        {
            continue;
        }
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || i + count > text.size())
        {
            return false;
        }
        const auto first = static_cast<unsigned char>(text[i]);
        if ((c == 0xe0 && first < 0xa0) || (c == 0xed && first >= 0xa0) || (c == 0xf0 && first < 0x90) ||
            (c == 0xf4 && first >= 0x90))
        {
            return false;
        }
        while (count--)
        {
            if ((static_cast<unsigned char>(text[i++]) & 0xc0) != 0x80)
            {
                return false;
            }
        }
    }
    return true;
}
bool numeric(const Value &v)
{
    return v.is<std::int64_t>() || v.is<std::uint64_t>() || v.is<double>();
}
double real(const Value &v)
{
    if (v.is<double>())
    {
        return v.as<double>();
    }
    if (v.is<std::int64_t>())
    {
        return static_cast<double>(v.as<std::int64_t>());
    }
    return static_cast<double>(v.as<std::uint64_t>());
}
bool less(const Value &a, const Value &b)
{
    if (a.is<double>() || b.is<double>())
    {
        return real(a) < real(b);
    }
    if (a.is<std::int64_t>() && b.is<std::int64_t>())
    {
        return a.as<std::int64_t>() < b.as<std::int64_t>();
    }
    if (a.is<std::uint64_t>() && b.is<std::uint64_t>())
    {
        return a.as<std::uint64_t>() < b.as<std::uint64_t>();
    }
    if (a.is<std::int64_t>())
    {
        return std::cmp_less(a.as<std::int64_t>(), b.as<std::uint64_t>());
    }
    return std::cmp_less(a.as<std::uint64_t>(), b.as<std::int64_t>());
}
[[noreturn]] void invalid(const Rule &rule, const Value &value, const std::string &path)
{
    throw InputError("invalid_value", path, describeRule(rule), valueType(value) + " " + formatValue(value));
}
Value validate(const Rule &rule, Value value, const std::string &path, int depth)
{
    if (depth > 32)
    {
        throw InputError("nesting_limit", path, "depth <= 32", "deep value");
    }
    if (value.is<std::monostate>() && rule.nullable)
    {
        return value;
    }
    switch (rule.type)
    {
    case ValueType::Boolean:
        if (!value.is<bool>())
        {
            invalid(rule, value, path);
        }
        break;
    case ValueType::Integer:
        if (value.is<std::uint64_t>() && value.as<std::uint64_t>() <= INT64_MAX)
        {
            value = static_cast<std::int64_t>(value.as<std::uint64_t>());
        }
        if (!value.is<std::int64_t>())
        {
            invalid(rule, value, path);
        }
        break;
    case ValueType::Unsigned:
        if (value.is<std::int64_t>() && value.as<std::int64_t>() >= 0)
        {
            value = static_cast<std::uint64_t>(value.as<std::int64_t>());
        }
        if (!value.is<std::uint64_t>())
        {
            invalid(rule, value, path);
        }
        break;
    case ValueType::Number:
        if (!numeric(value) || !std::isfinite(real(value)))
        {
            invalid(rule, value, path);
        }
        value = real(value);
        break;
    case ValueType::String:
    case ValueType::Path:
    case ValueType::Enum:
        if (!value.is<std::string>())
        {
            invalid(rule, value, path);
        }
        if (!utf8(value.as<std::string>()))
        {
            throw InputError("invalid_utf8", path, "UTF-8 string", "invalid byte sequence");
        }
        if (value.as<std::string>().find('\0') != std::string::npos)
        {
            invalid(rule, value, path);
        }
        if (rule.type == ValueType::Enum &&
            std::find(rule.choices.begin(), rule.choices.end(), value.as<std::string>()) == rule.choices.end())
        {
            invalid(rule, value, path);
        }
        if (value.as<std::string>().size() < rule.minSize || value.as<std::string>().size() > rule.maxSize)
        {
            invalid(rule, value, path);
        }
        break;
    case ValueType::Array:
        if (!value.is<Array>())
        {
            invalid(rule, value, path);
        }
        if (value.as<Array>().size() < rule.minSize || value.as<Array>().size() > rule.maxSize)
        {
            invalid(rule, value, path);
        }
        for (std::size_t i = 0; i < value.as<Array>().size(); ++i)
        {
            std::get<Array>(value)[i] =
                validate(*rule.items, value.as<Array>()[i], path + "[" + std::to_string(i) + "]", depth + 1);
        }
        break;
    case ValueType::Object:
        if (!value.is<Arguments>())
        {
            invalid(rule, value, path);
        }
        for (const auto &[key, item] : value.as<Arguments>())
        {
            if (!rule.properties.contains(key))
            {
                throw InputError("unknown_field", path + "." + key, "declared field", key);
            }
            std::get<Arguments>(value)[key] = validate(rule.properties.at(key), item, path + "." + key, depth + 1);
        }
        for (const auto &[key, field] : rule.properties)
        {
            if (!value.as<Arguments>().contains(key))
            {
                throw InputError("missing_field", path + "." + key, describeRule(field), "missing");
            }
        }
        break;
    }
    if (!rule.ranges.empty())
    {
        const bool allowed = std::any_of(rule.ranges.begin(), rule.ranges.end(), [&](const auto &range) {
            return !less(value, range.first) && !less(range.second, value);
        });
        if (!allowed)
        {
            invalid(rule, value, path);
        }
    }
    return value;
}
void checkRule(const Rule &rule, int depth)
{
    if (depth > 32 || rule.minSize > rule.maxSize)
    {
        throw std::invalid_argument("Invalid rule limits");
    }
    if (rule.type == ValueType::Enum)
    {
        const std::set<std::string> unique(rule.choices.begin(), rule.choices.end());
        if (unique.empty() || unique.size() != rule.choices.size())
        {
            throw std::invalid_argument("Invalid enum choices");
        }
    }
    for (const auto &[minimum, maximum] : rule.ranges)
    {
        if (rule.type != ValueType::Integer && rule.type != ValueType::Unsigned && rule.type != ValueType::Number)
        {
            throw std::invalid_argument("Ranges require a numeric type");
        }
        Rule bound = rule;
        bound.ranges.clear();
        const auto lo = validate(bound, minimum, "range.minimum", depth);
        const auto hi = validate(bound, maximum, "range.maximum", depth);
        if (less(hi, lo))
        {
            throw std::invalid_argument("Reversed range");
        }
    }
    if (rule.type == ValueType::Array)
    {
        if (!rule.items)
        {
            throw std::invalid_argument("Array requires items rule");
        }
        checkRule(*rule.items, depth + 1);
    }
    for (const auto &[key, child] : rule.properties)
    {
        if (key.empty())
        {
            throw std::invalid_argument("Empty property name");
        }
        checkRule(child, depth + 1);
    }
}
} // namespace
std::string describeRule(const Rule &rule)
{
    static constexpr const char *names[]{"bool", "int64", "uint64", "float", "string",
                                         "path", "enum",  "array",  "object"};
    std::string result = names[static_cast<int>(rule.type)];
    if (rule.type == ValueType::Boolean)
    {
        result += " {true|false}";
    }
    if (!rule.choices.empty())
    {
        result += " {";
        for (const auto &choice : rule.choices)
        {
            result += choice + "|";
        }
        result.back() = '}';
    }
    for (const auto &[lo, hi] : rule.ranges)
    {
        result += " [" + formatValue(lo) + ", " + formatValue(hi) + "]";
    }
    if (rule.nullable)
    {
        result += " | null";
    }
    if (rule.ranges.size() > 1)
    {
        result += " (区间并集)";
    }
    if (rule.type == ValueType::Array && rule.items)
    {
        result += "<" + describeRule(*rule.items) + ">";
    }
    if ((rule.type == ValueType::Array || rule.type == ValueType::String || rule.type == ValueType::Path) &&
        (rule.minSize != 0 || rule.maxSize != 100000))
    {
        result += " 长度 [" + std::to_string(rule.minSize) + ", " + std::to_string(rule.maxSize) + "]";
    }
    return result;
}
Value validateValue(const Rule &rule, const Value &value, const std::string &path)
{
    return validate(rule, value, path, 0);
}
Value convertToken(const Rule &rule, const std::string &text, const std::string &path)
{
    Value value = text;
    const char *begin = text.data();
    const char *end = begin + text.size();
    if (rule.nullable && text == "null")
    {
        value = Value{};
    }
    else if (rule.type == ValueType::Boolean)
    {
        if (text != "true" && text != "false")
        {
            invalid(rule, value, path);
        }
        value = text == "true";
    }
    else if (rule.type == ValueType::Integer || rule.type == ValueType::Unsigned)
    {
        std::from_chars_result parsed;
        if (rule.type == ValueType::Integer)
        {
            std::int64_t number{};
            parsed = std::from_chars(begin, end, number);
            value = number;
        }
        else
        {
            std::uint64_t number{};
            parsed = std::from_chars(begin, end, number);
            value = number;
        }
        if (parsed.ec != std::errc{} || parsed.ptr != end)
        {
            invalid(rule, Value(text), path);
        }
    }
    else if (rule.type == ValueType::Number)
    {
        double number{};
        const auto parsed = std::from_chars(begin, end, number);
        if (parsed.ec != std::errc{} || parsed.ptr != end)
        {
            invalid(rule, value, path);
        }
        value = number;
    }
    else if (rule.type == ValueType::Array || rule.type == ValueType::Object)
    {
        try
        {
            value = parseTextValue(text);
        }
        catch (const InputError &error)
        {
            throw InputError(error.code, path + "." + error.path, error.expected, error.actual);
        }
    }
    return validateValue(rule, value, path);
}
void checkDefinition(const CommandDefinition &definition)
{
    if (definition.module.empty() || !definition.name.starts_with(definition.module + ".") ||
        definition.module == "help" || definition.summary.empty() ||
        definition.usage.find_first_not_of(" \t\r\n") == std::string::npos)
    {
        throw std::invalid_argument("Command requires qualified name, module, summary and nonblank usage");
    }
    std::set<std::string> names, options;
    std::map<std::size_t, bool> positions;
    for (const auto &parameter : definition.parameters)
    {
        if (parameter.name.empty() || !names.insert(parameter.name).second ||
            (parameter.required && parameter.defaultValue))
        {
            throw std::invalid_argument("Invalid parameter name/default");
        }
        if (parameter.binding == Binding::Option)
        {
            if (!parameter.option.starts_with("--") || parameter.option.size() <= 2 || parameter.option == "--help" ||
                parameter.option.find_first_of("= \t\r\n") != std::string::npos ||
                !options.insert(parameter.option).second)
            {
                throw std::invalid_argument("Invalid/duplicate/reserved option");
            }
        }
        else if (parameter.position == 0 || !positions.emplace(parameter.position, parameter.required).second)
        {
            throw std::invalid_argument("Invalid positional index");
        }
        checkRule(parameter.rule, 0);
        if (parameter.defaultValue)
        {
            (void)validateValue(parameter.rule, *parameter.defaultValue, parameter.name);
        }
    }
    std::size_t next = 1;
    bool optional = false;
    for (const auto &[position, required] : positions)
    {
        if (position != next++ || (optional && required))
        {
            throw std::invalid_argument("Positionals must be contiguous, optional only at end");
        }
        optional = !required;
    }
}
} // namespace rm
