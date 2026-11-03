/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/SetOutputDirCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition SetOutputDirCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "设置本次任务输出目录";
    d.usage = "project.set_output_dir <path>";
    d.description = "";
    d.returns = "object: 输出目录；初始化 result.json 为失败状态";
    {
        rm::Parameter p;
        p.name = "path";
        p.binding = rm::Binding::Positional;
        p.option = "";
        p.position = 1;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Path; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "交付目录；同目录重复设置不重置结果，不允许切换到其他目录";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "project.set_output_dir \"D:/任务/001\"" };
    return d;
}
SetOutputDirCommand::Args SetOutputDirCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("path"); it != values.end())
    {
        args.path = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: path"); }
    return args;
}
}
