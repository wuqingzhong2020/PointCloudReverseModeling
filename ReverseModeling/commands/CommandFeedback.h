/* 文件描述：原生命令的控制台反馈；业务摘要与结构化查询结果分开显示。 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "commandManager/CommandDefinition.h"
#include "commandManager/CommandResult.h"

namespace reverse
{
struct CommandText
{
    std::string native;
    std::string python;
};
// Render the actual request, including option aliases and nested values, for copy/paste replay.
CommandText commandText(const rm::CommandDefinition &definition, const rm::Array &positional,
                        const rm::Arguments &options);
// Format system argv tokens only for display/copy; execution can use executeArgv directly.
std::string commandArgumentsText(const std::vector<std::string> &arguments);
// A typed command already has an rm> input line; automated UI submission adds it here.
std::string commandExecutionEcho(std::string_view line, bool includeInput = false);
std::string commandFeedback(const rm::CommandResult &result);
struct ProjectSnapshot;
// Shared business details for UI commands, interactive commands and script playback.
std::string commandDetails(const std::string &name, const ProjectSnapshot &project);
} // namespace reverse
