/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/RotateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition RotateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按指定轴旋转模型部件";
    d.usage = "model.rotate --parts <[\"部件ID\",...]> --axis x|y|z --angle <度> --center <[X,Y,Z]>";
    d.description = "";
    d.returns = "object: 更新后的工程摘要；可撤销和录制";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "当前模型中部件 ID；多个部件一起变换，未知 ID 整条拒绝";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "axis";
        p.binding = rm::Binding::Option;
        p.option = "--axis";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"x", "y", "z"}; return r; }();
        p.required = true;
        p.description = "项目坐标旋转轴；一次仅绕一个轴，遵循右手规则";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "angle";
        p.binding = rm::Binding::Option;
        p.option = "--angle";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-180LL}}, rm::Value{std::int64_t{180LL}}}}; return r; }();
        p.required = true;
        p.description = "有符号角度；从轴正向朝原点看，正值逆时针";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "center";
        p.binding = rm::Binding::Option;
        p.option = "--center";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }();
        p.required = true;
        p.description = "公共旋转中心，项目坐标 mm；必须明确填写以便重放";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.rotate --parts '[\"imported-part-1\"]' --axis z --angle -60 --center '[0,0,0]'" };
    return d;
}
RotateCommand::Args RotateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("axis"); it != values.end())
    {
        if (it->second.as<std::string>() == "x") { args.axis = AxisValue::Value0; }
        else if (it->second.as<std::string>() == "y") { args.axis = AxisValue::Value1; }
        else if (it->second.as<std::string>() == "z") { args.axis = AxisValue::Value2; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: axis"); }
    if (const auto it = values.find("angle"); it != values.end())
    {
        args.angle = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: angle"); }
    if (const auto it = values.find("center"); it != values.end())
    {
        args.center = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: center"); }
    return args;
}
}
