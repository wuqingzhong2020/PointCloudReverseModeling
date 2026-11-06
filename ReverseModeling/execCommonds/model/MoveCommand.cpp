/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/MoveCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition MoveCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "移动模型部件";
    d.usage = "model.move --parts <[\"部件ID\",...]> --offset <[X,Y,Z]>";
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
        p.name = "offset";
        p.binding = rm::Binding::Option;
        p.option = "--offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }();
        p.required = true;
        p.description = "项目坐标 X/Y/Z 平移量，单位 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.move --parts '[\"imported-part-1\"]' --offset '[-100,0,0]'" };
    return d;
}
MoveCommand::Args MoveCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("offset"); it != values.end())
    {
        args.offset = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: offset"); }
    return args;
}
}
