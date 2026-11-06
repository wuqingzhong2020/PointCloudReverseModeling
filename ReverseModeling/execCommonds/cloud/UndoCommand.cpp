/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/UndoCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition UndoCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "撤销工程编辑（旧入口）";
    d.usage = "cloud.undo";
    d.description = "project.undo 的旧入口；现在作用于统一工程历史，新录制写入 project.undo。";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    d.examples = {  };
    return d;
}
UndoCommand::Args UndoCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
