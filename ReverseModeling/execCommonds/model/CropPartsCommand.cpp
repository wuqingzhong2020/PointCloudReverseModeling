/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/CropPartsCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition CropPartsCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按明确的工程坐标平面切除端部";
    d.usage = "model.crop_parts --cuts <array>";
    d.description = "";
    d.returns = "object: 原子提交；失败不改变任何部件，支持撤销和录制";
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
    d.examples = {  };
    return d;
}
CropPartsCommand::Args CropPartsCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("cuts"); it != values.end())
    {
        args.cuts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: cuts"); }
    return args;
}
}
