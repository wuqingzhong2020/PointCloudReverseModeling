/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/RepairHolesCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition RepairHolesCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "修复等厚板的贯穿孔，可选边缘小缺口";
    d.usage = "model.repair_holes --parts <array> [--boundary False] [--max-hole-area 0.0] [--notch-width 35.0] [--notch-depth 20.0] [--simplify-tolerance 0.01]";
    d.description = "";
    d.returns = "object: 原子提交；失败不改变任何部件，支持撤销和录制";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "明确的源板 ID，整批成功才提交";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "boundary";
        p.binding = rm::Binding::Option;
        p.option = "--boundary";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "同时填补边缘小缺口，可能包括过焊孔";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "max_hole_area";
        p.binding = rm::Binding::Option;
        p.option = "--max-hole-area";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.0}};
        p.description = "内部孔面积上限 mm²，0 修复全部";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "notch_width";
        p.binding = rm::Binding::Option;
        p.option = "--notch-width";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{35.0}};
        p.description = "边缘缺口最大跨度 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "notch_depth";
        p.binding = rm::Binding::Option;
        p.option = "--notch-depth";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{20.0}};
        p.description = "边缘缺口最大深度 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "simplify_tolerance";
        p.binding = rm::Binding::Option;
        p.option = "--simplify-tolerance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.01}};
        p.description = "缺口检测容差 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
RepairHolesCommand::Args RepairHolesCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("boundary"); it != values.end())
    {
        args.boundary = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: boundary"); }
    if (const auto it = values.find("max_hole_area"); it != values.end())
    {
        args.max_hole_area = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: max_hole_area"); }
    if (const auto it = values.find("notch_width"); it != values.end())
    {
        args.notch_width = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: notch_width"); }
    if (const auto it = values.find("notch_depth"); it != values.end())
    {
        args.notch_depth = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: notch_depth"); }
    if (const auto it = values.find("simplify_tolerance"); it != values.end())
    {
        args.simplify_tolerance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: simplify_tolerance"); }
    return args;
}
}
