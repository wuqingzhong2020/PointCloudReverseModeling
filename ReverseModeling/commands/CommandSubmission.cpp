#include "commands/PlateEditCommands.h"
/* 文件描述：现有 GUI 有类型入口转换为统一命令请求，不重复业务校验或另起线程。 */
#include "commands/CommandBindings.h"
#include "commands/CommandService.h"
namespace reverse
{
namespace
{
std::string pathText(const std::filesystem::path &path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
nlohmann::json vector(const Vec3 &v)
{
    return {v.x, v.y, v.z};
}
nlohmann::json rectangleJson(const CloudSelectionRect &r)
{
    return {
        {"viewProjection", r.viewProjection}, {"minX", r.minX}, {"minY", r.minY}, {"maxX", r.maxX}, {"maxY", r.maxY},
        {"zeroToOneDepth", r.zeroToOneDepth}};
}
std::string selectionModeText(CloudSelectionMode mode)
{
    return mode == CloudSelectionMode::Clear    ? "clear"
           : mode == CloudSelectionMode::Add    ? "add"
           : mode == CloudSelectionMode::Remove ? "subtract"
                                                : "replace";
}
} // namespace
std::string CommandService::submitLocalReconstruction(const LocalReconstruction &request, std::uint64_t revision)
{
    rm::Array parts;
    for (const auto &id : request.partIds) parts.emplace_back(id);
    return submitUiCommand("model.reconstruct_local", {}, {{"parts", parts}, {"type", request.type},
        {"contour", request.contour}, {"up", commandValue(vector(request.up))},
        {"tolerance", request.toleranceMm}, {"corner", request.cornerMm}}, revision);
}

std::string CommandService::submitImport(const std::filesystem::path &path, LengthUnit unit)
{
    return submitUiCommand("cloud.import", {pathText(path)},
                           {{"unit", unit == LengthUnit::Meter        ? "m"
                                     : unit == LengthUnit::Centimeter ? "cm"
                                                                      : "mm"}},
                           project().revision);
}
std::string CommandService::submitCloudSelection(const CloudSelectionRect &r, CloudSelectionMode mode,
                                                 std::uint64_t revision)
{
    if (mode == CloudSelectionMode::Clear)
    {
        return submitUiCommand("cloud.select_indices", {}, {{"indices", rm::Array{}}, {"mode", "clear"}}, revision);
    }
    return submitUiCommand("cloud.select", {},
                           {{"rectangle", commandValue(rectangleJson(r))}, {"mode", selectionModeText(mode)}},
                           revision);
}
std::string CommandService::submitCloudEdit(CloudEditMode mode, std::uint64_t revision)
{
    const auto snapshot = project();
    rm::Arguments options;
    auto initial = snapshot.selection;
    if (snapshot.selectionRecipe)
    {
        const auto &recipe = *snapshot.selectionRecipe;
        initial = recipe.initialIndices;
        if (!recipe.regions.empty())
        {
            rm::Array regions;
            for (const auto &region : recipe.regions)
            {
                regions.emplace_back(rm::Arguments{{"rectangle", commandValue(rectangleJson(region.rectangle))},
                                                   {"mode", selectionModeText(region.mode)}});
            }
            options.emplace("regions", std::move(regions));
        }
    }
    if ((initial && !initial->empty()) || options.empty())
    {
        rm::Array indices;
        if (initial)
        {
            indices.reserve(initial->size());
            for (const auto index : *initial)
            {
                indices.emplace_back(static_cast<std::uint64_t>(index));
            }
        }
        options.emplace("indices", std::move(indices));
    }
    return submitUiCommand(mode == CloudEditMode::KeepSelection ? "cloud.crop" : "cloud.delete", {}, std::move(options),
                           revision);
}
std::string CommandService::submitCloudHistory(CloudHistoryAction action, std::uint64_t revision)
{
    return submitProjectHistory(action, revision);
}
std::string CommandService::submitProjectHistory(CloudHistoryAction action, std::uint64_t revision)
{
    return submitUiCommand(action == CloudHistoryAction::Undo   ? "project.undo"
                           : action == CloudHistoryAction::Redo ? "project.redo"
                                                                : "project.reset",
                           {}, {}, revision);
}
std::string CommandService::submitReconstruct(const BeamParameters &p, std::uint64_t revision,
                                              std::optional<BeamFrame> frame)
{
    const nlohmann::json parameters{{"length", p.length},
                                    {"width", p.width},
                                    {"height", p.height},
                                    {"webThickness", p.webThickness},
                                    {"flangeThickness", p.flangeThickness},
                                    {"ribThickness", p.ribThickness},
                                    {"ribStations", p.ribStations},
                                    {"swapCrossSection", p.swapCrossSection},
                                    {"mirrorSymmetry", p.mirrorSymmetry}};
    rm::Arguments options{{"parameters", commandValue(parameters)}};
    if (frame)
    {
        options.emplace("frame", commandValue(nlohmann::json{{"originMm", vector(frame->originMm)},
                                                             {"xAxis", vector(frame->xAxis)},
                                                             {"yAxis", vector(frame->yAxis)},
                                                             {"zAxis", vector(frame->zAxis)}}));
    }
    return submitUiCommand("model.parametric", {}, std::move(options), revision);
}
std::string CommandService::submitRecognizedReconstruct(bool mirror, std::uint64_t revision, double tolerance)
{
    return submitUiCommand("model.reconstruct", {}, {{"mirror", mirror}, {"end_tolerance", tolerance}}, revision);
}
std::string CommandService::submitExport(const std::filesystem::path &path, std::uint64_t revision)
{
    return submitUiCommand("model.export_step", {pathText(path)}, {}, revision);
}
std::string CommandService::submitExportCwrm(const std::filesystem::path &path, std::uint64_t revision)
{
    return submitUiCommand("model.export_cwrm", {pathText(path)}, {}, revision);
}
std::string CommandService::submitExportModelCloud(const std::filesystem::path &path, double spacing, std::uint64_t revision)
{
    return submitUiCommand("model.export_pcd", {pathText(path)}, {{"spacing",spacing}}, revision);
}
std::string CommandService::submitExportCloud(const std::filesystem::path &path, std::uint64_t revision)
{
    return submitUiCommand("cloud.export", {pathText(path)}, {}, revision);
}
std::string CommandService::submitDeviation(std::uint64_t revision)
{
    return submitUiCommand("model.deviation", {}, {}, revision);
}
std::string CommandService::submitThickness(const PlateThicknessChange &request, std::uint64_t revision)
{
    rm::Array parts;
    for (const auto &id : request.partIds)
    {
        parts.emplace_back(id);
    }
    return submitUiCommand("model.set_thickness", {},
        {{"parts", std::move(parts)}, {"thickness", request.thicknessMm}}, revision);
}

std::string CommandService::submitContourExpansion(const PlateContourExpansion &request, std::uint64_t revision)
{
    rm::Array parts;
    for (const auto &id : request.partIds)
    {
        parts.emplace_back(id);
    }
    return submitUiCommand("model.expand_contour", {},
        {{"parts", std::move(parts)}, {"distance", request.distanceMm}}, revision);
}

std::string CommandService::submitMirrorCompletion(const MirrorCompletion &request, std::uint64_t revision)
{
    rm::Array parts;
    for (const auto &id : request.partIds) parts.emplace_back(id);
    return submitUiCommand("model.mirror_complete", {},
        {{"parts", std::move(parts)}, {"plane_part", request.planePartId}}, revision);
}

std::string CommandService::submitPlateGeneration(const PlateGeneration &r, std::uint64_t revision)
{
    rm::Arguments args{{"part", r.partId}, {"offset", r.offsetMm}, {"thickness", r.thicknessMm}};
    if (r.kind == PlateGenerationKind::Cover)
    {
        args.emplace("face", static_cast<std::int64_t>(r.faceIndex));
        args.emplace("reverse", r.reversed);
        return submitUiCommand("model.generate_cover_plate", {}, std::move(args), revision);
    }
    args.emplace("up", commandValue(vector(r.up)));
    args.emplace("mode", r.baseMode == BasePlateMode::Horizontal ? "horizontal" : "vertical_pair");
    if (!r.secondPartId.empty())
    {
        args.emplace("second_part", r.secondPartId);
    }
    return submitUiCommand("model.generate_base_plate", {}, std::move(args), revision);
}

std::string CommandService::submitHole(const PlateHole &request, std::uint64_t revision)
{
    rm::Arguments args{{"part", request.partId}, {"edge", static_cast<std::int64_t>(request.bottomEdge)},
        {"kind", request.kind == PlateHoleKind::Weld ? "weld" : "bottom"},
        {"shape", request.shape == PlateHoleShape::Circle ? "circle" : request.shape == PlateHoleShape::Triangle ? "triangle" : "rectangle"}};
    if (request.shape == PlateHoleShape::Rectangle)
    {
        args.emplace("width", request.widthMm); args.emplace("height", request.heightMm);
    }
    else args.emplace("size", request.sizeMm);
    if (request.kind == PlateHoleKind::Weld)
    {
        args.emplace("first", request.first); args.emplace("second", request.second);
    }
    else
    {
        args.emplace("reference", request.reference == HoleReference::Start ? "start" : request.reference == HoleReference::End ? "end" : "middle");
        args.emplace("offset", request.offsetMm);
    }
    return submitUiCommand("model.add_hole", {}, std::move(args), revision);
}

std::string CommandService::submitPlateEdit(const PlateEdit &request,std::uint64_t revision)
{
    return submitUiCommand(plateEditCommand(request.operation),{},plateEditArguments(request),revision);
}
std::string CommandService::submitEdgeTreatment(const EdgeTreatment &request, std::uint64_t revision)
{
    rm::Arguments options{{"size", request.sizeMm}};
    rm::Array targets;
    for (const auto &target : request.targets)
    {
        rm::Array edges;
        for (const auto edge : target.edges) edges.emplace_back(static_cast<std::int64_t>(edge));
        targets.emplace_back(rm::Arguments{{"part", target.partId}, {"edges", std::move(edges)}});
    }
    if (targets.size() == 1)
    {
        for (auto &[key, value] : targets.front().as<rm::Arguments>()) options.emplace(key, value);
    }
    else options.emplace("targets", std::move(targets));
    return submitUiCommand(request.kind == EdgeTreatmentKind::Fillet ? "model.fillet" : "model.chamfer",
        {},std::move(options),revision);
}
std::string CommandService::submitModelTransform(const std::vector<std::string> &partIds,
    const ModelTransform &request, std::uint64_t revision)
{
    (void)modelTransformFrame(request);
    rm::Array ids;
    for (const auto &id : partIds) ids.emplace_back(id);
    rm::Arguments options{{"parts", std::move(ids)}};
    if (request.kind == ModelTransformKind::Move)
    {
        options.emplace("offset", commandValue(vector(request.offsetMm)));
        return submitUiCommand("model.move", {}, std::move(options), revision);
    }
    options.emplace("axis", request.axis == RotationAxis::X ? "x" : request.axis == RotationAxis::Y ? "y" : "z");
    options.emplace("angle", request.angleDegrees);
    options.emplace("center", commandValue(vector(request.centerMm)));
    return submitUiCommand("model.rotate", {}, std::move(options), revision);
}
std::string CommandService::submitDeleteModelParts(const std::vector<std::string> &partIds, std::uint64_t revision)
{
    rm::Array ids;
    for (const auto &id : partIds) ids.emplace_back(id);
    return submitUiCommand("model.delete", {}, {{"parts", std::move(ids)}}, revision);
}
std::string CommandService::submitReferencePlane(std::optional<std::array<Vec3, 3>> points, std::uint64_t revision)
{
    if (!points) return submitUiCommand("project.clear_reference_plane", {}, {}, revision);
    nlohmann::json values = nlohmann::json::array();
    for (const auto &p : *points) values.push_back(vector(p));
    return submitUiCommand("project.set_reference_plane", {}, {{"points", commandValue(values)}}, revision);
}
std::string CommandService::submitSection(PlaneSection section, std::uint64_t revision)
{
    return submitUiCommand("project.section", {}, {{"enabled", section.enabled}, {"offset", section.offsetMm},
        {"reverse", section.reversed}}, revision);
}
} // namespace reverse
