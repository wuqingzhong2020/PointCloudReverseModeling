/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/ResetCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition ResetCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "恢复导入时的点云";
    d.usage = "cloud.reset";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    d.examples = {  };
    return d;
}
ResetCommand::Args ResetCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    return args;
}
}
