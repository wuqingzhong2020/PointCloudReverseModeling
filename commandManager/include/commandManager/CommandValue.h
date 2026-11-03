/* 文件描述：与宿主和脚本解释器无关的命令值与规范化参数。 */
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace rm
{
struct Value;
using Array = std::vector<Value>;
using Arguments = std::map<std::string, Value, std::less<>>;
struct Value : std::variant<std::monostate, bool, std::int64_t, std::uint64_t, double, std::string, Array, Arguments>
{
    using variant::variant;
    Value() : variant(std::monostate{})
    {
    }
    Value(int number) : variant(static_cast<std::int64_t>(number))
    {
    }
    Value(unsigned number) : variant(static_cast<std::uint64_t>(number))
    {
    }
    Value(const char *text) : variant(std::string(text))
    {
    }
    template <class T> const T &as() const
    {
        return std::get<T>(*this);
    }
    template <class T> bool is() const
    {
        return std::holds_alternative<T>(*this);
    }
    bool operator==(const Value &) const = default;
};
std::string formatValue(const Value &value);
std::string valueType(const Value &value);
} // namespace rm
