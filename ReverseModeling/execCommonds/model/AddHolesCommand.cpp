/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/AddHolesCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition AddHolesCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "逐板指定底边，批量添加过焊孔";
    d.usage = "model.add_holes --targets <array>";
    d.description = "";
    d.returns = "object: 原子提交；失败不改变任何部件，支持撤销和录制";
    {
        rm::Parameter p;
        p.name = "targets";
        p.binding = rm::Binding::Option;
        p.option = "--targets";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"part", [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()}, {"edge", [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{1000000LL}}}}; return r; }()}, {"kind", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"weld"}; return r; }()}, {"shape", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"circle", "triangle"}; return r; }()}, {"size", [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.001}}, rm::Value{double{1000000.0}}}}; return r; }()}, {"first", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"second", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}}; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "各板独立底边和端点开关；只修改明确部件";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
AddHolesCommand::Args AddHolesCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("targets"); it != values.end())
    {
        args.targets = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: targets"); }
    return args;
}
}
