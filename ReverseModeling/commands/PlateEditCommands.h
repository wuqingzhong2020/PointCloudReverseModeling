/* 文件描述：批量板件命令的参数编解码；与几何算法及生成描述解耦。 */
#pragma once
#include "commands/CommandBindings.h"
#include "core/PlateEditing.h"
namespace reverse
{
std::string plateEditCommand(const std::string &operation);
std::string plateEditTitle(const std::string &operation);
PlateEdit plateEditRequest(const std::string &command, const rm::Arguments &args);
rm::Arguments plateEditArguments(const PlateEdit &request);
} // namespace reverse
