/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/ExportAndExitCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition ExportAndExitCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "导出完整模型、提交结果并退出";
    d.usage = "project.export_and_exit";
    d.description = "";
    d.returns = "object: STEP 和结果 JSON；成功保存录制后退出并停止脚本后续内容";
    d.examples = { "project.export_and_exit" };
    return d;
}
ExportAndExitCommand::Args ExportAndExitCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
