/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/ResetCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition ResetCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "恢复最近一次导入状态";
    d.usage = "project.reset";
    d.description = "恢复已加载的完整点云或 STEP/CWRM 模型，可撤销此次重置；不重读源文件，不停止录制，不回滚导出文件。";
    d.returns = "object: 基线工程摘要；没有导入基线返回错误";
    d.examples = { "project.reset" };
    return d;
}
ResetCommand::Args ResetCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
