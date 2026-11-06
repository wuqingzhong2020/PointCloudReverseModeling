/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/SetThicknessCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition SetThicknessCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "修改等厚板件的真实板厚，保持中面和轮廓";
    d.usage = "model.set_thickness --parts <[\"部件ID\",...]> --thickness <毫米>";
    d.description = "";
    d.returns = "object: 更新后的工程摘要；支持撤销、重做、录制、回放和 STEP 导出";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "选中的等厚实体板件 ID；支持多个部件，整批成功才提交";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "thickness";
        p.binding = rm::Binding::Option;
        p.option = "--thickness";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.001}}, rm::Value{std::int64_t{1000000LL}}}}; return r; }();
        p.required = true;
        p.description = "目标板厚；中面固定、两侧等量增减，保留轮廓及贯穿孔；不支持变厚板/曲面板";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.set_thickness --parts '[\"imported-part-5\"]' --thickness 20" };
    return d;
}
SetThicknessCommand::Args SetThicknessCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("thickness"); it != values.end())
    {
        args.thickness = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: thickness"); }
    return args;
}
}
