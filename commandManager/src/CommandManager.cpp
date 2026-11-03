/* 文件描述：先选项后位置的输入校验、无副作用帮助和同步业务分发。 */
#include "commandManager/CommandManager.h"
#include "commandManager/CommandHelp.h"
#include <algorithm>
#include <memory>
#include <set>
#include <thread>

namespace rm
{
namespace
{
Rule copyRule(const Rule &source)
{
    Rule copy = source;
    if (source.items)
    {
        copy.items = std::make_shared<const Rule>(copyRule(*source.items));
    }
    for (auto &[name, rule] : copy.properties)
    {
        rule = copyRule(rule);
    }
    return copy;
}
CommandResult inputResult(const InputError &error)
{
    auto result = CommandResult::error(error.code, error.what());
    result.value = Arguments{{"parameter", error.path}, {"expected", error.expected}, {"actual", error.actual}};
    return result;
}
bool helpRequested(const std::vector<std::string> &tokens)
{
    for (std::size_t i = 1; i < tokens.size(); ++i)
    {
        if (tokens[i] == "--")
        {
            break;
        }
        if (tokens[i] == "-h" || tokens[i] == "--help")
        {
            return true;
        }
    }
    return false;
}
} // namespace
void CommandManager::registerDefinition(CommandDefinition definition)
{
    if (m_frozen)
    {
        throw std::logic_error("Registry is frozen");
    }
    checkDefinition(definition);
    for (auto &parameter : definition.parameters)
    {
        parameter.rule = copyRule(parameter.rule);
    }
    const auto name = definition.name;
    if (!m_definitions.emplace(name, std::move(definition)).second)
    {
        throw std::invalid_argument("Duplicate command: " + name);
    }
}
void CommandManager::bindHandler(std::string_view name, Handler handler)
{
    if (m_frozen)
    {
        throw std::logic_error("Registry is frozen");
    }
    if (!m_definitions.contains(name) || !handler)
    {
        throw std::invalid_argument("Unknown command or empty handler");
    }
    if (!m_handlers.emplace(std::string(name), std::move(handler)).second)
    {
        throw std::invalid_argument("Handler already bound");
    }
}
void CommandManager::registerExecutor(Executor executor)
{
    if (m_frozen)
    {
        throw std::logic_error("Registry is frozen");
    }
    m_executor = std::move(executor);
}
void CommandManager::freeze()
{
    m_frozen = true;
}
CommandResult CommandManager::help(std::string_view target) const
{
    if (target == "help")
    {
        return {ResultKind::Help, {}, "help [模块 | 模块.命令]\nhelp -h/--help\n查询用法，不执行业务。", {}, false};
    }
    if (const auto found = m_definitions.find(target); found != m_definitions.end())
    {
        return {ResultKind::Help, {}, commandHelp(found->second, m_handlers.contains(target)), {}, false};
    }
    std::string message;
    if (target.empty())
    {
        message = "help [模块 | 模块.命令] — 查询用法\n";
    }
    for (const auto &[name, definition] : m_definitions)
    {
        if (target.empty() || definition.module == target)
        {
            message += name + " — " + definition.summary + '\n';
        }
    }
    if (message.empty())
    {
        return CommandResult::error("unknown_help_target", "未知帮助目标: " + std::string(target));
    }
    return {ResultKind::Help, {}, std::move(message), {}, false};
}
Validation CommandManager::validate(std::string_view name, const Array &positional, const Arguments &options) const
{
    if (name == "help")
    {
        if (!options.empty() || positional.size() > 1 || (!positional.empty() && !positional.front().is<std::string>()))
        {
            return {{}, CommandResult::error("invalid_help_arguments", "用法: help [模块 | 模块.命令]")};
        }
        return {{}, help(positional.empty() ? "" : positional.front().as<std::string>())};
    }
    const auto found = m_definitions.find(name);
    if (found == m_definitions.end())
    {
        return {{},
                CommandResult::error("unknown_command", "未知命令: " + std::string(name) + "\n输入 help 查看命令。")};
    }
    const auto &definition = found->second;
    Arguments normalized;
    std::set<std::string> consumed;
    std::size_t positions = 0;
    try
    {
        for (const auto &parameter : definition.parameters)
        {
            const Value *value = nullptr;
            if (parameter.binding == Binding::Option)
            {
                if (const auto item = options.find(parameter.name); item != options.end())
                {
                    value = &item->second;
                    consumed.insert(item->first);
                }
            }
            else
            {
                ++positions;
                if (parameter.position <= positional.size())
                {
                    value = &positional[parameter.position - 1];
                }
            }
            if (value)
            {
                normalized.emplace(parameter.name, validateValue(parameter.rule, *value, parameter.name));
            }
            else if (parameter.defaultValue)
            {
                normalized.emplace(parameter.name,
                                   validateValue(parameter.rule, *parameter.defaultValue, parameter.name));
            }
            else if (parameter.required)
            {
                throw InputError("missing_argument", parameter.name, describeRule(parameter.rule), "missing");
            }
        }
        for (const auto &[key, value] : options)
        {
            if (!consumed.contains(key))
            {
                throw InputError("unknown_argument", key, "declared option parameter", formatValue(value));
            }
        }
        if (positional.size() > positions)
        {
            throw InputError("extra_positional", "position " + std::to_string(positions + 1), "no further values",
                             formatValue(positional[positions]));
        }
        return {ValidatedCommand{std::string(name), std::move(normalized)}, {}};
    }
    catch (const InputError &error)
    {
        return {{}, inputResult(error)};
    }
}
Validation CommandManager::validateLine(std::string_view line) const
{
    try
    {
        return validateArgv(tokenize(line));
    }
    catch (const InputError &error)
    {
        return {{}, inputResult(error)};
    }
}
Validation CommandManager::validateArgv(const std::vector<std::string> &tokens) const
{
    if (tokens.empty())
    {
        return {{}, help()};
    }
    const auto &name = tokens.front();
    if (name != "help" && !m_definitions.contains(name))
    {
        return {{}, CommandResult::error("unknown_command", "未知命令: " + name + "\n输入 help 查看命令。")};
    }
    if (helpRequested(tokens))
    {
        return {{}, help(name)};
    }
    if (name == "help")
    {
        Array positional;
        for (std::size_t i = 1; i < tokens.size(); ++i)
        {
            positional.emplace_back(tokens[i]);
        }
        return validate(name, positional);
    }
    const auto &definition = m_definitions.at(name);
    std::map<std::string, const Parameter *, std::less<>> optionRules;
    std::map<std::size_t, const Parameter *> positionRules;
    for (const auto &parameter : definition.parameters)
    {
        if (parameter.binding == Binding::Option)
        {
            optionRules.emplace(parameter.option, &parameter);
        }
        else
        {
            positionRules.emplace(parameter.position, &parameter);
        }
    }
    Arguments options;
    Array positional;
    std::vector<std::string> remaining;
    bool terminated = false;
    try
    {
        for (std::size_t i = 1; i < tokens.size(); ++i)
        {
            const auto &token = tokens[i];
            if (!terminated && token == "--")
            {
                terminated = true;
                continue;
            }
            if (!terminated && token.starts_with("--"))
            {
                const auto equal = token.find('=');
                const auto key = token.substr(0, equal);
                const auto rule = optionRules.find(key);
                if (rule == optionRules.end())
                {
                    throw InputError("unknown_option", key, "declared option", token);
                }
                const auto &parameter = *rule->second;
                if (options.contains(parameter.name))
                {
                    throw InputError("duplicate_option", key, "one occurrence", token);
                }
                std::string value;
                if (equal != std::string::npos)
                {
                    value = token.substr(equal + 1);
                }
                else
                {
                    if (i + 1 == tokens.size() || tokens[i + 1].starts_with("--"))
                    {
                        throw InputError("missing_option_value", key, "one value (use = for --literal)", "missing");
                    }
                    value = tokens[++i];
                }
                options.emplace(parameter.name, convertToken(parameter.rule, value, key));
            }
            else
            {
                if (!terminated && token.size() > 1 && token[0] == '-' && !(token[1] >= '0' && token[1] <= '9') &&
                    token[1] != '.')
                {
                    throw InputError("unknown_option", token, "declared --option or -- before literal", token);
                }
                remaining.push_back(token);
            }
        }
        for (std::size_t i = 0; i < remaining.size(); ++i)
        {
            const auto rule = positionRules.find(i + 1);
            if (rule == positionRules.end())
            {
                throw InputError("extra_positional", "position " + std::to_string(i + 1), "no further values",
                                 remaining[i]);
            }
            positional.push_back(convertToken(rule->second->rule, remaining[i], rule->second->name));
        }
        return validate(name, positional, options);
    }
    catch (const InputError &error)
    {
        return {{}, inputResult(error)};
    }
}
CommandResult CommandManager::execute(std::string_view name, const Array &positional, const Arguments &options,
                                      const CommandContext &context)
{
    return dispatch(validate(name, positional, options), context);
}
CommandResult CommandManager::executeLine(std::string_view line, const CommandContext &context)
{
    return dispatch(validateLine(line), context);
}
CommandResult CommandManager::executeArgv(const std::vector<std::string> &tokens, const CommandContext &context)
{
    return dispatch(validateArgv(tokens), context);
}
CommandResult CommandManager::dispatch(Validation validation, const CommandContext &context)
{
    if (m_executing)
    {
        return CommandResult::error("reentrant_execution", "A handler/executor cannot recursively execute commands");
    }
    if (!validation.command)
    {
        return validation.result;
    }
    const auto &command = *validation.command;
    const auto found = m_handlers.find(command.name);
    if (found == m_handlers.end())
    {
        return CommandResult::error("handler_not_bound", "命令尚未绑定业务: " + command.name);
    }
    if (context.stop.stop_requested())
    {
        return CommandResult::error("cancelled", "Command cancelled before execution");
    }
    struct Execution
    {
        bool alive{true};
        bool called{};
        bool violated{};
        std::thread::id thread{std::this_thread::get_id()};
        CommandResult result;
    };
    auto execution = std::make_shared<Execution>();
    m_executing = true;
    struct Guard
    {
        bool &flag;
        std::shared_ptr<Execution> state;
        ~Guard()
        {
            state->alive = false;
            flag = false;
        }
    } guard{m_executing, execution};
    const Next next = [execution, handler = found->second, context, args = command.arguments]() {
        if (execution->thread != std::this_thread::get_id())
        {
            return CommandResult::error("wrong_thread", "next() must execute on the command thread");
        }
        if (!execution->alive || execution->called)
        {
            execution->violated = true;
            auto result = CommandResult::error("invalid_next", "next() is synchronous and may be called only once");
            result.committed = execution->result.committed;
            return result;
        }
        execution->called = true;
        execution->result = handler(context, args);
        return execution->result;
    };
    try
    {
        auto result = m_executor ? m_executor(context, command, next) : next();
        if (execution->violated)
        {
            result = CommandResult::error("invalid_next", "Executor invoked next() more than once");
        }
        if (!execution->called && result.ok())
        {
            result = CommandResult::error("executor_skipped", "Executor did not execute the handler");
        }
        result.committed = result.committed || execution->result.committed;
        return result;
    }
    catch (const std::exception &error)
    {
        auto result = CommandResult::error("execution_exception", error.what());
        result.committed = execution->result.committed;
        return result;
    }
    catch (...)
    {
        auto result = CommandResult::error("execution_exception", "Unknown exception");
        result.committed = execution->result.committed;
        return result;
    }
}
std::vector<std::string> CommandManager::completions() const
{
    std::set<std::string> entries{"help", "-h", "--help"};
    for (const auto &[name, definition] : m_definitions)
    {
        entries.insert(name);
        entries.insert(definition.module);
        for (const auto &parameter : definition.parameters)
        {
            if (parameter.binding == Binding::Option)
            {
                entries.insert(parameter.option);
            }
            for (const auto &choice : parameter.rule.choices)
            {
                entries.insert(choice);
            }
        }
    }
    return {entries.begin(), entries.end()};
}
} // namespace rm
