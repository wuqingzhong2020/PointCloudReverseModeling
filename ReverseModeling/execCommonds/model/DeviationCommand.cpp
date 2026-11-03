/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/DeviationCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition DeviationCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "计算点云与当前模型的偏差";
    d.usage = "model.deviation";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    d.examples = { "model.deviation", "rm.execute(\"model.deviation\")" };
    return d;
}
DeviationCommand::Args DeviationCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
