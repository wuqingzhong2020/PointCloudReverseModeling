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
    d.usage = "model.reconstruct [--type hbeam|tbar|other] [--mirror false] [--end-tolerance 30] [--web-holes true] [--min-hole-radius 5]";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "type";
        p.binding = rm::Binding::Option;
        p.option = "--type";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"hbeam", "tbar", "other"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"hbeam"};
        p.description = "重建工件类型；other 为俯视底板焊件，按直线/圆弧顶边垂直投影恢复连接，保留独立障碍物";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "on_ground";
        p.binding = rm::Binding::Option;
        p.option = "--on-ground";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "T 排置于地面；关闭表示台架";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "regular_upper_surface";
        p.binding = rm::Binding::Option;
        p.option = "--regular-upper-surface";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "T 排规则顶边；异形直 T 关闭";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "projection_onto_border";
        p.binding = rm::Binding::Option;
        p.option = "--projection-onto-border";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "T 排端边投影到底板边界";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "perpendicular";
        p.binding = rm::Binding::Option;
        p.option = "--perpendicular";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "T 排立板与底板垂直约束；斜 T 关闭";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "max_slot_width";
        p.binding = rm::Binding::Option;
        p.option = "--max-slot-width";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{0LL}}, rm::Value{std::int64_t{1000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{50.0}};
        p.description = "T 排顶边无支持断带的最大连接宽度";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "max_base_width";
        p.binding = rm::Binding::Option;
        p.option = "--max-base-width";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{20LL}}, rm::Value{std::int64_t{1000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{250.0}};
        p.description = "T 排底板识别宽度上限";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
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
    if (const auto it = values.find("type"); it != values.end())
    {
        if (it->second.as<std::string>() == "hbeam") { args.type = TypeValue::Value0; }
        else if (it->second.as<std::string>() == "tbar") { args.type = TypeValue::Value1; }
        else if (it->second.as<std::string>() == "other") { args.type = TypeValue::Value2; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: type"); }
    if (const auto it = values.find("on_ground"); it != values.end())
    {
        args.on_ground = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: on_ground"); }
    if (const auto it = values.find("regular_upper_surface"); it != values.end())
    {
        args.regular_upper_surface = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: regular_upper_surface"); }
    if (const auto it = values.find("projection_onto_border"); it != values.end())
    {
        args.projection_onto_border = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: projection_onto_border"); }
    if (const auto it = values.find("perpendicular"); it != values.end())
    {
        args.perpendicular = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: perpendicular"); }
    if (const auto it = values.find("max_slot_width"); it != values.end())
    {
        args.max_slot_width = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: max_slot_width"); }
    if (const auto it = values.find("max_base_width"); it != values.end())
    {
        args.max_base_width = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: max_base_width"); }
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
