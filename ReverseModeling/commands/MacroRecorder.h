/* 文件描述：在命令工作线程逐条写入可回放脚本，不缓存整段录制。 */
#pragma once
#include <filesystem>
#include <fstream>
#include <map>
#include "commandManager/CommandManager.h"
#include "commands/RecordingState.h"
namespace reverse
{
class MacroRecorder
{
  public:
    rm::CommandResult start(const std::filesystem::path &path, bool overwrite);
    rm::CommandResult stop();
    void record(const rm::CommandDefinition &definition, const rm::ValidatedCommand &command);
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
