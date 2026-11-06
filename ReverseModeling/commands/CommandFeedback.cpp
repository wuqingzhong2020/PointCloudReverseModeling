/* 文件描述：默认显示命令摘要或具体错误；只有显式查询才回显完整结构化值。 */
#include <algorithm>
#include <cctype>
#include <set>
#include <sstream>
#include <iomanip>
#include <locale>
#include "commands/CommandService.h"

#include "commands/CommandFeedback.h"

namespace reverse
{
namespace
{
std::string quoteToken(const std::string &value)
{
    if (!value.empty() && value.find_first_of(" \t\r\n\"'") == std::string::npos)
    {
        return value;
    }
    // The lexer preserves Windows backslashes; JSON escaping cannot be used for command tokens.
    for (const char quote : {'"', '\''})
    {
        if (value.find(quote) == std::string::npos && (value.empty() || value.back() != '\\'))
        {
            return quote + value + quote;
        }
    }
    std::string result;
    for (const char c : value)
    {
        if (c == '\'')
        {
            result += "\"'\"";
        }
        else if (c == '"' || std::isspace(static_cast<unsigned char>(c)))
        {
            result += std::string("'") + c + '\'';
        }
        else
        {
            result += c;
        }
    }
    return result;
}
std::string nativeValue(const rm::Value &value)
{
    return quoteToken(value.is<std::string>() ? value.as<std::string>() : rm::formatValue(value));
}
std::string pythonValue(const rm::Value &value)
{
    if (value.is<std::monostate>())
    {
        return "None";
    }
    if (value.is<bool>())
    {
        return value.as<bool>() ? "True" : "False";
    }
    if (value.is<rm::Array>() || value.is<rm::Arguments>())
    {
        const bool array = value.is<rm::Array>();
        std::string result = array ? "[" : "{";
        const auto append = [&](const std::string &entry) {
            if (result.size() > 1)
            {
                result += ", ";
            }
            result += entry;
        };
        if (array)
        {
            for (const auto &item : value.as<rm::Array>())
            {
                append(pythonValue(item));
            }
        }
        else
        {
            for (const auto &[key, item] : value.as<rm::Arguments>())
            {
                append(rm::formatValue(key) + ": " + pythonValue(item));
            }
        }
        return result + (array ? ']' : '}');
    }
    return rm::formatValue(value);
}
} // namespace
CommandText commandText(const rm::CommandDefinition &definition, const rm::Array &positional,
                        const rm::Arguments &options)
{
    CommandText result{definition.name, "rm.execute(" + rm::formatValue(definition.name)};
    std::string positionText;
    bool terminateOptions = false;
    for (const auto &value : positional)
    {
        positionText += " " + nativeValue(value);
        terminateOptions |= value.is<std::string>() && value.as<std::string>().starts_with('-');
        result.python += ", " + pythonValue(value);
    }
    if (!terminateOptions)
    {
        result.native += positionText;
    }
    std::set<std::string> renderedOptions;
    for (const auto &parameter : definition.parameters)
    {
        const auto value = options.find(parameter.name);
        if (parameter.binding != rm::Binding::Option || value == options.end())
        {
            continue;
        }
        const auto token = nativeValue(value->second);
        // = keeps literal --help / --values from being parsed as options.
        const bool leadingDash = value->second.is<std::string>() && value->second.as<std::string>().starts_with('-');
        result.native += " " + parameter.option + (leadingDash ? "=" : " ") + token;
        renderedOptions.insert(parameter.name);
    }
    // Failed script calls must also show undeclared options instead of silently omitting them.
    for (const auto &[name, value] : options)
    {
        if (!renderedOptions.contains(name))
        {
            result.native += " --" + name + "=" + nativeValue(value);
        }
    }
    if (terminateOptions)
    {
        result.native += " --" + positionText;
    }
    if (!options.empty())
    {
        result.python += ", **" + pythonValue(options);
    }
    result.python += ')';
    return result;
}
std::string commandArgumentsText(const std::vector<std::string> &arguments)
{
    std::string result;
    for (const auto &argument : arguments)
    {
        if (!result.empty()) result += ' ';
        result += quoteToken(argument);
    }
    return result;
}
std::string commandExecutionEcho(std::string_view line, bool includeInput)
{
    if (line.find_first_not_of(" \t\r\n") == std::string_view::npos) return {};
    const std::string command(line);
    return (includeInput ? "rm> " + command + '\n' : std::string{}) + "// " + command + '\n';
}
std::string commandFeedback(const rm::CommandResult &result)
{
    if (!result.message.empty())
    {
        return result.message;
    }
    if (result.kind == rm::ResultKind::Success && !result.value.is<std::monostate>())
    {
        return rm::formatValue(result.value);
    }
    return {};
}
std::string commandDetails(const std::string &name, const ProjectSnapshot &project)
{
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(8);
    const bool cloudChanged = name == "cloud.import" || name == "cloud.crop" || name == "cloud.delete" ||
                              name == "cloud.undo" || name == "cloud.redo" || name == "cloud.reset";
    const auto warnings = [&out](const auto &values, const std::string &prefix) {
        for (const auto &warning : values) out << prefix << warning << '\n';
    };
    if (cloudChanged)
    {
        if (name == "cloud.import") out << "源文件：" << project.sourcePathUtf8 << '\n';
        if (project.cloud)
        {
            const auto &cloud = *project.cloud;
            out << "原始点 " << cloud.sourcePointCount << "；无效点 " << cloud.invalidPointCount
                << "；当前点 " << cloud.points.size() << "；1 单位 = " << cloud.sourceScaleToMm << " mm。\n";
            if (project.estimate) warnings(project.estimate->warnings, "初值：");
            if (name != "cloud.import") out << "源文件未修改，可使用撤销、重做或重置恢复点云。\n";
        }
        else if (project.model)
        {
            out << "模型格式 " << project.model->importedFormat << "；显示及导出单位 mm。\n";
        }
        warnings(project.warnings, "");
    }
    if ((name == "model.reconstruct_local" || name == "model.reconstruct" || name == "model.parametric") && project.model)
    {
        out << "模型版本 " << project.revision << "；" << project.model->solidCount << " 个板件实体，"
            << project.model->surfaceCount << " 个观测面。\n";
        out << (project.model->fromPointCloud ? "双击板厚标签可修改，厚度来源可在属性中查看。"
                                                            : "手动参数模型，标签不是点云识别厚度。") << '\n';
        warnings(project.model->warnings, "");
    }
    if ((name == "model.export_step" || name == "model.export_cwrm") && project.lastExport)
    {
        const auto &result = *project.lastExport;
        out << (name == "model.export_cwrm" ? "CWRM 回读：" : "STEP 回读：") << result.solidCount << " 个实体，" << result.surfaceCount << " 个观测面，"
            << result.fileBytes << " 字节；文件：" << project.exportedPathUtf8 << '\n';
    }
    if ((name == "cloud.export" || name == "model.export_pcd") && project.lastCloudExport)
    {
        const auto &result = *project.lastCloudExport;
        out << "PCD 导出：" << result.pointCount << " 点，" << result.fileBytes
            << " 字节；文件：" << project.exportedCloudPathUtf8 << '\n';
    }
    if (name == "model.deviation" && project.deviation)
    {
        const auto &result = *project.deviation;
        out << "偏差（mm）：均值 " << result.meanMm << "，RMS " << result.rmsMm << "，P95 " << result.p95Mm
            << "，最大 " << result.maximumMm << "；" << result.pointCount << " 点。仅当前矩形板件表面。\n";
    }
    return out.str();
}
} // namespace reverse
