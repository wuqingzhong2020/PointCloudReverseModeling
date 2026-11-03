/* 文件描述：命令成功、帮助与输入错误的结构化结果，不以异常文本冒充成功。 */
#pragma once
#include "commandManager/CommandValue.h"
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>

namespace rm
{
enum class ResultKind
{
    Success,
    Help,
    Error
};
struct CommandResult
{
    ResultKind kind{ResultKind::Success};
    std::string code;
    std::string message;
    Value value;
    bool committed{};
    bool ok() const
    {
        return kind != ResultKind::Error;
    }
    static CommandResult error(std::string code, std::string message);
};
class InputError : public std::runtime_error
{
  public:
    InputError(std::string code, std::string path, std::string expected, std::string actual);
    std::string code;
    std::string path;
    std::string expected;
    std::string actual;
};
struct CommandContext
{
    std::stop_token stop;
    std::optional<std::uint64_t> expectedRevision;
    std::function<void(double, std::string_view)> progress;
    std::function<void(std::string_view)> output;
    // Diagnostic details belong in the host log, separate from interactive stdout.
    std::function<void(std::string_view)> diagnostic;
};
} // namespace rm
