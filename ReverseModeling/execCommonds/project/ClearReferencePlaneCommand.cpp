/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/ClearReferencePlaneCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition ClearReferencePlaneCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "清除基准面并关闭点云剖视";
    d.usage = "project.clear_reference_plane";
    d.description = "";
    d.returns = "object: 工程摘要";
    d.examples = { "project.clear_reference_plane" };
    return d;
}
ClearReferencePlaneCommand::Args ClearReferencePlaneCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
