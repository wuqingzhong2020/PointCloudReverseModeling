/* 文件描述：独立命令注册、校验、帮助、业务绑定和可选统一执行包装。 */
#pragma once
#include "commandManager/CommandDefinition.h"
#include "commandManager/CommandResult.h"

namespace rm
{
struct ValidatedCommand
{
    std::string name;
    Arguments arguments;
};
struct Validation
{
    std::optional<ValidatedCommand> command;
    CommandResult result;
};
class CommandManager
{
  public:
    using Handler = std::function<CommandResult(const CommandContext &, const Arguments &)>;
    using Next = std::function<CommandResult()>;
    using Executor = std::function<CommandResult(const CommandContext &, const ValidatedCommand &, const Next &)>;
    void registerDefinition(CommandDefinition definition);
    void bindHandler(std::string_view name, Handler handler);
    template <class Command>
    void bindHandler(std::function<CommandResult(const CommandContext &, const typename Command::Args &)> handler)
    {
        bindHandler(Command::name(), [handler = std::move(handler)](const auto &context, const auto &args) {
            return handler(context, Command::arguments(args));
        });
    }
    void registerExecutor(Executor executor);
    void freeze();
    Validation validate(std::string_view name, const Array &positional = {}, const Arguments &options = {}) const;
    Validation validateLine(std::string_view line) const;
    Validation validateArgv(const std::vector<std::string> &tokens) const;
    CommandResult execute(std::string_view name, const Array &positional = {}, const Arguments &options = {},
                          const CommandContext &context = {});
    CommandResult executeLine(std::string_view line, const CommandContext &context = {});
    CommandResult executeArgv(const std::vector<std::string> &tokens, const CommandContext &context = {});
    CommandResult help(std::string_view target = {}) const;
    const std::map<std::string, CommandDefinition, std::less<>> &definitions() const
    {
        return m_definitions;
    }
    std::vector<std::string> completions() const;

  private:
    CommandResult dispatch(Validation validation, const CommandContext &context);
    std::map<std::string, CommandDefinition, std::less<>> m_definitions;
    std::map<std::string, Handler, std::less<>> m_handlers;
    Executor m_executor;
    bool m_frozen{};
    bool m_executing{};
};
} // namespace rm
