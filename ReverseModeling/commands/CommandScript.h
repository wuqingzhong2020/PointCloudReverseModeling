/* 文件描述：命令文本脚本逐行使用同一参数校验和分发入口，实时回显并在失败时停止。 */
#pragma once

#include <string_view>

#include "commandManager/CommandManager.h"

namespace reverse
{
bool isNativeCommandLine(const rm::CommandManager &manager, std::string_view line);
bool isNativeCommandScript(const rm::CommandManager &manager, std::string_view source);
rm::CommandResult executeCommandScript(rm::CommandManager &manager, std::string_view source,
                                       const rm::CommandContext &context);
} // namespace reverse
