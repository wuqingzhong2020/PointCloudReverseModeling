/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ReconstructLocalCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ReconstructLocalCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按编辑后的点云局部重建指定部件，保留未选部件";
    d.usage = "model.reconstruct_local --parts <[\"部件ID\",...]> [--type plate|horizontal|box] [--contour rectangle|circle|chamfer_rectangle|convex|polygon|smooth] [--up <[X,Y,Z]>] [--tolerance 2] [--corner 5]";
    d.description = "";
    d.returns = "object: 局部重建后的工程摘要，支持撤销、重做和脚本录制";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "与点云关联的原重建部件 ID；整批成功才替换，未选部件保持不变。选中部件已有开孔等修改由新识别结果替代。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "type";
        p.binding = rm::Binding::Option;
        p.option = "--type";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"plate", "horizontal", "box"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"plate"};
        p.description = "plate 保持原板面方向及板厚；horizontal 采用指定水平法向；box 按原板件方向拟合点云包围盒。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "contour";
        p.binding = rm::Binding::Option;
        p.option = "--contour";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"rectangle", "circle", "chamfer_rectangle", "convex", "polygon", "smooth"}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{"rectangle"};
        p.description = "矩形、包络圆、倒角矩形、凸包、凹多边形、连续曲线；包围盒忽略轮廓。仅拟合外轮廓，不重新识别内孔。";
        p.unit = "";
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
        p.description = "水平板法向，GUI 有基准面时明确记录其法向；其他类型忽略。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "tolerance";
        p.binding = rm::Binding::Option;
        p.option = "--tolerance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.1}}, rm::Value{double{1000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{2.0}};
        p.description = "任意多边形和连续曲线的轮廓采样/简化容差，过稀或分离的点云明确报错。";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "corner";
        p.binding = rm::Binding::Option;
        p.option = "--corner";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.001}}, rm::Value{double{100000.0}}}}; return r; }();
        p.required = false;
        p.defaultValue = rm::Value{double{5.0}};
        p.description = "倒角矩形各角退距，必须小于较短边的一半。";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.reconstruct_local --parts '[\"web\"]' --type plate --contour rectangle" };
    return d;
}
ReconstructLocalCommand::Args ReconstructLocalCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("type"); it != values.end())
    {
        if (it->second.as<std::string>() == "plate") { args.type = TypeValue::Value0; }
        else if (it->second.as<std::string>() == "horizontal") { args.type = TypeValue::Value1; }
        else if (it->second.as<std::string>() == "box") { args.type = TypeValue::Value2; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: type"); }
    if (const auto it = values.find("contour"); it != values.end())
    {
        if (it->second.as<std::string>() == "rectangle") { args.contour = ContourValue::Value0; }
        else if (it->second.as<std::string>() == "circle") { args.contour = ContourValue::Value1; }
        else if (it->second.as<std::string>() == "chamfer_rectangle") { args.contour = ContourValue::Value2; }
        else if (it->second.as<std::string>() == "convex") { args.contour = ContourValue::Value3; }
        else if (it->second.as<std::string>() == "polygon") { args.contour = ContourValue::Value4; }
        else if (it->second.as<std::string>() == "smooth") { args.contour = ContourValue::Value5; }
        else { throw std::invalid_argument("Unvalidated enum value"); }
    }
    else { throw std::invalid_argument("Missing normalized argument: contour"); }
    if (const auto it = values.find("up"); it != values.end())
    {
        args.up = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: up"); }
    if (const auto it = values.find("tolerance"); it != values.end())
    {
        args.tolerance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: tolerance"); }
    if (const auto it = values.find("corner"); it != values.end())
    {
        args.corner = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: corner"); }
    return args;
}
}
