/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/OptimizeCrossPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition OptimizeCrossPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "交叉板优化：保持 A 连续，打断 B 并可选调整高度差";
    d.usage = "model.optimize_cross_plate --continuous <A> --broken <B> [--height-offset 10]";
    d.description = "";
    d.returns = "object: 原子提交交叉板结果；支持预览、撤销重做和录制回放";
    {
        rm::Parameter p;
        p.name = "continuous";
        p.binding = rm::Binding::Option;
        p.option = "--continuous";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "连续板 A 的部件 ID";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "broken";
        p.binding = rm::Binding::Option;
        p.option = "--broken";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "打断板 B 的部件 ID，必须与 A 不同";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "height_offset";
        p.binding = rm::Binding::Option;
        p.option = "--height-offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.0}}, rm::Value{double{10.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{10.0}};
        p.description = "A 顶面高于 B 的距离；0 关闭高度修改";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
OptimizeCrossPlateCommand::Args OptimizeCrossPlateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("continuous"); it != values.end())
    {
        args.continuous = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: continuous"); }
    if (const auto it = values.find("broken"); it != values.end())
    {
        args.broken = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: broken"); }
    if (const auto it = values.find("height_offset"); it != values.end())
    {
        args.height_offset = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: height_offset"); }
    return args;
}
}
