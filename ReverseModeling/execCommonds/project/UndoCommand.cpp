/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/UndoCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition UndoCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "撤销上一步工程编辑";
    d.usage = "project.undo";
    d.description = "统一撤销点云编辑、模型重建/删除和已确认基准面，最多 10 步；新导入建立历史边界。";
    d.returns = "object: 恢复后的工程摘要；无历史返回错误";
    d.examples = { "project.undo" };
    return d;
}
UndoCommand::Args UndoCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
