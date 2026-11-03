/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "cloud/CropCommand.h"
namespace rmcmd::cloud
{
rm::CommandDefinition CropCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "cloud";
    d.summary = "保留选中点云";
    d.usage = "cloud.crop [--regions <array>] [--indices <array>]";
    d.description = "传入regions或indices即可独立重放选区并编辑，失败不改变当前选区或点云；均省略时兼容旧命令，使用当前选区。需使用相同输入点云和前序编辑结果。";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "regions";
        p.binding = rm::Binding::Option;
        p.option = "--regions";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"rectangle", [] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"viewProjection", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 16; r.maxSize = 16; return r; }()}, {"minX", [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-1LL}}, rm::Value{std::int64_t{1LL}}}}; return r; }()}, {"minY", [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-1LL}}, rm::Value{std::int64_t{1LL}}}}; return r; }()}, {"maxX", [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-1LL}}, rm::Value{std::int64_t{1LL}}}}; return r; }()}, {"maxY", [] { rm::Rule r; r.type = rm::ValueType::Number; r.ranges = {{rm::Value{std::int64_t{-1LL}}, rm::Value{std::int64_t{1LL}}}}; return r; }()}, {"zeroToOneDepth", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}}; return r; }()}, {"mode", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"replace", "add", "subtract"}; return r; }()}}; return r; }()); r.minSize = 1; r.maxSize = 1000; return r; }();
        p.required = false;
        p.description = "完整框选序列。矩形位置为(minX,minY)，宽高为(maxX-minX,maxY-minY)，坐标为NDC；viewProjection保存当时投影。按顺序替换/增选/减选，不读取当前视口。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "indices";
        p.binding = rm::Binding::Option;
        p.option = "--indices";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Unsigned; return r; }()); r.maxSize = 10000000; return r; }();
        p.required = false;
        p.description = "框选序列之前的初始选区，零起始点索引；未传时从空选区开始。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = { "cloud.crop --indices \"[0,1,2]\"", "rm.execute(\"cloud.crop\", regions=[{'rectangle': {'viewProjection': [1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1], 'minX': -0.5, 'minY': -0.5, 'maxX': 0.5, 'maxY': 0.5, 'zeroToOneDepth': False}, 'mode': 'replace'}])" };
    return d;
}
CropCommand::Args CropCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("regions"); it != values.end())
    {
        args.regions = it->second.as<rm::Array>();
    }
    if (const auto it = values.find("indices"); it != values.end())
    {
        args.indices = it->second.as<rm::Array>();
    }
    return args;
}
}
