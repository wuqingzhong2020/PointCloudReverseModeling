/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/MirrorCompleteCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition MirrorCompleteCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "以底板（腹板）中面镜像补全所选板件";
    d.usage = "model.mirror_complete --parts <[\"部件ID\",...]> --plane-part <底板ID>";
    d.description = "";
    d.returns = "object: 整批成功提交；已完整对称时提示无需补全；部分重叠失败不修改模型";
    {
        rm::Parameter p;
        p.name = "parts";
        p.binding = rm::Binding::Option;
        p.option = "--parts";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()); r.minSize = 1; return r; }();
        p.required = true;
        p.description = "待补全的等厚实体板件；原有几何保留，相接镜像合并为原部件，分离镜像新增独立部件";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "plane_part";
        p.binding = rm::Binding::Option;
        p.option = "--plane-part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = true;
        p.description = "用作镜像参考的等厚底板（腹板）ID，使用实际中面，不随相机或世界 Z 改变；不得同时选入 parts";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.mirror_complete --parts '[\"flange-top\",\"rib-1-positive\"]' --plane-part web" };
    return d;
}
MirrorCompleteCommand::Args MirrorCompleteCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parts"); it != values.end())
    {
        args.parts = it->second.as<rm::Array>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parts"); }
    if (const auto it = values.find("plane_part"); it != values.end())
    {
        args.plane_part = it->second.as<std::string>();
    }
    else { throw std::invalid_argument("Missing normalized argument: plane_part"); }
    return args;
}
}
