/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/MergeToPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition MergeToPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "以目标厚度合并板件";
    d.usage = "model.merge_to_plate --parts <array> --target <string> [--normal-angle 1.0] [--inplane-angle 1.0] [--max-gap 5.0]";
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
        p.name = "target";
        p.binding = rm::Binding::Option;
        p.option = "--target";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "参照目标板 ID；合并时保留此 ID";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "normal_angle";
        p.binding = rm::Binding::Option;
        p.option = "--normal-angle";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{15.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{1.0}};
        p.description = "法向校正最大角度";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "inplane_angle";
        p.binding = rm::Binding::Option;
        p.option = "--inplane-angle";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{15.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{1.0}};
        p.description = "面内校正最大角度";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "max_gap";
        p.binding = rm::Binding::Option;
        p.option = "--max-gap";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{5.0}};
        p.description = "最大桥接间隙 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
MergeToPlateCommand::Args MergeToPlateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("target"); it != values.end())
    {
        args.target = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: target"); }
    if (const auto it = values.find("normal_angle"); it != values.end())
    {
        args.normal_angle = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: normal_angle"); }
    if (const auto it = values.find("inplane_angle"); it != values.end())
    {
        args.inplane_angle = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: inplane_angle"); }
    if (const auto it = values.find("max_gap"); it != values.end())
    {
        args.max_gap = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: max_gap"); }
    return args;
}
}
