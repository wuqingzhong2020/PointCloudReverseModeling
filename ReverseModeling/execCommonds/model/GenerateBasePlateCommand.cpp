/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/GenerateBasePlateCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition GenerateBasePlateCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "生成下底板，保留源板并新增独立部件";
    d.usage = "model.generate_base_plate --mode horizontal --part <水平板ID> [--offset 20 --thickness 10]\nmodel.generate_base_plate --mode vertical_pair --part <竖板ID1> --second-part <竖板ID2> [--offset 20 --thickness 10]";
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
        p.description = "第一块等厚实体板件 ID；方向偏差允许 ±5°，源板不移动";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "mode";
        p.binding = rm::Binding::Option;
        p.option = "--mode";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"horizontal", "vertical_pair"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"horizontal"};
        p.description = "horizontal：一块水平板的底面轮廓；vertical_pair：两块相对平行的竖直板底部外包络，均只生成一块底板";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "second_part";
        p.binding = rm::Binding::Option;
        p.option = "--second-part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{""};
        p.description = "vertical_pair 必须指定不同的第二块板 ID；两板底部需横向分开、沿长度重叠且为连续直边；horizontal 不允许指定";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "offset";
        p.binding = rm::Binding::Option;
        p.option = "--offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{4LL}}, rm::Value{std::int64_t{1000LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{20.0}};
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
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{4LL}}, rm::Value{std::int64_t{50LL}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{10.0}};
        p.description = "新增板件厚度，不修改源板厚度";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "up";
        p.binding = rm::Binding::Option;
        p.option = "--up";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{rm::Array{rm::Value{double{0.0}}, rm::Value{double{0.0}}, rm::Value{double{1.0}}}};
        p.description = "固定世界 +Z [0,0,1]；其他方向拒绝。新板平行于 XY 平面，从源板最低 Z 沿 -Z 生成，不受基准面或相机影响";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.generate_base_plate --mode horizontal --part imported-part-1 --offset 20 --thickness 10", "model.generate_base_plate --mode vertical_pair --part imported-part-2 --second-part imported-part-3 --offset 20 --thickness 10" };
    return d;
}
GenerateBasePlateCommand::Args GenerateBasePlateCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: part"); }
    if (const auto it = values.find("mode"); it != values.end())
    {
        if (it->second.as<std::string>() == "horizontal") { args.mode = ModeValue::Value0; }
        else if (it->second.as<std::string>() == "vertical_pair") { args.mode = ModeValue::Value1; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: mode"); }
    if (const auto it = values.find("second_part"); it != values.end())
    {
        args.second_part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: second_part"); }
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
    if (const auto it = values.find("up"); it != values.end())
    {
        args.up = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: up"); }
    return args;
}
}
