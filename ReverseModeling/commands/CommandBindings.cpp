/* 文件描述：将产品命令定义绑定到现有同步几何业务，统一返回错误码和提交状态。 */
#include "commands/CommandBindings.h"
#include <algorithm>
#include "cloud/ImportCommand.h"
#include "commands/Json.h"
#include "commands/ProjectOperations.h"
#include "geometry/Geometry.h"

namespace reverse
{
std::filesystem::path commandPath(std::string_view utf8)
{
    return std::filesystem::path(std::u8string_view(reinterpret_cast<const char8_t *>(utf8.data()), utf8.size()));
}
rm::Value commandValue(const nlohmann::json &value)
{
    if (value.is_null())
    {
        return {};
    }
    if (value.is_boolean())
    {
        return value.get<bool>();
    }
    if (value.is_number_unsigned())
    {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer())
    {
        return value.get<std::int64_t>();
    }
    if (value.is_number_float())
    {
        return value.get<double>();
    }
    if (value.is_string())
    {
        return value.get<std::string>();
    }
    if (value.is_array())
    {
        rm::Array result;
        for (const auto &item : value)
        {
            result.push_back(commandValue(item));
        }
        return result;
    }
    rm::Arguments result;
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        result.emplace(it.key(), commandValue(it.value()));
    }
    return result;
}
nlohmann::json commandJson(const rm::Value &value)
{
    return nlohmann::json::parse(rm::formatValue(value));
}
namespace
{
CloudSelectionMode selectionMode(const std::string &mode)
{
    if (mode == "clear")
    {
        return CloudSelectionMode::Clear;
    }
    if (mode == "add")
    {
        return CloudSelectionMode::Add;
    }
    if (mode == "subtract")
    {
        return CloudSelectionMode::Remove;
    }
    return CloudSelectionMode::Replace;
}
using Business = std::function<void(const rm::Arguments &, std::uint64_t)>;
CloudSelectionRect rectangleValue(const rm::Value &value)
{
    const auto rectangle = commandJson(value);
    CloudSelectionRect r;
    r.viewProjection = rectangle.at("viewProjection").get<std::array<double, 16>>();
    r.minX = rectangle.at("minX");
    r.minY = rectangle.at("minY");
    r.maxX = rectangle.at("maxX");
    r.maxY = rectangle.at("maxY");
    r.zeroToOneDepth = rectangle.at("zeroToOneDepth");
    return r;
}
std::optional<CloudSelectionRecipe> editSelection(const rm::Arguments &args)
{
    if (!args.contains("regions") && !args.contains("indices"))
    {
        return {};
    }
    CloudSelectionRecipe result;
    if (const auto indices = args.find("indices"); indices != args.end())
    {
        auto values = commandJson(indices->second).get<CloudSelection>();
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
        result.initialIndices = std::make_shared<const CloudSelection>(std::move(values));
    }
    if (const auto regions = args.find("regions"); regions != args.end())
    {
        for (const auto &value : regions->second.as<rm::Array>())
        {
            const auto &region = value.as<rm::Arguments>();
            auto rectangle = rectangleValue(region.at("rectangle"));
            if (rectangle.minX >= rectangle.maxX || rectangle.minY >= rectangle.maxY)
            {
                throw GeometryError("invalid_selection", "参数 --regions[" + std::to_string(result.regions.size()) +
                                                             "].rectangle 的 maxX/maxY 必须分别大于 minX/minY。");
            }
            result.regions.push_back({rectangle, selectionMode(region.at("mode").as<std::string>())});
        }
    }
    return result;
}
std::string successMessage(const std::string &name, const ProjectSnapshot &project)
{
    if (name == "cloud.import")
    {
        return project.cloud ? "已导入点云，共 " + std::to_string(project.cloud->points.size()) + " 点。"
                             : "已导入模型，共 " + std::to_string(project.model->solidCount) + " 个实体、" +
                                   std::to_string(project.model->surfaceCount) + " 个曲面。";
    }
    if (name == "cloud.select" || name == "cloud.select_indices")
    {
        return "已选择 " + std::to_string(project.selection ? project.selection->size() : 0) + " 个点。";
    }
    if (name == "cloud.export" || name == "model.export_step")
    {
        return name == "cloud.export" ? "点云已导出。" : "模型已导出。";
    }
    if (name == "model.reconstruct" || name == "model.parametric")
    {
        return "模型已生成，共 " + std::to_string(project.model->solidCount) + " 个实体、" +
               std::to_string(project.model->surfaceCount) + " 个曲面。";
    }
    if (name == "model.deviation")
    {
        return "偏差计算完成。";
    }
    return "点云已更新，当前 " + std::to_string(project.cloud ? project.cloud->points.size() : 0) + " 点。";
}
void bind(rm::CommandManager &manager, ProjectOperations &operations, const std::string &name, Business business)
{
    manager.bindHandler(
        name, [&operations, name, business = std::move(business)](const auto &context, const auto &args) {
            try
            {
                const auto before = operations.project();
                if (context.expectedRevision && *context.expectedRevision != before.revision)
                {
                    return rm::CommandResult::error("stale_revision", "工程已变化，请刷新后重试。");
                }
                operations.setContext(context);
                business(args, before.revision);
                const auto after = operations.project();
                const bool committed = after.revision != before.revision || after.deviation != before.deviation ||
                                       name == "cloud.export" || name == "model.export_step";
                return rm::CommandResult{
                    rm::ResultKind::Success, {}, successMessage(name, after), commandValue(toJson(after)), committed};
            }
            catch (const GeometryError &error)
            {
                return rm::CommandResult::error(error.code(), error.what());
            }
        });
}
} // namespace
void registerApplicationCommandHandlers(rm::CommandManager &manager, ProjectOperations &operations)
{
    bind(manager, operations, "cloud.import", [&operations](const rm::Arguments &args, auto) {
        // Typed descriptor conversion is separate from the business implementation.
        const auto input = rmcmd::cloud::ImportCommand::arguments(args);
        (void)operations.submitImport(commandPath(input.path), parseLengthUnit(args.at("unit").as<std::string>()));
    });
    bind(manager, operations, "cloud.select", [&operations](const rm::Arguments &args, auto revision) {
        const auto r = rectangleValue(args.at("rectangle"));
        (void)operations.submitCloudSelection(r, selectionMode(args.at("mode").as<std::string>()), revision);
    });
    bind(manager, operations, "cloud.select_indices", [&operations](const rm::Arguments &args, auto) {
        operations.selectIndices(args.at("indices").as<rm::Array>(), selectionMode(args.at("mode").as<std::string>()));
    });
    bind(manager, operations, "cloud.crop", [&operations](const auto &args, auto revision) {
        (void)operations.submitCloudEdit(CloudEditMode::KeepSelection, revision, editSelection(args));
    });
    bind(manager, operations, "cloud.delete", [&operations](const auto &args, auto revision) {
        (void)operations.submitCloudEdit(CloudEditMode::DeleteSelection, revision, editSelection(args));
    });
    for (const auto &[name, action] : {std::pair{"cloud.undo", CloudHistoryAction::Undo},
                                       {"cloud.redo", CloudHistoryAction::Redo},
                                       {"cloud.reset", CloudHistoryAction::Reset}})
    {
        bind(manager, operations, name, [&operations, action](const auto &, auto revision) {
            (void)operations.submitCloudHistory(action, revision);
        });
    }
    bind(manager, operations, "cloud.export", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitExportCloud(commandPath(args.at("path").as<std::string>()), revision);
    });
    bind(manager, operations, "model.reconstruct", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitRecognizedReconstruct(args.at("mirror").as<bool>(), revision,
                                                     args.at("end_tolerance").as<double>());
    });
    bind(manager, operations, "model.parametric", [&operations](const rm::Arguments &args, auto revision) {
        const auto json = commandJson(rm::Value(args));
        (void)operations.submitReconstruct(parseBeamParameters(json), revision, parseFrameOverride(json));
    });
    bind(manager, operations, "model.export_step", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitExport(commandPath(args.at("path").as<std::string>()), revision);
    });
    bind(manager, operations, "model.deviation",
         [&operations](const auto &, auto revision) { (void)operations.submitDeviation(revision); });
    manager.bindHandler("project.snapshot", [&operations](const auto &, const auto &) {
        return rm::CommandResult{rm::ResultKind::Success, {}, {}, commandValue(toJson(operations.project())), false};
    });
}
} // namespace reverse
