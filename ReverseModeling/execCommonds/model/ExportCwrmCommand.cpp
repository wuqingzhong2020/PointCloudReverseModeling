/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ExportCwrmCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ExportCwrmCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "导出当前模型为 CWRM v2";
    d.usage = "model.export_cwrm <path>";
    d.description = "保存当前几何，不包含原始点云、重建参数或编辑历史；不覆盖已有文件。";
    d.returns = "object: 导出结果摘要";
    {
        rm::Parameter p;
        p.name = "path";
        p.binding = rm::Binding::Positional;
        p.option = "";
        p.position = 1;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Path; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
ExportCwrmCommand::Args ExportCwrmCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("path"); it != values.end())
    {
        args.path = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: path"); }
    return args;
}
}
