/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/RedoCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition RedoCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "重做已撤销的工程编辑";
    d.usage = "project.redo";
    d.description = "恢复保存的几何结果；新工程编辑截断重做分支。";
    d.returns = "object: 恢复后的工程摘要；无历史返回错误";
    d.examples = { "project.redo" };
    return d;
}
RedoCommand::Args RedoCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
