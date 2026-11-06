/* 文件描述：保存当前会话的复现过程，与十步几何历史和录制文件分别管理。 */
#pragma once
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace reverse
{
enum class ReplayPolicy { Import, OutputDirectory, Include, Exclude, Unspecified };
[[nodiscard]] ReplayPolicy replayPolicy(const std::string &name);
[[nodiscard]] std::string canonicalHistoryCommand(const std::string &name);

class ReplayJournal
{
  public:
    void append(const std::string &name, std::string line);
    void invalidate(std::string error);
    [[nodiscard]] std::vector<std::string> prefix() const;
    [[nodiscard]] const std::string &error() const;

  private:
    std::vector<std::string> m_lines;
    std::optional<std::string> m_output;
    std::uint64_t m_bytes{};
    std::string m_error;
};
}
