/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ExpandContourCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ExpandContourCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "外扩等厚板件外轮廓，保持板厚及封闭内孔";
    d.usage = "model.expand_contour --parts <[\"部件ID\",...]> --distance <毫米>";
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
        p.description = "等厚实体板件 ID；支持多选，整批成功才提交";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "distance";
        p.binding = rm::Binding::Option;
        p.option = "--distance";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{double{0.001}}, rm::Value{std::int64_t{1000000LL}}}}; return r; }();
        p.required = true;
        p.description = "各外边界在板面内向外偏移的距离；直线角延伸相交，封闭内孔不变，开放缺口随外轮廓偏移；再次应用为累加";
        p.unit = "mm";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.expand_contour --parts '[\"imported-part-1\"]' --distance 10" };
    return d;
}
ExpandContourCommand::Args ExpandContourCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("distance"); it != values.end())
    {
        args.distance = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: distance"); }
    return args;
}
}
