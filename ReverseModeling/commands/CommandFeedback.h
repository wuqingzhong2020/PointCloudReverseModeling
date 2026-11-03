/* 文件描述：原生命令的控制台反馈；业务摘要与结构化查询结果分开显示。 */
#pragma once

#include <string>

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
std::string commandFeedback(const rm::CommandResult &result);
} // namespace reverse
