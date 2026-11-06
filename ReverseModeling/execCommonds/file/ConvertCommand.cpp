/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "file/ConvertCommand.h"
namespace rmcmd::file
{
rm::CommandDefinition ConvertCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "file";
    d.summary = "转换 STEP/CWRM 模型格式或采样为 PCD";
    d.usage = "file.convert <input.step|input.cwrm> <output.step|output.cwrm|output.pcd> [--spacing 2]";
    d.description = "输入仅支持 STEP/STP、CWRM。禁止以 PCD 为输入（包括 PCD 转 PCD），点云正常导入、导出不受影响。输出 PCD 为模型表面采样，并非原始扫描数据；曲面受三角网格离散精度限制。转换不修改当前工程，不覆盖已有文件。";
    d.returns = "object: 当前工程摘要，转换结果写入日志";
    {
        rm::Parameter p;
        p.name = "input";
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
        p.name = "output";
        p.binding = rm::Binding::Positional;
        p.option = "";
        p.position = 2;
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
    d.examples = { "file.convert \"D:/data/model.step\" \"D:/data/model.cwrm\"", "file.convert \"D:/data/model.cwrm\" \"D:/data/surface.pcd\" --spacing 2" };
    return d;
}
ConvertCommand::Args ConvertCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("input"); it != values.end())
    {
        args.input = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: input"); }
    if (const auto it = values.find("output"); it != values.end())
    {
        args.output = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: output"); }
    if (const auto it = values.find("spacing"); it != values.end())
    {
        args.spacing = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: spacing"); }
    return args;
}
}
