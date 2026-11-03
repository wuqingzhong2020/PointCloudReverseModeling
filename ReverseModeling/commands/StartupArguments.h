/* 文件描述：桌面程序启动参数的中立解析结果；命令与脚本参数保持原始边界。 */
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace reverse
{
enum class StartupMode { None, Command, Script, Help, Error };
struct StartupRequest
{
    StartupMode mode{StartupMode::None};
    std::vector<std::string> arguments;
    std::string error;
    std::optional<std::filesystem::path> recordFile;
    std::optional<std::filesystem::path> inputFile;
    std::optional<std::filesystem::path> outputDirectory;
};
// Extract registered --key/value options first; pass the remaining -c/-f tail through unchanged.
StartupRequest parseStartupArguments(const std::vector<std::string> &arguments);
std::string startupUsage();
} // namespace reverse
