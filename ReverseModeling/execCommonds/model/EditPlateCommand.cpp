/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/EditPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition EditPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按右键菜单的固定规则直接编辑板件";
    d.usage = "model.edit_plate --operation extend|trim|merge|split|crop|repair --parts <array> [--target <id>] [--cuts <array>] [--all false]";
    d.description = "";
    d.returns = "object: 实际几何、统一撤销重做与录制回放";
    {
        rm::Parameter p;
        p.name = "operation";
        p.binding = rm::Binding::Option;
        p.option = "--operation";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"extend", "trim", "merge", "split", "crop", "repair"}; return r; }();
        p.required = true;
        p.description = "与菜单一致的板件操作";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "单个源板；修复全部时记录实际范围";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "target";
        p.binding = rm::Binding::Option;
        p.option = "--target";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{""};
        p.description = "目标板 ID";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "cuts";
        p.binding = rm::Binding::Option;
        p.option = "--cuts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"part", [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()}, {"origin", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"normal", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"keep", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"positive", "negative"}; return r; }()}}; return r; }()); return r; }();
        p.required = false;
        p.defaultValue = rm::Value{rm::Array{}};
        p.description = "框选操作解析后的工程坐标截断面";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "all";
        p.binding = rm::Binding::Option;
        p.option = "--all";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "修复全部时跳过不支持或无需修复的板件";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
EditPlateCommand::Args EditPlateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("operation"); it != values.end())
    {
        if (it->second.as<std::string>() == "extend") { args.operation = OperationValue::Value0; }
        else if (it->second.as<std::string>() == "trim") { args.operation = OperationValue::Value1; }
        else if (it->second.as<std::string>() == "merge") { args.operation = OperationValue::Value2; }
        else if (it->second.as<std::string>() == "split") { args.operation = OperationValue::Value3; }
        else if (it->second.as<std::string>() == "crop") { args.operation = OperationValue::Value4; }
        else if (it->second.as<std::string>() == "repair") { args.operation = OperationValue::Value5; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: operation"); }
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
    if (const auto it = values.find("cuts"); it != values.end())
    {
        args.cuts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: cuts"); }
    if (const auto it = values.find("all"); it != values.end())
    {
        args.all = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: all"); }
    return args;
}
}
