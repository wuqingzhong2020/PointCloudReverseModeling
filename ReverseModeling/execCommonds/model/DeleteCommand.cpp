/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/DeleteCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition DeleteCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "删除指定模型部件";
    d.usage = "model.delete --parts <[\"部件ID\",...]>";
    d.description = "";
    d.returns = "object: 删除后的工程摘要；全部删除后无模型";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "当前模型中要删除的部件 ID；同步更新实际几何和 STEP 导出，不删除点云。未知 ID 整条拒绝。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.delete --parts '[\"imported-part-1\",\"imported-part-3\"]'" };
    return d;
}
DeleteCommand::Args DeleteCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    return args;
}
}
