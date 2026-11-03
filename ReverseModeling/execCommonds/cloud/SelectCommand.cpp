/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/SelectCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition SelectCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "按视图矩形贯穿深度选择点云";
    d.usage = "cloud.select --rectangle <object> [--mode replace|add|subtract|clear]";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "rectangle";
        p.binding = rm::Binding::Option;
        p.option = "--rectangle";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"viewProjection", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 16; r.maxSize = 16; return r; }()}, {"minX", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"minY", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"maxX", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"maxY", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"zeroToOneDepth", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}}; return r; }();
        p.required = true;
        p.description = "视图投影矩阵和 NDC 矩形，供视图适配器使用";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "mode";
        p.binding = rm::Binding::Option;
        p.option = "--mode";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"replace", "add", "subtract", "clear"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"replace"};
        p.description = "";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
SelectCommand::Args SelectCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("rectangle"); it != values.end())
    {
        args.rectangle = it->second.as<rm::Arguments>();
    }
    else { throw std::invalid_argument("Missing normalized argument: rectangle"); }
    if (const auto it = values.find("mode"); it != values.end())
    {
        if (it->second.as<std::string>() == "replace") { args.mode = ModeValue::Value0; }
        else if (it->second.as<std::string>() == "add") { args.mode = ModeValue::Value1; }
        else if (it->second.as<std::string>() == "subtract") { args.mode = ModeValue::Value2; }
        else if (it->second.as<std::string>() == "clear") { args.mode = ModeValue::Value3; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: mode"); }
    return args;
}
}
