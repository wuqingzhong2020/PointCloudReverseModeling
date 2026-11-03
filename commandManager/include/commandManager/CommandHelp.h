/* 文件描述：共享运行时帮助与命令行词法规则。 */
#pragma once
#include "commandManager/CommandDefinition.h"
namespace rm
{
std::vector<std::string> tokenize(std::string_view text);
std::string commandHelp(const CommandDefinition &definition, bool bound);
} // namespace rm
