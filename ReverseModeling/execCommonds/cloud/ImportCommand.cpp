/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/ImportCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition ImportCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "导入 PCD 点云或 STEP/STP/CWRM 模型";
    d.usage = "cloud.import <path>";
    d.description = "PCD 点云固定按毫米读取，不做单位缩放。STEP/STP 按文件单位转换到毫米，CWRM 按毫米读取。";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "path";
        p.binding = rm::Binding::Positional;
        p.option = "";
        p.position = 1;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Path; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "输入文件路径（支持中文）；PCD 坐标单位为毫米";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "cloud.import \"D:/data/中文点云.pcd\"", "rm.execute(\"cloud.import\", \"D:/data/cloud.pcd\")" };
    return d;
}
ImportCommand::Args ImportCommand::arguments(const rm::Arguments &values)
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
