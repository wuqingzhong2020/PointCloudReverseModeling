/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/SelectIndicesCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition SelectIndicesCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "按原始点索引选择点云";
    d.usage = "cloud.select_indices --indices <array> [--mode replace|add|subtract|clear]";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "indices";
        p.binding = rm::Binding::Option;
        p.option = "--indices";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Unsigned; return r; }()); r.maxSize = 10000000; return r; }();
        p.required = true;
        p.description = "当前点云中的零起始索引";
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
    d.examples = { "rm.execute(\"cloud.select_indices\", indices=[0, 1, 2])" };
    return d;
}
SelectIndicesCommand::Args SelectIndicesCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("indices"); it != values.end())
    {
        args.indices = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: indices"); }
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
