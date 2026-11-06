/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/TrimToPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition TrimToPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "截取到目标板，保留指定一侧";
    d.usage = "model.trim_to_plate --parts <array> --target <string> [--contacts []] --cuts <array> [--clearance 0.0]";
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
        p.name = "cuts";
        p.binding = rm::Binding::Option;
        p.option = "--cuts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"part", [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()}, {"origin", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"normal", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"keep", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"positive", "negative"}; return r; }()}}; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "逐源工程坐标截断面和显式保留正/负侧；截取到板以所选目标面重新校验平面";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "clearance";
        p.binding = rm::Binding::Option;
        p.option = "--clearance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{1000000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.0}};
        p.description = "目标面外的装配间隙 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
TrimToPlateCommand::Args TrimToPlateCommand::arguments(const rm::Arguments &values)
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
    if (const auto it = values.find("cuts"); it != values.end())
    {
        args.cuts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: cuts"); }
    if (const auto it = values.find("clearance"); it != values.end())
    {
        args.clearance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: clearance"); }
    return args;
}
}
