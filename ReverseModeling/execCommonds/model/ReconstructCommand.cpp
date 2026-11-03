/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ReconstructCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ReconstructCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按点云识别板厚重建";
    d.usage = "model.reconstruct [--mirror false] [--end-tolerance 30]";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "mirror";
        p.binding = rm::Binding::Option;
        p.option = "--mirror";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "是否补建镜像侧";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "end_tolerance";
        p.binding = rm::Binding::Option;
        p.option = "--end-tolerance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{0LL}}, rm::Value{std::int64_t{100000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{30.0}};
        p.description = "端部突出容差";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.reconstruct --mirror false --end-tolerance 30", "rm.execute(\"model.reconstruct\", mirror=False)" };
    return d;
}
ReconstructCommand::Args ReconstructCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("mirror"); it != values.end())
    {
        args.mirror = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: mirror"); }
    if (const auto it = values.find("end_tolerance"); it != values.end())
    {
        args.end_tolerance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: end_tolerance"); }
    return args;
}
}
