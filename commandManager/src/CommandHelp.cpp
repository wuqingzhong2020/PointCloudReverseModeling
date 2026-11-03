/* 文件描述：保留 Windows 路径的词法拆分，以及共用的命令帮助格式。 */
#include "commandManager/CommandHelp.h"
#include "commandManager/CommandResult.h"
#include <cctype>
#include <sstream>
namespace rm
{
std::vector<std::string> tokenize(std::string_view text)
{
    if (text.size() > 1024 * 1024)
    {
        throw InputError("input_limit", "command", "<= 1 MiB", "too long");
    }
    std::vector<std::string> tokens;
    std::string token;
    char quote = 0;
    bool started = false;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        const char c = text[i];
        if (c == '\0')
        {
            throw InputError("invalid_token", "command", "text without NUL", "NUL");
        }
        if (quote)
        {
            if (c == quote)
            {
                quote = 0;
            }
            else if (c == '\\' && i + 1 < text.size() && text[i + 1] == quote)
            {
                // A final backslash in a quoted Windows directory is literal.
                if (i + 2 == text.size() || std::isspace(static_cast<unsigned char>(text[i + 2])))
                {
                    token += c;
                }
                else
                {
                    token += text[++i];
                }
            }
            else
            {
                token += c;
            }
        }
        else if (c == '\'' || c == '"')
        {
            quote = c;
            started = true;
        }
        else if (std::isspace(static_cast<unsigned char>(c)))
        {
            if (started)
            {
                tokens.push_back(std::move(token));
                token.clear();
                started = false;
            }
        }
        else
        {
            token += c;
            started = true;
        }
    }
    if (quote)
    {
        throw InputError("unclosed_quote", "command", "closing quote", "end of input");
    }
    if (started)
    {
        tokens.push_back(std::move(token));
    }
    return tokens;
}
std::string commandHelp(const CommandDefinition &definition, bool bound)
{
    std::ostringstream out;
    out << definition.name << " — " << definition.summary << "\n用法: " << definition.usage << '\n';
    if (!definition.description.empty())
    {
        out << definition.description << '\n';
    }
    out << "模块: " << definition.module << "  执行: " << (bound ? "已绑定" : "未绑定（仅支持校验/帮助）") << '\n';
    for (const auto &parameter : definition.parameters)
    {
        out << "  "
            << (parameter.binding == Binding::Option
                    ? parameter.option
                    : "<" + parameter.name + "> [位置 " + std::to_string(parameter.position) + "]")
            << "  " << describeRule(parameter.rule) << (parameter.required ? " 必填" : " 可选");
        if (parameter.defaultValue)
        {
            out << " 默认=" << formatValue(*parameter.defaultValue);
        }
        if (!parameter.unit.empty())
        {
            out << " 单位=" << parameter.unit;
        }
        out << "  " << parameter.description << '\n';
    }
    out << "  -h, --help  查看帮助，不执行业务\n"
        << "参数对可混排；固定位置参数保持相对顺序；-- 后均为位置值。\n返回: " << definition.returns << '\n';
    for (const auto &example : definition.examples)
    {
        out << "示例: " << example << '\n';
    }
    return out.str();
}
} // namespace rm
