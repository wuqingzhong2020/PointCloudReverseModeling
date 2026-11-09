/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/SetAngleSteelCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition SetAngleSteelCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "将矩形实体设为角钢";
    d.usage = "model.set_angle_steel --part <ID> --face <编号> [--thickness 10] [--auto-height true] [--height 100]";
    d.description = "";
    d.returns = "object: 替换选中部件，保留其他部件；支持撤销和录制";
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
        p.name = "face";
        p.binding = rm::Binding::Option;
        p.option = "--face";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2147483647LL}}}}; return r; }();
        p.required = true;
        p.description = "矩形实体的竖直开口侧面编号，BRep 1-based";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "thickness";
        p.binding = rm::Binding::Option;
        p.option = "--thickness";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{50LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{10.0}};
        p.description = "";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "auto_height";
        p.binding = rm::Binding::Option;
        p.option = "--auto-height";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "参考下方 50 mm 内覆盖底边的支撑面；无支撑时使用原实体高度";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "height";
        p.binding = rm::Binding::Option;
        p.option = "--height";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{1000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{100.0}};
        p.description = "关闭自动高度后生效，底部固定，沿真实底面内法线量取高度";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.set_angle_steel --part imported-part-1 --face 3 --thickness 10 --auto-height false --height 160" };
    return d;
}
SetAngleSteelCommand::Args SetAngleSteelCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: part"); }
    if (const auto it = values.find("face"); it != values.end())
    {
        args.face = it->second.as<std::int64_t>();
    }
    else { throw std::invalid_argument("Missing normalized argument: face"); }
    if (const auto it = values.find("thickness"); it != values.end())
    {
        args.thickness = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: thickness"); }
    if (const auto it = values.find("auto_height"); it != values.end())
    {
        args.auto_height = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: auto_height"); }
    if (const auto it = values.find("height"); it != values.end())
    {
        args.height = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: height"); }
    return args;
}
}
