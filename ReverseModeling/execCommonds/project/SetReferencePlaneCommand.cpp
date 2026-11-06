/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "project/SetReferencePlaneCommand.h"
namespace rmcmd::project
{
rm::CommandDefinition SetReferencePlaneCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "project";
    d.summary = "用三个工程坐标点设置基准面";
    d.usage = "project.set_reference_plane --points <[[x,y,z],[x,y,z],[x,y,z]]>";
    d.description = "三个非共线点，单位 mm；点序决定法线方向。用于正视和点云剖视，不变换原始坐标或重建几何。重新定面会关闭剖视。";
    d.returns = "object: 包含基准面的工程摘要";
    {
        rm::Parameter p;
        p.name = "points";
        p.binding = rm::Binding::Option;
        p.option = "--points";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }();
        p.required = true;
        p.description = "三个工程坐标 XYZ（mm），不是显示局部坐标";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "project.set_reference_plane --points \"[[0,0,0],[100,0,0],[0,100,0]]\"" };
    return d;
}
SetReferencePlaneCommand::Args SetReferencePlaneCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("points"); it != values.end())
    {
        args.points = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: points"); }
    return args;
}
}
