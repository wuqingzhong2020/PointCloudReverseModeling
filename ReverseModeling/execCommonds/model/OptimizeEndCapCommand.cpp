/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/OptimizeEndCapCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition OptimizeEndCapCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "封板优化：独立调整边缘 1 或 2 的错边、凸边及相邻连接";
    d.usage = "model.optimize_end_cap --part <ID> --edge 1 --joint misalign [--auto-distance true] [--distance 15]";
    d.description = "";
    d.returns = "object: 当前封板边缘、对应翼板与腹板原子提交，可撤销及录制回放";
    {
        rm::Parameter p;
        p.name = "part";
        p.binding = rm::Binding::Option;
        p.option = "--part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "edge";
        p.binding = rm::Binding::Option;
        p.option = "--edge";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{std::int64_t{1LL}};
        p.description = "当前边缘；编号由梁的固定横向排序，与相机无关";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "joint";
        p.binding = rm::Binding::Option;
        p.option = "--joint";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"misalign", "convex"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"misalign"};
        p.description = "错边向翼板外表面内缩，凸边向外伸出";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "auto_distance";
        p.binding = rm::Binding::Option;
        p.option = "--auto-distance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "仅错边生效：取当前侧翼板厚度的 75%";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "distance";
        p.binding = rm::Binding::Option;
        p.option = "--distance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{200LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{15.0}};
        p.description = "自定义距离；错边最大 100，凸边最大 200";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
OptimizeEndCapCommand::Args OptimizeEndCapCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: part"); }
    if (const auto it = values.find("edge"); it != values.end())
    {
        args.edge = it->second.as<std::int64_t>();
    }
    else { throw std::invalid_argument("Missing normalized argument: edge"); }
    if (const auto it = values.find("joint"); it != values.end())
    {
        if (it->second.as<std::string>() == "misalign") { args.joint = JointValue::Value0; }
        else if (it->second.as<std::string>() == "convex") { args.joint = JointValue::Value1; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: joint"); }
    if (const auto it = values.find("auto_distance"); it != values.end())
    {
        args.auto_distance = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: auto_distance"); }
    if (const auto it = values.find("distance"); it != values.end())
    {
        args.distance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: distance"); }
    return args;
}
}
