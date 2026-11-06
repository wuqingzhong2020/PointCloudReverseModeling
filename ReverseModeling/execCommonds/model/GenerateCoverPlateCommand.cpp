/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/GenerateCoverPlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition GenerateCoverPlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "生成外扩贴板，保留源板并新增独立部件";
    d.usage = "model.generate_cover_plate --part <部件ID> --face <主面编号> [--reverse false] [--offset 30 --thickness 10]";
    d.description = "";
    d.returns = "object: 含新增部件 ID 的工程摘要；支持撤销、重做、录制和 STEP 导出";
    {
        rm::Parameter p;
        p.name = "part";
        p.binding = rm::Binding::Option;
        p.option = "--part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "一个有效的等厚实体板件；曲面、变厚板和观测面不适用";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "offset";
        p.binding = rm::Binding::Option;
        p.option = "--offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-100LL}}, rm::Value{std::int64_t{100LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{30.0}};
        p.description = "平面外轮廓等距外扩；新板不继承源板封闭内孔";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "thickness";
        p.binding = rm::Binding::Option;
        p.option = "--thickness";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{100LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{10.0}};
        p.description = "新增板件厚度，不修改源板厚度";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "face";
        p.binding = rm::Binding::Option;
        p.option = "--face";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2147483647LL}}}}; return r; }();
        p.required = true;
        p.description = "等厚实体板件的主面 BRep 编号（1-based），不能选薄侧面或孔壁";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "reverse";
        p.binding = rm::Binding::Option;
        p.option = "--reverse";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{false};
        p.description = "沿所选面的内法线拉伸，可与原板重叠；默认向外";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.generate_cover_plate --part imported-part-1 --face 3 --offset 30 --thickness 10" };
    return d;
}
GenerateCoverPlateCommand::Args GenerateCoverPlateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: part"); }
    if (const auto it = values.find("offset"); it != values.end())
    {
        args.offset = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: offset"); }
    if (const auto it = values.find("thickness"); it != values.end())
    {
        args.thickness = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: thickness"); }
    if (const auto it = values.find("face"); it != values.end())
    {
        args.face = it->second.as<std::int64_t>();
    }
    else { throw std::invalid_argument("Missing normalized argument: face"); }
    if (const auto it = values.find("reverse"); it != values.end())
    {
        args.reverse = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: reverse"); }
    return args;
}
}
