/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ExportPcdCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ExportPcdCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "将当前模型表面采样导出为 PCD";
    d.usage = "model.export_pcd <path> [--spacing 2]";
    d.description = "采样模型三角网格表面，曲面受网格离散精度限制；并非原始扫描点。已有点云请用 cloud.export，不覆盖已有文件。";
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
    {
        rm::Parameter p;
        p.name = "spacing";
        p.binding = rm::Binding::Option;
        p.option = "--spacing";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.01}}, rm::Value{double{100000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{2.0}};
        p.description = "模型表面面积采样间距，越小点越密；最多五百万点。";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
ExportPcdCommand::Args ExportPcdCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("path"); it != values.end())
    {
        args.path = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: path"); }
    if (const auto it = values.find("spacing"); it != values.end())
    {
        args.spacing = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: spacing"); }
    return args;
}
}
