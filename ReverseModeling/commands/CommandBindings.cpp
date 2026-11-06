#include "commands/PlateEditCommands.h"
/* 文件描述：将产品命令定义绑定到现有同步几何业务，统一返回错误码和提交状态。 */
#include "commands/CommandBindings.h"
#include "commands/CommandFeedback.h"
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
EdgeTreatment edgeTreatmentRequest(const rm::Arguments &args, EdgeTreatmentKind kind)
{
    EdgeTreatment request;
    request.kind = kind;
    request.sizeMm = args.at("size").as<double>();
    const auto add = [&](const rm::Arguments &group) {
        EdgeTreatmentTarget target;
        target.partId = group.at("part").as<std::string>();
        for (const auto &edge : group.at("edges").as<rm::Array>())
            target.edges.push_back(static_cast<std::uint32_t>(edge.as<std::int64_t>()));
        request.targets.push_back(std::move(target));
    };
    if (args.contains("targets"))
    {
        if (args.contains("part") || args.contains("edges"))
            throw GeometryError("conflicting_targets", "参数 --targets 不可与 --part / --edges 同时使用。");
        for (const auto &group : args.at("targets").as<rm::Array>()) add(group.as<rm::Arguments>());
    }
    else
    {
        if (!args.contains("part") || !args.contains("edges"))
            throw GeometryError("missing_targets", "请同时填写 --part 和 --edges，或使用 --targets 指定多部件边线。");
        add(args);
    }
    return request;
}

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
    if (name == "model.move" || name == "model.rotate") return "部件变换已完成；STEP 导出使用变换后的几何。";
    if (name == "model.delete") return project.model
        ? "部件已删除，剩余 " + std::to_string(project.model->partIds.size()) + " 个部件。"
        : "模型部件已全部删除；原始点云保持不变。";
    if (name == "project.set_reference_plane") return "基准面已设置；原始坐标与模型保持不变。";
    if (name == "project.clear_reference_plane") return "基准面和点云剖视已清除。";
    if (name == "project.section") return project.section.enabled
        ? "已启用点云剖视，仅改变显示；点数、重建与导出不变。" : "已关闭点云剖视。";
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
    if (name == "file.convert") return "文件转换完成；当前工程未改变。";
    if (name == "model.export_pcd") return "模型表面采样已导出，非原始扫描点云。";
    if (name == "cloud.export" || name == "model.export_step" || name == "model.export_cwrm")
    {
        return name == "cloud.export" ? "点云已导出。" : "模型已导出。";
    }
    if (name == "model.reconstruct_local") return "已局部重建所选部件；未选部件保持不变，可撤销。";
    if (name == "model.reconstruct" || name == "model.parametric")
    {
        return "模型已生成，共 " + std::to_string(project.model->solidCount) + " 个实体、" +
               std::to_string(project.model->surfaceCount) + " 个曲面。";
    }
    if (name == "model.generate_cover_plate" || name == "model.generate_base_plate")
        return std::string(name == "model.generate_cover_plate" ? "已生成外扩贴板：" : "已生成下底板：") +
            project.model->partIds.back() + "；源板保留，支持撤销和 STEP 导出。";
    if (name == "model.mirror_complete") return "镜像补全已应用，保留原有几何；支持撤销和 STEP 导出。";
    if (name == "model.expand_contour") return "板件轮廓已外扩，板厚和封闭内孔保持不变；支持撤销和 STEP 导出。";
    if (name == "model.set_thickness") return "板厚已修改，中面及轮廓保持不变；支持撤销和 STEP 导出。";
    if (name == "model.add_hole") return "孔已应用，板厚保持不变；可以撤销，STEP 导出包含开孔。";
    if (name == "model.fillet" || name == "model.chamfer") return "圆角 / 倒角已应用，可以撤销。";
    if (name == "model.deviation")
    {
        return "偏差计算完成。";
    }
    for (const auto op : {"extend","trim","merge","split","crop","repair","holes"})
        if (name==plateEditCommand(op)) return plateEditTitle(op)+"处理完成。";
    return "点云已更新，当前 " + std::to_string(project.cloud ? project.cloud->points.size() : 0) + " 点。";
}
void bindBusiness(rm::CommandManager &manager, ProjectOperations &operations, const std::string &name, Business business)
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
                if (context.output)
                {
                    const auto details = commandDetails(name, after);
                    if (!details.empty()) context.output(details);
                }
                const bool committed = after.revision != before.revision || after.deviation != before.deviation ||
                                       name == "cloud.export" || name == "model.export_step" || name == "model.export_cwrm" ||
                                       name == "model.export_pcd" || name == "file.convert";
                auto message = successMessage(name, after);
                if (name == "model.edit_plate")
                    message = plateEditTitle(args.at("operation").template as<std::string>()) +
                        (committed ? "完成。" : "：无需修改。");
                if (!committed && (name == "model.move" || name == "model.rotate")) message = "位移或角度为零，模型保持不变。";
                if (name == "project.undo" || name == "cloud.undo") message = "已撤销：" + before.undoLabel + "。";
                if (name == "project.redo" || name == "cloud.redo") message = "已重做：" + before.redoLabel + "。";
                if (name == "project.reset" || name == "cloud.reset")
                    message = before.canReset ? "已恢复本次导入状态；可以撤销此次重置。" : "当前已是导入状态。";
                return rm::CommandResult{
                    rm::ResultKind::Success, {}, std::move(message), commandValue(toJson(after)), committed};
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
    bindBusiness(manager, operations, "model.edit_plate", [&operations](const auto &args, auto revision) {
        operations.applyPlateEdit(plateEditRequest("model.edit_plate", args), revision);
    });
    for (const auto op : {"extend","trim","merge","split","crop","repair","holes"})
    {
        const auto name=plateEditCommand(op);
        bindBusiness(manager,operations,name,[&operations,name](const auto &args,auto revision){ operations.applyPlateEdit(plateEditRequest(name,args),revision); });
    }
    bindBusiness(manager, operations, "model.reconstruct_local", [&operations](const rm::Arguments &args, auto revision) {
        LocalReconstruction request;
        for (const auto &id : args.at("parts").as<rm::Array>()) request.partIds.push_back(id.as<std::string>());
        request.type = args.at("type").as<std::string>();
        request.contour = args.at("contour").as<std::string>();
        const auto &up = args.at("up").as<rm::Array>();
        request.up = {up[0].as<double>(), up[1].as<double>(), up[2].as<double>()};
        request.toleranceMm = args.at("tolerance").as<double>();
        request.cornerMm = args.at("corner").as<double>();
        operations.applyLocalReconstruction(request, revision);
    });
    bindBusiness(manager, operations, "model.set_thickness", [&operations](const rm::Arguments &args, auto revision) {
        PlateThicknessChange request;
        for (const auto &id : args.at("parts").as<rm::Array>())
        {
            request.partIds.push_back(id.as<std::string>());
        }
        request.thicknessMm = args.at("thickness").as<double>();
        operations.applyThickness(request, revision);
    });
    bindBusiness(manager, operations, "model.mirror_complete", [&operations](const rm::Arguments &args, auto revision) {
        MirrorCompletion request;
        for (const auto &id : args.at("parts").as<rm::Array>()) request.partIds.push_back(id.as<std::string>());
        request.planePartId = args.at("plane_part").as<std::string>();
        operations.applyMirrorCompletion(request, revision);
    });
    bindBusiness(manager, operations, "model.expand_contour", [&operations](const rm::Arguments &args, auto revision) {
        PlateContourExpansion request;
        for (const auto &id : args.at("parts").as<rm::Array>())
        {
            request.partIds.push_back(id.as<std::string>());
        }
        request.distanceMm = args.at("distance").as<double>();
        operations.applyContourExpansion(request, revision);
    });
    for (const auto kind : {PlateGenerationKind::Cover, PlateGenerationKind::Base})
    {
        bindBusiness(manager, operations, kind == PlateGenerationKind::Cover ? "model.generate_cover_plate" : "model.generate_base_plate",
            [&operations, kind](const rm::Arguments &args, auto revision) {
                PlateGeneration request;
                request.kind = kind;
                request.partId = args.at("part").as<std::string>();
                request.offsetMm = args.at("offset").as<double>();
                request.thicknessMm = args.at("thickness").as<double>();
                if (kind == PlateGenerationKind::Cover)
                {
                    request.faceIndex = static_cast<std::uint32_t>(args.at("face").as<std::int64_t>());
                    request.reversed = args.at("reverse").as<bool>();
                }
                else
                {
                    request.baseMode = args.at("mode").as<std::string>() == "horizontal"
                        ? BasePlateMode::Horizontal : BasePlateMode::VerticalPair;
                    request.secondPartId = args.at("second_part").as<std::string>();
                    const auto &up = args.at("up").as<rm::Array>();
                    request.up = {up[0].as<double>(), up[1].as<double>(), up[2].as<double>()};
                }
                operations.applyPlateGeneration(request, revision);
            });
    }
    bindBusiness(manager, operations, "model.add_hole", [&operations](const rm::Arguments &args, auto revision) {
        PlateHole request;
        request.partId = args.at("part").as<std::string>();
        request.bottomEdge = static_cast<std::uint32_t>(args.at("edge").as<std::int64_t>());
        request.kind = args.at("kind").as<std::string>() == "weld" ? PlateHoleKind::Weld : PlateHoleKind::Bottom;
        const auto &shape = args.at("shape").as<std::string>();
        request.shape = shape == "circle" ? PlateHoleShape::Circle : shape == "triangle" ? PlateHoleShape::Triangle : PlateHoleShape::Rectangle;
        const auto &reference = args.at("reference").as<std::string>();
        request.reference = reference == "start" ? HoleReference::Start : reference == "end" ? HoleReference::End : HoleReference::Middle;
        request.sizeMm = args.at("size").as<double>();
        request.widthMm = args.at("width").as<double>();
        request.heightMm = args.at("height").as<double>();
        request.offsetMm = args.at("offset").as<double>();
        request.first = args.at("first").as<bool>();
        request.second = args.at("second").as<bool>();
        operations.applyHole(request, revision);
    });
    for (const auto name : {"model.fillet", "model.chamfer"})
    {
        bindBusiness(manager,operations,name,[&operations,name](const rm::Arguments &args, auto revision) {
            const auto kind = std::string_view(name) == "model.fillet" ? EdgeTreatmentKind::Fillet : EdgeTreatmentKind::Chamfer;
            const auto request = edgeTreatmentRequest(args, kind);
            operations.applyEdgeTreatment(request,revision);
        });
    }
    for (const auto name : {"model.move", "model.rotate"})
    {
        bindBusiness(manager, operations, name, [&operations, name](const rm::Arguments &args, auto revision) {
            std::vector<std::string> ids;
            for (const auto &id : args.at("parts").as<rm::Array>()) ids.push_back(id.as<std::string>());
            const auto vec = [&](const char *key) {
                const auto &a = args.at(key).as<rm::Array>();
                return Vec3{a.at(0).as<double>(), a.at(1).as<double>(), a.at(2).as<double>()};
            };
            ModelTransform request;
            if (std::string_view(name) == "model.move") request.offsetMm = vec("offset");
            else
            {
                request.kind = ModelTransformKind::Rotate;
                const auto &axis = args.at("axis").as<std::string>();
                request.axis = axis == "x" ? RotationAxis::X : axis == "y" ? RotationAxis::Y : RotationAxis::Z;
                request.angleDegrees = args.at("angle").as<double>();
                request.centerMm = vec("center");
            }
            (void)operations.submitModelTransform(ids, request, revision);
        });
    }
    bindBusiness(manager, operations, "model.delete", [&operations](const auto &args, auto revision) {
        std::vector<std::string> ids;
        for (const auto &id : args.at("parts").template as<rm::Array>()) ids.push_back(id.template as<std::string>());
        (void)operations.submitDeleteModelParts(ids, revision);
    });
    bindBusiness(manager, operations, "cloud.import", [&operations](const rm::Arguments &args, auto) {
        // Typed descriptor conversion is separate from the business implementation.
        const auto input = rmcmd::cloud::ImportCommand::arguments(args);
        (void)operations.submitImport(commandPath(input.path));
    });
    bindBusiness(manager, operations, "cloud.select", [&operations](const rm::Arguments &args, auto revision) {
        const auto r = rectangleValue(args.at("rectangle"));
        (void)operations.submitCloudSelection(r, selectionMode(args.at("mode").as<std::string>()), revision,
                                             args.at("edit_last").as<bool>());
    });
    bindBusiness(manager, operations, "cloud.select_indices", [&operations](const rm::Arguments &args, auto) {
        operations.selectIndices(args.at("indices").as<rm::Array>(), selectionMode(args.at("mode").as<std::string>()));
    });
    bindBusiness(manager, operations, "cloud.crop", [&operations](const auto &args, auto revision) {
        (void)operations.submitCloudEdit(CloudEditMode::KeepSelection, revision, editSelection(args));
    });
    bindBusiness(manager, operations, "cloud.delete", [&operations](const auto &args, auto revision) {
        (void)operations.submitCloudEdit(CloudEditMode::DeleteSelection, revision, editSelection(args));
    });
    for (const auto &[name, action] : {std::pair{"cloud.undo", CloudHistoryAction::Undo},
                                       {"cloud.redo", CloudHistoryAction::Redo},
                                       {"cloud.reset", CloudHistoryAction::Reset},
                                       {"project.undo", CloudHistoryAction::Undo},
                                       {"project.redo", CloudHistoryAction::Redo},
                                       {"project.reset", CloudHistoryAction::Reset}})
    {
        bindBusiness(manager, operations, name, [&operations, action](const auto &, auto revision) {
            (void)operations.submitCloudHistory(action, revision);
        });
    }
    bindBusiness(manager, operations, "cloud.export", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitExportCloud(commandPath(args.at("path").as<std::string>()), revision);
    });
    bindBusiness(manager, operations, "model.reconstruct", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitRecognizedReconstruct(args.at("mirror").as<bool>(), revision,
                                                     args.at("end_tolerance").as<double>(),
                                                     args.at("web_holes").as<bool>(), args.at("min_hole_radius").as<double>());
    });
    bindBusiness(manager, operations, "model.parametric", [&operations](const rm::Arguments &args, auto revision) {
        const auto json = commandJson(rm::Value(args));
        (void)operations.submitReconstruct(parseBeamParameters(json), revision, parseFrameOverride(json));
    });
    bindBusiness(manager, operations, "model.export_cwrm", [&operations](const auto &args, auto revision) {
        operations.exportCwrmModel(commandPath(args.at("path").template as<std::string>()), revision);
    });
    bindBusiness(manager, operations, "model.export_pcd", [&operations](const auto &args, auto revision) {
        operations.exportSampledModel(commandPath(args.at("path").template as<std::string>()), args.at("spacing").template as<double>(), revision);
    });
    bindBusiness(manager, operations, "file.convert", [&operations](const auto &args, auto) {
        operations.convertFile(commandPath(args.at("input").template as<std::string>()),
            commandPath(args.at("output").template as<std::string>()), args.at("spacing").template as<double>());
    });
    bindBusiness(manager, operations, "model.export_step", [&operations](const rm::Arguments &args, auto revision) {
        (void)operations.submitExport(commandPath(args.at("path").as<std::string>()), revision);
    });
    bindBusiness(manager, operations, "model.deviation",
         [&operations](const auto &, auto revision) { (void)operations.submitDeviation(revision); });
    bindBusiness(manager, operations, "project.set_reference_plane", [&operations](const auto &args, auto) {
        const auto values = commandJson(args.at("points"));
        std::array<Vec3, 3> points;
        for (std::size_t i = 0; i < points.size(); ++i)
            points[i] = {values[i][0].template get<double>(), values[i][1].template get<double>(),
                         values[i][2].template get<double>()};
        operations.setReferencePlane(points);
    });
    bindBusiness(manager, operations, "project.clear_reference_plane", [&operations](const auto &, auto) {
        operations.setReferencePlane({});
    });
    bindBusiness(manager, operations, "project.section", [&operations](const rm::Arguments &args, auto) {
        operations.setSection({args.at("enabled").as<bool>(), args.at("offset").as<double>(),
                              args.at("reverse").as<bool>(), args.at("plane").as<std::string>() == "bottom"
                                  ? SectionPlaneSource::Bottom : SectionPlaneSource::Reference});
    });
    manager.bindHandler("project.snapshot", [&operations](const auto &, const auto &) {
        return rm::CommandResult{rm::ResultKind::Success, {}, {}, commandValue(toJson(operations.project())), false};
    });
}
} // namespace reverse
