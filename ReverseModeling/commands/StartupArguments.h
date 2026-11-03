/* 文件描述：桌面程序启动参数的中立解析结果；命令与脚本参数保持原始边界。 */
#pragma once

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
};
// arguments excludes the executable name. After -c/-f every token belongs to that request.
StartupRequest parseStartupArguments(const std::vector<std::string> &arguments);
std::string startupUsage();
} // namespace reverse
