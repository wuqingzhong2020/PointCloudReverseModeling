/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ExtendToPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ExtendToPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "延长到目标板";
    d.usage = "model.extend_to_plate --parts <array> --target <string> [--contacts []] [--tolerance 0.15] [--max-distance 50.0]";
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
        p.name = "contacts";
        p.binding = rm::Binding::Option;
        p.option = "--contacts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"part", [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()}, {"face", [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{0LL}}, rm::Value{std::int64_t{1000000LL}}}}; return r; }()}}; return r; }()); return r; }();
        p.required = false;
        p.defaultValue = rm::Value{rm::Array{}};
        p.description = "逐源目标面编号，从1开始；0 自动判断，歧义时报错";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "tolerance";
        p.binding = rm::Binding::Option;
        p.option = "--tolerance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.15}};
        p.description = "接触距离容差 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "max_distance";
        p.binding = rm::Binding::Option;
        p.option = "--max-distance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{50.0}};
        p.description = "最长允许延长距离 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
ExtendToPlateCommand::Args ExtendToPlateCommand::arguments(const rm::Arguments &values)
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
    if (const auto it = values.find("contacts"); it != values.end())
    {
        args.contacts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: contacts"); }
    if (const auto it = values.find("tolerance"); it != values.end())
    {
        args.tolerance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: tolerance"); }
    if (const auto it = values.find("max_distance"); it != values.end())
    {
        args.max_distance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: max_distance"); }
    return args;
}
}
