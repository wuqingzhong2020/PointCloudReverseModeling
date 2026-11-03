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
    return submitUiCommand(action == CloudHistoryAction::Undo   ? "cloud.undo"
                           : action == CloudHistoryAction::Redo ? "cloud.redo"
                                                                : "cloud.reset",
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
std::string CommandService::submitExportCloud(const std::filesystem::path &path, std::uint64_t revision)
{
    return submitUiCommand("cloud.export", {pathText(path)}, {}, revision);
}
std::string CommandService::submitDeviation(std::uint64_t revision)
{
    return submitUiCommand("model.deviation", {}, {}, revision);
}
} // namespace reverse
