/* 文件描述：由 rm_define 生成的声明元数据和类型转换；无产品依赖。 */
#include "model/ParametricCommand.h"
namespace rmcmd::model
{
rm::CommandDefinition ParametricCommand::definition()
{
    rm::CommandDefinition d;
    d.name = name();
    d.module = "model";
    d.summary = "按显式参数构建梁体";
    d.usage = "model.parametric --parameters <object> [--frame <object>]";
    d.description = "";
    d.returns = "object: 结果摘要；执行失败返回错误码";
    {
        rm::Parameter p;
        p.name = "parameters";
        p.binding = rm::Binding::Option;
        p.option = "--parameters";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"length", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"width", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"height", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"webThickness", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"flangeThickness", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"ribThickness", [] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()}, {"ribStations", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); return r; }()}, {"swapCrossSection", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}, {"mirrorSymmetry", [] { rm::Rule r; r.type = rm::ValueType::Boolean; return r; }()}}; return r; }();
        p.required = true;
        p.description = "梁尺寸及板厚，单位 mm";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    {
        rm::Parameter p;
        p.name = "frame";
        p.binding = rm::Binding::Option;
        p.option = "--frame";
        p.position = 0;
        p.rule = [] { rm::Rule r; r.type = rm::ValueType::Object; r.properties = {{"originMm", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"xAxis", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"yAxis", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}, {"zAxis", [] { rm::Rule r; r.type = rm::ValueType::Array; r.items = std::make_shared<rm::Rule>([] { rm::Rule r; r.type = rm::ValueType::Number; return r; }()); r.minSize = 3; r.maxSize = 3; return r; }()}}; return r; }();
        p.required = false;
        p.description = "未传入时采用点云识别框架";
        p.unit = "";
        d.parameters.push_back(std::move(p));
    }
    d.examples = {  };
    return d;
}
ParametricCommand::Args ParametricCommand::arguments(const rm::Arguments &values)
{
    (void)values;
    Args args;
    if (const auto it = values.find("parameters"); it != values.end())
    {
        args.parameters = it->second.as<rm::Arguments>();
    }
    else { throw std::invalid_argument("Missing normalized argument: parameters"); }
    if (const auto it = values.find("frame"); it != values.end())
    {
        args.frame = it->second.as<rm::Arguments>();
    }
    return args;
}
}
