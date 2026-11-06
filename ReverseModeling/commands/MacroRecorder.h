/* 文件描述：在命令工作线程逐条写入可回放脚本，不缓存整段录制。 */
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include "commandManager/CommandManager.h"
#include "commands/RecordingState.h"
namespace reverse
{
// 规范化原生命令行，同时供逐条录制与会话补写使用。
std::string recordingLine(const rm::CommandDefinition &definition, const rm::ValidatedCommand &command);
class MacroRecorder
{
  public:
    static constexpr std::uint64_t maxScriptBytes = 64 * 1024 * 1024;
    static constexpr std::string_view header = "# ReverseModeler 命令脚本（UTF-8）\n"
        "# 每行一条命令，与命令窗口相同；失败停止，前序提交不自动回滚。\n\n";
    rm::CommandResult start(const std::filesystem::path &path, bool overwrite);
    rm::CommandResult stop();
    void record(const rm::CommandDefinition &definition, const rm::ValidatedCommand &command);
    void recordLine(std::string_view line);
    void fail(std::string message);
    bool recording() const
    {
        return m_state.active;
    }
    const RecordingState &state() const
    {
        return m_state;
    }

  private:
    std::ofstream m_file;
    std::filesystem::path m_path;
    RecordingState m_state;
};
} // namespace reverse
