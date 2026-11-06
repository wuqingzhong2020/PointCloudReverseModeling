/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/AddHoleCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition AddHoleCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "在等厚板件底边添加过焊孔或底边孔";
    d.usage = "model.add_hole --part <部件ID> --edge <底边编号> --kind weld|bottom --shape circle|triangle|rectangle [--size 20] [--width 30 --height 15] [--reference start|middle|end --offset 0] [--first true --second true]";
    d.description = "";
    d.returns = "object: 更新后的工程摘要；保持板厚，支持撤销/重做、录制及 STEP 导出";
    {
        rm::Parameter p;
        p.name = "part";
        p.binding = rm::Binding::Option;
        p.option = "--part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "一个等厚实体板件的 ID；可通过 project.snapshot 查询";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "edge";
        p.binding = rm::Binding::Option;
        p.option = "--edge";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2147483647LL}}}}; return r; }();
        p.required = true;
        p.description = "当前部件主面直线外轮廓边的 BRep 编号，作为底边；界面录制会填写，重放不依赖视角";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "kind";
        p.binding = rm::Binding::Option;
        p.option = "--kind";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"weld", "bottom"}; return r; }();
        p.required = true;
        p.description = "weld：底角过焊孔；bottom：沿底边定位开孔";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "shape";
        p.binding = rm::Binding::Option;
        p.option = "--shape";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"circle", "triangle", "rectangle"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"circle"};
        p.description = "过焊孔支持 circle/triangle；底边孔支持 circle/rectangle";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "size";
        p.binding = rm::Binding::Option;
        p.option = "--size";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{20.0}};
        p.description = "圆孔半径或三角形两直角边长度，mm，必须大于 0";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "width";
        p.binding = rm::Binding::Option;
        p.option = "--width";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{30.0}};
        p.description = "矩形总宽，mm，孔中心两侧各一半";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "height";
        p.binding = rm::Binding::Option;
        p.option = "--height";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{15.0}};
        p.description = "矩形从底边切入板内的高度，mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "reference";
        p.binding = rm::Binding::Option;
        p.option = "--reference";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"start", "middle", "end"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"middle"};
        p.description = "底边端点 1 / 中点 / 端点 2；端点编号对应界面 1、2 标记";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "offset";
        p.binding = rm::Binding::Option;
        p.option = "--offset";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{0.0}};
        p.description = "底边孔偏移 mm；从端点向内为正，从中点朝端点 2 为正";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "first";
        p.binding = rm::Binding::Option;
        p.option = "--first";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "是否切除端点 1 的过焊孔";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "second";
        p.binding = rm::Binding::Option;
        p.option = "--second";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{true};
        p.description = "是否切除端点 2 的过焊孔";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.add_hole --part imported-part-4 --edge 4 --kind weld --shape circle --size 20", "model.add_hole --part imported-part-4 --edge 4 --kind bottom --shape rectangle --width 30 --height 15 --reference middle --offset 0" };
    return d;
}
AddHoleCommand::Args AddHoleCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: part"); }
    if (const auto it = values.find("edge"); it != values.end())
    {
        args.edge = it->second.as<std::int64_t>();
    }
    else { throw std::invalid_argument("Missing normalized argument: edge"); }
    if (const auto it = values.find("kind"); it != values.end())
    {
        if (it->second.as<std::string>() == "weld") { args.kind = KindValue::Value0; }
        else if (it->second.as<std::string>() == "bottom") { args.kind = KindValue::Value1; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: kind"); }
    if (const auto it = values.find("shape"); it != values.end())
    {
        if (it->second.as<std::string>() == "circle") { args.shape = ShapeValue::Value0; }
        else if (it->second.as<std::string>() == "triangle") { args.shape = ShapeValue::Value1; }
        else if (it->second.as<std::string>() == "rectangle") { args.shape = ShapeValue::Value2; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: shape"); }
    if (const auto it = values.find("size"); it != values.end())
    {
        args.size = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: size"); }
    if (const auto it = values.find("width"); it != values.end())
    {
        args.width = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: width"); }
    if (const auto it = values.find("height"); it != values.end())
    {
        args.height = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: height"); }
    if (const auto it = values.find("reference"); it != values.end())
    {
        if (it->second.as<std::string>() == "start") { args.reference = ReferenceValue::Value0; }
        else if (it->second.as<std::string>() == "middle") { args.reference = ReferenceValue::Value1; }
        else if (it->second.as<std::string>() == "end") { args.reference = ReferenceValue::Value2; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: reference"); }
    if (const auto it = values.find("offset"); it != values.end())
    {
        args.offset = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: offset"); }
    if (const auto it = values.find("first"); it != values.end())
    {
        args.first = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: first"); }
    if (const auto it = values.find("second"); it != values.end())
    {
        args.second = it->second.as<bool>();
    }
    else { throw std::invalid_argument("Missing normalized argument: second"); }
    return args;
}
}
