/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/SectionCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition SectionCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "设置基准面点云剖视";
    d.usage = "project.section --enabled true|false [--offset <mm>] [--reverse true|false]";
    d.description = "仅过滤点云显示，不删除点云，不影响框选、重建或导出。偏移沿原法线；默认保留正法线侧，reverse 仅反转保留侧。";
    d.returns = "object: 包含剖视状态的工程摘要";
    {
        rm::Parameter p;
        p.name = "enabled";
        p.binding = rm::Binding::Option;
        p.option = "--enabled";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = true;
        p.description = "";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "offset";
        p.binding = rm::Binding::Option;
        p.option = "--offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.0}};
        p.description = "沿基准面法线的偏移，单位 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "reverse";
        p.binding = rm::Binding::Option;
        p.option = "--reverse";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "反转点云保留侧";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "project.section --enabled true --offset 10 --reverse false", "project.section --enabled false" };
    return d;
}
SectionCommand::Args SectionCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("enabled"); it != values.end())
    {
        args.enabled = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: enabled"); }
    if (const auto it = values.find("offset"); it != values.end())
    {
        args.offset = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: offset"); }
    if (const auto it = values.find("reverse"); it != values.end())
    {
        args.reverse = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: reverse"); }
    return args;
}
}
