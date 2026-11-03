/* 文件描述：在调用线程顺序执行命令文本；跳过空行和整行注释，保留参数错误与已提交结果。 */
#include <cctype>
#include <string>

#include "commands/CommandFeedback.h"
#include "commands/CommandScript.h"

namespace reverse
{
bool isNativeCommandLine(const rm::CommandManager &manager, std::string_view line)
{
    const auto first = line.find_first_not_of(" \t\r");
    if (first == std::string_view::npos)
    {
        return false;
    }
    line.remove_prefix(first);
    std::size_t end = 0;
    while (end < line.size() && (std::isalnum(static_cast<unsigned char>(line[end])) ||
                                line[end] == '_' || line[end] == '.'))
    {
        ++end;
    }
    if (end == 0 || (end < line.size() && !std::isspace(static_cast<unsigned char>(line[end]))))
    {
        return false;
    }
    const auto name = line.substr(0, end);
    auto argument = line.substr(end);
    const auto value = argument.find_first_not_of(" \t\r");
    // A Python call, subscription or assignment is not shell-style command syntax.
    if (value != std::string_view::npos && std::string_view("([=:+*/%<>!&|^,@").find(argument[value]) != std::string_view::npos)
    {
        return false;
    }
    if (name == "help" || manager.definitions().contains(name))
    {
        return true;
    }
    const auto dot = name.find('.');
    if (dot != std::string_view::npos)
    {
        for (const auto &[key, definition] : manager.definitions())
        {
            if (definition.module == name.substr(0, dot))
            {
                return true; // Misspelled commands in a registered module retain command errors.
            }
        }
    }
    return false;
}
bool isNativeCommandScript(const rm::CommandManager &manager, std::string_view source)
{
    if (source.starts_with("\xEF\xBB\xBF"))
    {
        source.remove_prefix(3);
    }
    while (!source.empty())
    {
        const auto end = source.find('\n');
        const auto line = source.substr(0, end);
        source.remove_prefix(end == std::string_view::npos ? source.size() : end + 1);
        const auto first = line.find_first_not_of(" \t\r");
        if (first != std::string_view::npos && line[first] != '#' && !isNativeCommandLine(manager, line))
        {
            return false;
        }
    }
    return true;
}
rm::CommandResult executeCommandScript(rm::CommandManager &manager, std::string_view source,
                                       const rm::CommandContext &context,
                                       const std::function<bool()> &exitRequested)
{
    if (source.starts_with("\xEF\xBB\xBF"))
    {
        source.remove_prefix(3);
    }
    std::size_t lineNumber = 0;
    std::size_t commandCount = 0;
    bool committed = false;
    const auto output = [&](const std::string &text) {
        if (context.output)
        {
            context.output(text);
        }
    };
    const auto failure = [&](rm::CommandResult result) {
        result.committed = result.committed || committed;
        result.message = "第 " + std::to_string(lineNumber) + " 行：" + result.message;
        auto details = result.value.is<rm::Arguments>() ? result.value.as<rm::Arguments>() : rm::Arguments{};
        details["scriptLine"] = static_cast<std::uint64_t>(lineNumber);
        result.value = std::move(details);
        output("未完成：" + result.message + '\n');
        return result;
    };
    while (!source.empty())
    {
        ++lineNumber;
        if (context.stop.stop_requested())
        {
            return failure(rm::CommandResult::error("cancelled", "脚本任务已取消。"));
        }
        const auto end = source.find('\n');
        auto line = source.substr(0, end);
        source.remove_prefix(end == std::string_view::npos ? source.size() : end + 1);
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string_view::npos || line[first] == '#')
        {
            continue;
        }
        line = line.substr(first, line.find_last_not_of(" \t\r") - first + 1);
        output(commandExecutionEcho(line));
        auto result = manager.executeLine(line, context);
        committed = committed || result.committed;
        if (!result.ok())
        {
            return failure(std::move(result));
        }
        ++commandCount;
        const auto feedback = commandFeedback(result);
        if (!feedback.empty())
        {
            output(feedback + '\n');
        }
        if (exitRequested && exitRequested()) return result;
    }
    return {rm::ResultKind::Success, {}, "脚本执行完成，共 " + std::to_string(commandCount) + " 条命令。", {}, committed};
}
} // namespace reverse
