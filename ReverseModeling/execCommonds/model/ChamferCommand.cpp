/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ChamferCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ChamferCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "对板件轮廓应用等距倒角，保持板厚";
    d.usage = "model.chamfer (--part <部件ID> --edges <[边编号,...]> | --targets <分组数组>) --size <毫米>";
    d.description = "";
    d.returns = "object: 更新后的工程摘要；可撤销、录制和回放";
    {
        rm::Parameter p;
        p.name = "part";
        p.binding = rm::Binding::Option;
        p.option = "--part";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }();
        p.required = false;
        p.description = "模型部件 ID，可通过 project.snapshot 查询";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "edges";
        p.binding = rm::Binding::Option;
        p.option = "--edges";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2147483647LL}}}}; return r; }()); r.minSize = 1; return r; }();
        p.required = false;
        p.description = "当前部件 BRep 选边编号（从 1 开始）；轮廓边处理两端板角，贯穿板厚短边处理一个角";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "targets";
        p.binding = rm::Binding::Option;
        p.option = "--targets";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"part", [] { rm::Rule r; r.type = rm::ValueType::String; r.minSize = 1; return r; }()}, {"edges", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{1LL}}, rm::Value{std::int64_t{2147483647LL}}}}; return r; }()); r.minSize = 1; return r; }()}}; return r; }()); r.minSize = 1; return r; }();
        p.required = false;
        p.description = "多部件边线分组；每组含 part 和 edges，不可与 --part/--edges 同用；整批成功才提交";
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
        p.required = true;
        p.description = "轮廓两侧距离，mm，必须大于 0；保持板厚；命令直接应用";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "model.chamfer --part web --edges '[1]' --size 1", "model.chamfer --targets '[{\"part\":\"web\",\"edges\":[1]},{\"part\":\"flange-bottom\",\"edges\":[1]}]' --size 1" };
    return d;
}
ChamferCommand::Args ChamferCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("part"); it != values.end())
    {
        args.part = it->second.as<std::string>();
    }
    if (const auto it = values.find("edges"); it != values.end())
    {
        args.edges = it->second.as<rm::Array>();
    }
    if (const auto it = values.find("targets"); it != values.end())
    {
        args.targets = it->second.as<rm::Array>();
    }
    if (const auto it = values.find("size"); it != values.end())
    {
        args.size = it->second.as<double>();
    }
    else { throw std::invalid_argument("Missing normalized argument: size"); }
    return args;
}
}
