/* 文件描述：描述参数绑定、严格类型、取值约束与帮助元数据。 */
#pragma once
#include "commandManager/CommandValue.h"
#include <memory>
#include <optional>

namespace rm
{
enum class ValueType
{
    Boolean,
    Integer,
    Unsigned,
    Number,
    String,
    Path,
    Enum,
    Array,
    Object
};
enum class Binding
{
    Option,
    Positional
};
struct Rule
{
    ValueType type{ValueType::String};
    bool nullable{};
    std::vector<std::string> choices;
    std::vector<std::pair<Value, Value>> ranges;
    std::shared_ptr<const Rule> items;
    std::map<std::string, Rule, std::less<>> properties;
    std::size_t minSize{};
    std::size_t maxSize{100000};
};
struct Parameter
{
    std::string name;
    Binding binding{Binding::Option};
    std::string option;
    std::size_t position{};
    Rule rule;
    bool required{};
    std::optional<Value> defaultValue;
    std::string description;
    std::string unit;
};
struct CommandDefinition
{
    std::string name;
    std::string module;
    std::string summary;
    std::string usage;
    std::string description;
    std::vector<Parameter> parameters;
    std::string returns;
    std::vector<std::string> examples;
};
std::string describeRule(const Rule &rule);
Value validateValue(const Rule &rule, const Value &value, const std::string &path);
Value convertToken(const Rule &rule, const std::string &text, const std::string &path);
void checkDefinition(const CommandDefinition &definition);
} // namespace rm
