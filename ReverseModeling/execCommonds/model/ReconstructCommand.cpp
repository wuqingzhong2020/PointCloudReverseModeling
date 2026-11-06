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
    d.usage = "model.reconstruct [--mirror false] [--end-tolerance 30] [--web-holes true] [--min-hole-radius 5]";
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
    {
        rm::Parameter p;
        p.name = "web_holes";
        p.binding = rm::Binding::Option;
        p.option = "--web-holes";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "保留俯视可见的腹板圆孔和长圆孔；关闭时生成无内孔腹板，不影响筋板可见顶边缺口。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "min_hole_radius";
        p.binding = rm::Binding::Option;
        p.option = "--min-hole-radius";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{0LL}}, rm::Value{std::int64_t{1000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{5.0}};
        p.description = "忽略小于此半径的腹板孔。长圆孔按端圆半径（孔宽一半）过滤；0 仍受点云分辨率限制。";
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
    if (const auto it = values.find("web_holes"); it != values.end())
    {
        args.web_holes = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: web_holes"); }
    if (const auto it = values.find("min_hole_radius"); it != values.end())
    {
        args.min_hole_radius = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: min_hole_radius"); }
    return args;
}
}
