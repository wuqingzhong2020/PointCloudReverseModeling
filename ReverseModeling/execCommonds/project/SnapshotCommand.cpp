/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/SnapshotCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition SnapshotCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "查询当前工程摘要";
    d.usage = "project.snapshot";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    d.examples = { "project.snapshot", "snapshot = rm.execute(\"project.snapshot\")" };
    return d;
}
SnapshotCommand::Args SnapshotCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
