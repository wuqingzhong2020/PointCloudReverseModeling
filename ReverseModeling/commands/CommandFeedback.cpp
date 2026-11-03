/* 文件描述：默认显示命令摘要或具体错误；只有显式查询才回显完整结构化值。 */
#include <algorithm>
#include <cctype>
#include <set>

#include "commands/CommandFeedback.h"

namespace reverse
{
namespace
{
std::string quoteToken(const std::string &value)
{
    if (!value.empty() && value.find_first_of(" \t\r\n\"'") == std::string::npos)
    {
        return value;
    }
    // The lexer preserves Windows backslashes; JSON escaping cannot be used for command tokens.
    for (const char quote : {'"', '\''})
    {
        if (value.find(quote) == std::string::npos && (value.empty() || value.back() != '\\'))
        {
            return quote + value + quote;
        }
    }
    std::string result;
    for (const char c : value)
    {
        if (c == '\'')
        {
            result += "\"'\"";
        }
        else if (c == '"' || std::isspace(static_cast<unsigned char>(c)))
        {
            result += std::string("'") + c + '\'';
        }
        else
        {
            result += c;
        }
    }
    return result;
}
std::string nativeValue(const rm::Value &value)
{
    return quoteToken(value.is<std::string>() ? value.as<std::string>() : rm::formatValue(value));
}
std::string pythonValue(const rm::Value &value)
{
    if (value.is<std::monostate>())
    {
        return "None";
    }
    if (value.is<bool>())
    {
        return value.as<bool>() ? "True" : "False";
    }
    if (value.is<rm::Array>() || value.is<rm::Arguments>())
    {
        const bool array = value.is<rm::Array>();
        std::string result = array ? "[" : "{";
        const auto append = [&](const std::string &entry) {
            if (result.size() > 1)
            {
                result += ", ";
            }
            result += entry;
        };
        if (array)
        {
            for (const auto &item : value.as<rm::Array>())
            {
                append(pythonValue(item));
            }
        }
        else
        {
            for (const auto &[key, item] : value.as<rm::Arguments>())
            {
                append(rm::formatValue(key) + ": " + pythonValue(item));
            }
        }
        return result + (array ? ']' : '}');
    }
    return rm::formatValue(value);
}
} // namespace
CommandText commandText(const rm::CommandDefinition &definition, const rm::Array &positional,
                        const rm::Arguments &options)
{
    CommandText result{definition.name, "rm.execute(" + rm::formatValue(definition.name)};
    std::string positionText;
    bool terminateOptions = false;
    for (const auto &value : positional)
    {
        positionText += " " + nativeValue(value);
        terminateOptions |= value.is<std::string>() && value.as<std::string>().starts_with('-');
        result.python += ", " + pythonValue(value);
    }
    if (!terminateOptions)
    {
        result.native += positionText;
    }
    std::set<std::string> renderedOptions;
    for (const auto &parameter : definition.parameters)
    {
        const auto value = options.find(parameter.name);
        if (parameter.binding != rm::Binding::Option || value == options.end())
        {
            continue;
        }
        const auto token = nativeValue(value->second);
        // = keeps literal --help / --values from being parsed as options.
        const bool leadingDash = value->second.is<std::string>() && value->second.as<std::string>().starts_with('-');
        result.native += " " + parameter.option + (leadingDash ? "=" : " ") + token;
        renderedOptions.insert(parameter.name);
    }
    // Failed script calls must also show undeclared options instead of silently omitting them.
    for (const auto &[name, value] : options)
    {
        if (!renderedOptions.contains(name))
        {
            result.native += " --" + name + "=" + nativeValue(value);
        }
    }
    if (terminateOptions)
    {
        result.native += " --" + positionText;
    }
    if (!options.empty())
    {
        result.python += ", **" + pythonValue(options);
    }
    result.python += ')';
    return result;
}
std::string commandArgumentsText(const std::vector<std::string> &arguments)
{
    std::string result;
    for (const auto &argument : arguments)
    {
        if (!result.empty()) result += ' ';
        result += quoteToken(argument);
    }
    return result;
}
std::string commandFeedback(const rm::CommandResult &result)
{
    if (!result.message.empty())
    {
        return result.message;
    }
    if (result.kind == rm::ResultKind::Success && !result.value.is<std::monostate>())
    {
        return rm::formatValue(result.value);
    }
    return {};
}
} // namespace reverse
