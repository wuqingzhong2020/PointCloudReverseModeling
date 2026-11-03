/* 文件描述：产品解释器适配；Python 对象仅存在于常驻命令线程中。 */
#pragma once
#include "commandManager/CommandManager.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace pybind11
{
class object;
}
namespace reverse
{
class PythonRuntime
{
  public:
    PythonRuntime(rm::CommandManager &manager, std::filesystem::path runtimeDirectory);
    ~PythonRuntime();
    rm::CommandResult execute(const std::string &code, const rm::CommandContext &context, bool interactive = false,
                              bool mixedCommands = false, const std::string &scriptPath = {},
                              const std::vector<std::string> &arguments = {});
    rm::CommandResult executeConsole(const std::string &line, const rm::CommandContext &context);
    rm::CommandResult executeScript(const std::string &source, const rm::CommandContext &context,
                                     const std::string &scriptPath = {}, const std::vector<std::string> &arguments = {});
    rm::CommandResult executeNative(const std::string &line, std::size_t lineNumber);
    void resetConsole();
    void setInputReader(std::function<std::optional<std::string>()> reader);
    std::optional<std::string> readInput();
    // Only direct REPL expression results use the summary; returned Python values stay unchanged.
    void rememberCommandResult(const pybind11::object &value, const rm::CommandResult &result);
    rm::CommandManager &manager();
    const rm::CommandContext &context() const;
    static PythonRuntime &current();

  private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
void linkPythonCommandBindings();
} // namespace reverse
