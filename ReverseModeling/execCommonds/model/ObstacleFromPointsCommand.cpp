/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ObstacleFromPointsCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ObstacleFromPointsCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "框选点云生成参考面障碍物";
    d.usage = "model.obstacle_from_points --request <object>";
    d.description = "";
    d.returns = "object: 原子提交后的模型，可撤销及录制回放";
    {
        rm::Parameter p;
        p.name = "request";
        p.binding = rm::Binding::Option;
        p.option = "--request";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"shape", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"rectangle", "circle", "polygon", "polyline", "box"}; return r; }()}, {"method", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"axis", "diagonal", "corner", "three", "center"}; return r; }()}, {"points", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()); r.maxSize = 10000; return r; }()}, {"origin", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"normal", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"axis", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"width", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"length", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"diameter", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"height", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"offset", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"to_plane", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"cut", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"section", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"reversed", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"scope", [] { rm::Rule r; r.type = rm::ValueType::Enum; r.choices = {"all", "no_base", "manual"}; return r; }()}, {"targets", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::String; return r; }()); return r; }()}, {"indices", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Integer; r.ranges = {{rm::Value{std::int64_t{0LL}}, rm::Value{std::int64_t{9223372036854775807LL}}}}; return r; }()); return r; }()}}; return r; }();
        p.required = true;
        p.description = "有序工程坐标点和拉伸方向；障碍物记录完整点云索引和参考面剖视设置。";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
ObstacleFromPointsCommand::Args ObstacleFromPointsCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("request"); it != values.end())
    {
        args.request = it->second.as<rm::Arguments>();
    }
    else { throw std::invalid_argument("Missing normalized argument: request"); }
    return args;
}
}
