#include "commands/PlateEditCommands.h"
/*
 * 文件描述：
 * 手写同步几何业务，通过不可变快照提交结果；由固定命令工作线程调用。
 */
#include "commands/ProjectOperations.h"
#include "commands/ProjectHistory.h"
#include "commands/SectionPlane.h"
#include "commands/TaskOutput.h"
#include "geometry/CloudEditing.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
#include "geometry/ReferencePlane.h"
#include <cctype>
#include <cmath>

#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <type_traits>

namespace reverse
{
namespace
{
std::string utf8Path(const std::filesystem::path &path)
{
    const auto text = path.u8string();
    return {reinterpret_cast<const char *>(text.data()), text.size()};
}
std::string sessionId()
{
    std::random_device source;
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i)
        text << std::setw(8) << source();
    return text.str();
}
void checkCancelled(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "Task cancelled before committing results.");
}

void analyzeCloud(ProjectSnapshot &snapshot, std::stop_token stop, const ProgressCallback &progress)
{
    if (snapshot.cloud->points.empty())
    {
        snapshot.warnings.emplace_back("点云已清空，可撤销或重置恢复。");
        return;
    }
    try
    {
        snapshot.estimate = estimateBeam(*snapshot.cloud, stop, [&](double p, auto m) { progress(p * 0.4, m); });
    }
    catch (const GeometryError &error)
    {
        if (error.code() == "cancelled")
        {
            throw;
        }
        snapshot.warnings.emplace_back(std::string("点云框架不可用：") + error.what());
    }
    if (snapshot.estimate)
    {
        try
        {
            snapshot.recognition = recognizePlates(*snapshot.cloud, *snapshot.estimate, stop,
                                                   [&](double p, auto m) { progress(0.4 + p * 0.6, m); });
            snapshot.warnings.insert(snapshot.warnings.end(), snapshot.recognition->warnings.begin(),
                                     snapshot.recognition->warnings.end());
        }
        catch (const GeometryError &error)
        {
            if (error.code() == "cancelled")
            {
                throw;
            }
            snapshot.warnings.emplace_back(std::string("板件识别未完成：") + error.what());
        }
    }
}
} // namespace

struct ProjectOperations::Impl
{
    mutable std::mutex stateMutex;
    ProjectSnapshot project;
    std::shared_ptr<const Model> model;
    ProjectHistory history;
    rm::CommandContext context;
    const std::string session{sessionId()};

    void commitEdit(ProjectSnapshot next, std::shared_ptr<const Model> geometry, std::string label)
    {
        static_assert(std::is_nothrow_move_assignable_v<ProjectSnapshot>);
        next.historyRestoreRevision = project.historyRestoreRevision;
        next.selection.reset();
        next.selectionRecipe.reset();
        next.deviation.reset();
        next.lastExport.reset();
        next.lastCloudExport.reset();
        next.exportedPathUtf8.clear();
        next.exportedCloudPathUtf8.clear();
        if (next.section.enabled && (!next.cloud || !selectedSectionPlane(next, next.section.source)))
            next.section.enabled = false;
        history.push(ProjectContent::capture(project, model), ProjectContent::capture(next, geometry), std::move(label));
        project = std::move(next);
        model = std::move(geometry);
    }

    using Work = std::function<void(std::stop_token, const ProgressCallback &)>;
    std::string launch(std::string, std::uint64_t expectedRevision, Work work)
    {
        {
            std::lock_guard lock(stateMutex);
            if (project.revision != expectedRevision)
            {
                throw GeometryError("stale_revision", "工程已变化，请刷新后重试。");
            }
        }
        checkCancelled(context.stop);
        const ProgressCallback progress = [this](double value, std::string_view message) {
            checkCancelled(context.stop);
            if (context.progress)
            {
                context.progress(value, message);
            }
        };
        work(context.stop, progress);
        return {};
    }
};

ProjectOperations::ProjectOperations() : impl_(std::make_unique<Impl>())
{
}
ProjectOperations::~ProjectOperations() = default;

void ProjectOperations::setReferencePlane(std::optional<std::array<Vec3, 3>> points)
{
    auto plane = points ? std::make_shared<const ReferencePlane>(makeReferencePlane(*points)) : nullptr;
    if (plane) (void)sectionEquation(*plane, {});
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    if (plane && !impl_->project.cloud && !impl_->project.model)
        throw GeometryError("no_geometry", "请先导入点云或模型，再设置基准面。");
    auto next = impl_->project;
    next.referencePlane = std::move(plane);
    next.section = {};
    if (ProjectContent::capture(next, impl_->model).sameContent(ProjectContent::capture(impl_->project, impl_->model)))
    {
        if (impl_->project.section != PlaneSection{})
        {
            impl_->project.section = {};
            ++impl_->project.revision;
        }
        return;
    }
    ++next.revision;
    impl_->commitEdit(std::move(next), impl_->model, points ? "设置基准面" : "清除基准面");
}

void ProjectOperations::setSection(PlaneSection section)
{
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    const auto plane = selectedSectionPlane(impl_->project, section.source);
    if (section.enabled && !plane)
        throw GeometryError(section.source == SectionPlaneSource::Bottom ? "no_bottom_plane" : "no_reference_plane",
            section.source == SectionPlaneSource::Bottom ? "尚未获得底板面，请先导入可识别的点云或设置基准面。"
                                                        : "请先设置基准面，或在剖视下拉菜单中选择底板面。");
    if (!std::isfinite(section.offsetMm))
        throw GeometryError("invalid_section", "剖视偏移必须为有限的毫米数值。");
    if (plane) (void)sectionEquation(*plane, section);
    if (section.enabled && !impl_->project.cloud)
        throw GeometryError("no_cloud", "剖视只过滤点云显示；当前没有点云。");
    if (impl_->project.section == section) return;
    impl_->project.section = section;
    ++impl_->project.revision;
}

std::string ProjectOperations::submitImport(const std::filesystem::path &path)
{
    const auto version = project().revision;
    const auto absolutePath = std::filesystem::absolute(path);
    auto *state = impl_.get();
    auto extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".pcd" && extension != ".step" && extension != ".stp" && extension != ".cwrm")
        throw GeometryError("unsupported_format", "支持导入 PCD、STEP/STP 和 CWRM 文件");
    if (extension != ".pcd")
    {
        return state->launch("ImportModel", version, [state, absolutePath](auto stop, const auto &progress) {
            auto model = loadCadModel(absolutePath, stop, progress);
            ProjectSnapshot next;
            next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
            next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
            next.sourcePathUtf8 = utf8Path(absolutePath);
            next.warnings = model->summary().warnings;
            checkCancelled(stop);
            std::lock_guard lock(state->stateMutex);
            next.revision = state->project.revision + 1;
            next.modelId = state->session + "-model-" + std::to_string(next.revision);
            state->history.establishBaseline(ProjectContent::capture(next, model));
            state->project = std::move(next);
            state->model = std::move(model);
        });
    }
    return state->launch("ImportPointCloud", version, [state, absolutePath](auto stop, const auto &progress) {
        auto cloud = loadPointCloud(absolutePath, LengthUnit::Millimeter, stop,
                                   [&](double p, auto m) { progress(p * 0.65, m); });
        ProjectSnapshot next;
        next.cloud = std::move(cloud);
        analyzeCloud(next, stop, [&](double p, auto m) { progress(0.65 + p * 0.35, m); });
        checkCancelled(stop);
        next.sourcePathUtf8 = utf8Path(absolutePath);
        std::lock_guard lock(state->stateMutex);
        next.revision = state->project.revision + 1;
        next.cloudId = state->session + "-cloud-" + std::to_string(next.revision);
        state->history.establishBaseline(ProjectContent::capture(next, {}));
        state->project = std::move(next);
        state->model.reset();
    });
}

std::string ProjectOperations::submitCloudSelection(const CloudSelectionRect &rectangle, CloudSelectionMode mode,
                                                    std::uint64_t expectedRevision, bool editLast)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
    {
        throw GeometryError("stale_revision", "工程已变化，请重新框选。");
    }
    if (!snapshot.cloud)
    {
        throw GeometryError("no_cloud", "请先导入点云。");
    }
    if (editLast && (!snapshot.selectionRecipe || snapshot.selectionRecipe->regions.empty() ||
                     mode == CloudSelectionMode::Clear || snapshot.selectionRecipe->regions.back().mode != mode))
    {
        throw GeometryError("invalid_selection", "原框选已失效或选择方式已变化，请重新框选。");
    }
    auto *state = impl_.get();
    return state->launch(
        "SelectCloud", expectedRevision, [state, snapshot, rectangle, mode, editLast](auto stop, const auto &progress) {
            std::shared_ptr<const CloudSelection> selected;
            auto recipe = std::make_shared<CloudSelectionRecipe>();
            if (editLast)
            {
                *recipe = *snapshot.selectionRecipe;
                recipe->regions.back().rectangle = rectangle;
                auto resolved = recipe->initialIndices ? *recipe->initialIndices : CloudSelection{};
                for (std::size_t i = 0; i < recipe->regions.size(); ++i)
                {
                    const auto &region = recipe->regions[i];
                    resolved = selectCloudPoints(*snapshot.cloud, region.rectangle, region.mode, resolved, stop,
                        [&](double p, auto message) { progress((i + p) / recipe->regions.size(), message); });
                }
                selected = std::make_shared<const CloudSelection>(std::move(resolved));
            }
            else
            {
                selected = std::make_shared<const CloudSelection>(selectCloudPoints(
                    *snapshot.cloud, rectangle, mode,
                    snapshot.selection ? std::span<const std::size_t>(*snapshot.selection) : std::span<const std::size_t>{},
                    stop, progress));
                if (mode == CloudSelectionMode::Add || mode == CloudSelectionMode::Remove)
                {
                    if (snapshot.selectionRecipe)
                        *recipe = *snapshot.selectionRecipe;
                    else
                        recipe->initialIndices = snapshot.selection;
                }
                if (mode != CloudSelectionMode::Clear)
                    recipe->regions.push_back({rectangle, mode});
            }
            checkCancelled(stop);
            std::lock_guard lock(state->stateMutex);
            state->project.selection = std::move(selected);
            state->project.selectionRecipe = std::move(recipe);
            ++state->project.revision;
        });
}

std::string ProjectOperations::submitCloudEdit(CloudEditMode mode, std::uint64_t expectedRevision,
                                               std::optional<CloudSelectionRecipe> selectionOverride)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
    {
        throw GeometryError("stale_revision", "工程已变化，请重新框选。");
    }
    if (!snapshot.cloud)
    {
        throw GeometryError("no_cloud", "请先导入点云。");
    }
    if (!selectionOverride && (!snapshot.selection || snapshot.selection->empty()))
    {
        throw GeometryError("empty_selection", "请先框选需要处理的点云。");
    }
    auto *state = impl_.get();
    return state->launch(
        mode == CloudEditMode::KeepSelection ? "CropCloud" : "DeleteCloud", expectedRevision,
        [state, snapshot, mode, selectionOverride = std::move(selectionOverride)](auto stop, const auto &progress) {
            // Resolve explicit regions locally. Failure/cancellation must not change live selection.
            auto selection = snapshot.selection;
            if (selectionOverride)
            {
                auto resolved =
                    selectionOverride->initialIndices ? *selectionOverride->initialIndices : CloudSelection{};
                if (!resolved.empty() && resolved.back() >= snapshot.cloud->points.size())
                {
                    throw GeometryError("invalid_index", "参数 --indices 超出当前点云范围。");
                }
                const auto &regions = selectionOverride->regions;
                for (std::size_t i = 0; i < regions.size(); ++i)
                {
                    const auto &region = regions[i];
                    resolved =
                        selectCloudPoints(*snapshot.cloud, region.rectangle, region.mode, resolved, stop,
                                          [&](double p, auto m) { progress(0.1 * (i + p) / regions.size(), m); });
                }
                selection = std::make_shared<const CloudSelection>(std::move(resolved));
            }
            if (!selection || selection->empty())
            {
                throw GeometryError("empty_selection", "给定选区没有选中点云，未执行编辑。");
            }
            checkCancelled(stop);
            // 全选后截取不改变数据，也不丢弃已生成模型和重做记录。
            if (mode == CloudEditMode::KeepSelection && selection->size() == snapshot.cloud->points.size())
            {
                return;
            }
            ProjectSnapshot next = snapshot;
            next.estimate.reset();
            next.recognition.reset();
            next.warnings.clear();
            next.cloud = editCloudPoints(*snapshot.cloud, *selection, mode, stop,
                                         [&](double p, auto m) { progress(0.1 + p * 0.15, m); });
            next.sourcePathUtf8 = snapshot.sourcePathUtf8;
            next.referencePlane = snapshot.referencePlane;
            next.section = snapshot.section;
            analyzeCloud(next, stop, [&](double p, auto m) { progress(0.25 + p * 0.75, m); });
            if (snapshot.model)
            {
                next.warnings.push_back("点云已更新，已有模型保持不变；可选择部件进行局部重建。");
            }
            checkCancelled(stop);
            std::lock_guard lock(state->stateMutex);
            next.revision = state->project.revision + 1;
            next.cloudId = state->session + "-cloud-" + std::to_string(next.revision);
            state->commitEdit(std::move(next), state->model, mode == CloudEditMode::KeepSelection ? "截取点云" : "删除点云");
        });
}

std::string ProjectOperations::submitCloudHistory(CloudHistoryAction action, std::uint64_t expectedRevision)
{
    auto *state = impl_.get();
    return state->launch("ProjectHistory", expectedRevision, [state, action](auto stop, const auto &) {
        std::lock_guard lock(state->stateMutex);
        checkCancelled(stop);
        const bool reset = action == CloudHistoryAction::Reset;
        if (!reset && action != CloudHistoryAction::Undo && action != CloudHistoryAction::Redo)
            throw GeometryError("invalid_edit", "未知工程历史操作。");
        const auto &content = reset ? state->history.baseline()
            : action == CloudHistoryAction::Undo ? state->history.undoContent() : state->history.redoContent();
        if (reset && content.sameContent(ProjectContent::capture(state->project, state->model)))
        {
            // 只清理临时显示/选区，不消费历史或截断重做分支。
            if (state->project.section != PlaneSection{} || state->project.selection || state->project.selectionRecipe)
            {
                state->project.section = {};
                state->project.selection.reset();
                state->project.selectionRecipe.reset();
                state->project.historyRestoreRevision = ++state->project.revision;
            }
            return;
        }
        auto restored = content.restore(state->project, reset);
        auto model = content.geometry;
        if (reset)
            state->history.push(ProjectContent::capture(state->project, state->model), content, "重置到导入状态");
        else if (action == CloudHistoryAction::Undo) state->history.undo();
        else state->history.redo();
        state->project = std::move(restored);
        state->model = std::move(model);
    });
}

std::string ProjectOperations::submitReconstruct(const BeamParameters &parameters, std::uint64_t expectedRevision,
                                                 std::optional<BeamFrame> frameOverride)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
        throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
    if ((!snapshot.cloud || snapshot.cloud->points.empty()) && !frameOverride)
    {
        throw GeometryError("no_cloud", "Import a nonempty point cloud before reconstruction.");
    }
    if (!snapshot.estimate && !frameOverride)
        throw GeometryError("no_frame", "Provide an explicit frame; this cloud has no usable PCA initial frame.");
    const auto frame = frameOverride ? *frameOverride : snapshot.estimate->frame;
    auto *state = impl_.get();
    return state->launch("Reconstruct", expectedRevision, [state, parameters, frame](auto stop, const auto &progress) {
        auto model = buildBeam(frame, parameters, stop, progress);
        checkCancelled(stop);
        std::lock_guard lock(state->stateMutex);
        auto next = state->project;
        ++next.revision;
        next.modelId = state->session + "-model-" + std::to_string(next.revision);
        next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
        next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
        next.deviation.reset();
        next.lastExport.reset();
        next.exportedPathUtf8.clear();
        next.committedParameters = parameters;
        next.committedRecognitionOptions.reset();
        state->commitEdit(std::move(next), std::move(model), "手动参数建模");
    });
}

std::string ProjectOperations::submitRecognizedReconstruct(bool mirrorSymmetry, std::uint64_t expectedRevision,
                                                           double endOverhangToleranceMm, bool recognizeWebHoles, double minimumHoleRadiusMm)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
    {
        throw GeometryError("stale_revision", "工程已变化，请刷新后重建。");
    }
    if (!snapshot.recognition)
    {
        throw GeometryError("no_recognition", "点云没有可用的板件识别结果，不使用默认板厚补建。");
    }
    auto *state = impl_.get();
    return state->launch(
        "ReconstructRecognized", expectedRevision,
        [state, cloud = snapshot.cloud, recognition = *snapshot.recognition,
         options = PlateBuildOptions{mirrorSymmetry, endOverhangToleranceMm, recognizeWebHoles, minimumHoleRadiusMm}](auto stop, const auto &progress) {
            auto model = buildRecognizedPlates(recognition, options, stop, progress);
            if (cloud) model = associateModelCloud(*model, *cloud, stop);
            checkCancelled(stop);
            std::lock_guard lock(state->stateMutex);
            auto next = state->project;
            ++next.revision;
            next.modelId = state->session + "-model-" + std::to_string(next.revision);
            next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
            next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
            next.deviation.reset();
            next.lastExport.reset();
            next.exportedPathUtf8.clear();
            next.committedParameters.reset();
            next.committedRecognitionOptions = options;
            state->commitEdit(std::move(next), std::move(model), "重建模型");
        });
}

PlateEdit ProjectOperations::prepareDirectPlateEdit(const PlateEdit &request, bool all, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision)
            throw GeometryError("stale_revision", "工程已变化，请重新选择模型。");
        source = impl_->model;
    }
    if (!source)
        throw GeometryError("no_model", "当前没有模型。");
    return reverse::prepareDirectPlateEdit(*source, request, all, impl_->context.stop, impl_->context.output);
}

PlateEditResult ProjectOperations::previewPlateEdit(const PlateEdit &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    { std::lock_guard lock(impl_->stateMutex);
      if (impl_->project.revision!=revision) throw GeometryError("stale_revision","工程已变化，请重新预览。");
      source=impl_->model; }
    if (!source) throw GeometryError("no_model","当前没有模型。");
    return editModelPlates(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyPlateEdit(const PlateEdit &request,std::uint64_t revision)
{
    auto result=previewPlateEdit(request,revision);
    for (const auto &line:result.reports) if (impl_->context.output) impl_->context.output(line+"\n");
    if (!result.changed) return;
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    if (impl_->project.revision!=revision) throw GeometryError("stale_revision","工程已变化，整批未提交。");
    auto next=impl_->project;++next.revision;
    next.modelId=impl_->session+"-model-"+std::to_string(next.revision);
    next.model=std::shared_ptr<const ModelSummary>(result.model,&result.model->summary());
    next.mesh=std::shared_ptr<const MeshData>(result.model,&result.model->mesh());
    impl_->commitEdit(std::move(next),std::move(result.model),plateEditTitle(request.operation));
}

std::shared_ptr<const Model> ProjectOperations::previewLocalReconstruction(const LocalReconstruction &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    std::shared_ptr<const CloudData> cloud;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
        cloud = impl_->project.cloud;
    }
    if (!source || !cloud) throw GeometryError("no_local_source", "局部重建需要点云及其重建模型；单独导入的 CAD 没有对应点云。");
    return reconstructLocalParts(*source, *cloud, request, impl_->context.stop, impl_->context.progress);
}
void ProjectOperations::applyLocalReconstruction(const LocalReconstruction &request, std::uint64_t revision)
{
    auto model = previewLocalReconstruction(request, revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，局部重建未提交。");
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
    impl_->commitEdit(std::move(next), std::move(model), "局部重建");
}

std::shared_ptr<const Model> ProjectOperations::previewEdgeTreatment(const EdgeTreatment &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return treatModelEdges(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyEdgeTreatment(const EdgeTreatment &request, std::uint64_t revision)
{
    auto model = previewEdgeTreatment(request,revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model,&model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model,&model->mesh());
    impl_->commitEdit(std::move(next),std::move(model), "圆角 / 倒角");
}

std::map<std::string, PlateThicknessInfo> ProjectOperations::inspectThickness(
    const std::vector<std::string> &parts, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision)
        {
            throw GeometryError("stale_revision", "工程已变化，请重新选择板件。");
        }
        source = impl_->model;
    }
    if (!source)
    {
        throw GeometryError("no_model", "当前没有模型。");
    }
    return inspectModelThickness(*source, parts, impl_->context.stop);
}

std::shared_ptr<const Model> ProjectOperations::previewThickness(const PlateThicknessChange &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return changeModelThickness(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyThickness(const PlateThicknessChange &request, std::uint64_t revision)
{
    auto model = previewThickness(request,revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model,&model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model,&model->mesh());
    impl_->commitEdit(std::move(next),std::move(model), "修改板厚");
}

std::shared_ptr<const Model> ProjectOperations::previewContourExpansion(const PlateContourExpansion &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return expandModelContours(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyContourExpansion(const PlateContourExpansion &request, std::uint64_t revision)
{
    auto model = previewContourExpansion(request,revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model,&model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model,&model->mesh());
    impl_->commitEdit(std::move(next),std::move(model), "外扩板件轮廓");
}

std::shared_ptr<const Model> ProjectOperations::previewMirrorCompletion(const MirrorCompletion &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return completeModelMirror(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyMirrorCompletion(const MirrorCompletion &request, std::uint64_t revision)
{
    auto model = previewMirrorCompletion(request,revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model,&model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model,&model->mesh());
    impl_->commitEdit(std::move(next),std::move(model), "镜像补全");
}

std::shared_ptr<const Model> ProjectOperations::previewPlateGeneration(const PlateGeneration &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return generateModelPlate(*source, request, impl_->context.stop, impl_->context.progress);
}
void ProjectOperations::applyPlateGeneration(const PlateGeneration &request, std::uint64_t revision)
{
    auto model = previewPlateGeneration(request, revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
    impl_->commitEdit(std::move(next), std::move(model),
        request.kind == PlateGenerationKind::Cover ? "生成外扩贴板" : "生成下底板");
}

std::vector<PlateHoleBase> ProjectOperations::inspectHoleBases(const std::string &partId, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新选择板件。");
        source = impl_->model;
    }
    checkCancelled(impl_->context.stop);
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return inspectModelHoleBases(*source, partId);
}

std::shared_ptr<const Model> ProjectOperations::previewHole(const PlateHole &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision) throw GeometryError("stale_revision", "工程已变化，请重新预览。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有模型。");
    return addModelHole(*source,request,impl_->context.stop,impl_->context.progress);
}
void ProjectOperations::applyHole(const PlateHole &request, std::uint64_t revision)
{
    auto model = previewHole(request,revision);
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    auto next = impl_->project;
    ++next.revision;
    next.modelId = impl_->session + "-model-" + std::to_string(next.revision);
    next.model = std::shared_ptr<const ModelSummary>(model,&model->summary());
    next.mesh = std::shared_ptr<const MeshData>(model,&model->mesh());
    impl_->commitEdit(std::move(next),std::move(model), "添加孔");
}

std::string ProjectOperations::submitModelTransform(const std::vector<std::string> &partIds,
    const ModelTransform &request, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision)
            throw GeometryError("stale_revision", "工程已变化，请重新选择待变换部件。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有可变换的模型。");
    auto *state = impl_.get();
    return state->launch("TransformModelParts", revision, [state, source, partIds, request](auto stop, const auto &progress) {
        auto model = transformModelParts(*source, partIds, request, stop, progress);
        checkCancelled(stop);
        if (isIdentityTransform(request)) return;
        std::lock_guard lock(state->stateMutex);
        auto next = state->project;
        ++next.revision;
        next.modelId = state->session + "-model-" + std::to_string(next.revision);
        next.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
        next.mesh = std::shared_ptr<const MeshData>(model, &model->mesh());
        next.deviation.reset();
        next.lastExport.reset();
        next.exportedPathUtf8.clear();
        state->commitEdit(std::move(next), std::move(model),
            request.kind == ModelTransformKind::Move ? "移动模型部件" : "旋转模型部件");
    });
}

std::string ProjectOperations::submitDeleteModelParts(const std::vector<std::string> &partIds, std::uint64_t revision)
{
    std::shared_ptr<const Model> source;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != revision)
            throw GeometryError("stale_revision", "工程已变化，请重新选择待删除部件。");
        source = impl_->model;
    }
    if (!source) throw GeometryError("no_model", "当前没有可删除的模型。");
    auto *state = impl_.get();
    return state->launch("DeleteModelParts", revision, [state, source, partIds](auto stop, const auto &progress) {
        auto model = deleteModelParts(*source, partIds, stop, progress);
        checkCancelled(stop);
        std::lock_guard lock(state->stateMutex);
        auto next = state->project;
        ++next.revision;
        next.modelId = model ? state->session + "-model-" + std::to_string(next.revision) : std::string{};
        next.model = model ? std::shared_ptr<const ModelSummary>(model, &model->summary()) : nullptr;
        next.mesh = model ? std::shared_ptr<const MeshData>(model, &model->mesh()) : nullptr;
        next.deviation.reset();
        next.lastExport.reset();
        next.exportedPathUtf8.clear();
        state->commitEdit(std::move(next), std::move(model),
                          "删除模型（" + std::to_string(partIds.size()) + " 个部件）");
    });
}

std::string ProjectOperations::submitExport(const std::filesystem::path &path, std::uint64_t expectedRevision)
{
    std::shared_ptr<const Model> model;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != expectedRevision)
            throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
        model = impl_->model;
    }
    if (!model)
        throw GeometryError("no_model", "Reconstruct a model before exporting STEP.");
    const auto absolutePath = std::filesystem::absolute(path);
    auto *state = impl_.get();
    return state->launch("ExportStep", expectedRevision, [state, model, absolutePath](auto stop, const auto &progress) {
        const auto result = exportStep(*model, absolutePath, stop, progress);
        std::lock_guard lock(state->stateMutex);
        state->project.lastExport = result;
        state->project.exportedPathUtf8 = utf8Path(absolutePath);
    });
}

void ProjectOperations::exportCwrmModel(const std::filesystem::path &path, std::uint64_t revision)
{
    std::shared_ptr<const Model> model;
    { std::lock_guard lock(impl_->stateMutex); model = impl_->model; }
    if (!model) throw GeometryError("no_model", "请先导入模型或完成点云重建，再导出 CWRM");
    const auto target = std::filesystem::absolute(path);
    (void)impl_->launch("ExportCwrm", revision, [&](auto stop, const auto &progress) {
        const auto result = exportCwrm(*model, target, stop, progress);
        std::lock_guard lock(impl_->stateMutex);
        impl_->project.lastExport = result;
        impl_->project.exportedPathUtf8 = utf8Path(target);
    });
}
void ProjectOperations::exportSampledModel(const std::filesystem::path &path, double spacing, std::uint64_t revision)
{
    std::shared_ptr<const Model> model;
    { std::lock_guard lock(impl_->stateMutex); model = impl_->model; }
    if (!model) throw GeometryError("no_model", "请先导入或重建模型，再采样导出 PCD");
    const auto target = std::filesystem::absolute(path);
    (void)impl_->launch("SampleModel", revision, [&](auto stop, const auto &progress) {
        const auto cloud = sampleModelSurface(*model, spacing, stop, [&](double p, auto m) { progress(p*0.5,m); });
        const auto result = exportPointCloud(*cloud, target, stop, [&](double p, auto m) { progress(0.5+p*0.5,m); });
        std::lock_guard lock(impl_->stateMutex);
        impl_->project.lastCloudExport = result;
        impl_->project.exportedCloudPathUtf8 = utf8Path(target);
    });
}
void ProjectOperations::convertFile(const std::filesystem::path &input, const std::filesystem::path &output, double spacing)
{
    const auto result = convertModelFile(input, output, spacing, impl_->context.stop, impl_->context.progress);
    if (impl_->context.output)
    {
        std::string message = result.inputFormat + " -> " + result.outputFormat + "；" +
            std::to_string(result.fileBytes) + " 字节；文件：" + utf8Path(std::filesystem::absolute(output));
        if (result.outputFormat == "pcd") message += "；模型表面采样 " + std::to_string(result.pointCount) + " 点，非原始扫描点云";
        impl_->context.output(message);
    }
}

void ProjectOperations::exportTaskOutput(TaskOutput &output, std::uint64_t expectedRevision)
{
    std::shared_ptr<const Model> model;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != expectedRevision)
        {
            throw GeometryError("stale_revision", "工程已变化，请刷新后重试。");
        }
        model = impl_->model;
    }
    if (!model)
    {
        throw GeometryError("no_model", "当前没有可导出的模型。");
    }
    (void)impl_->launch("ExportAndExit", expectedRevision, [&](auto stop, const auto &progress) {
        const auto result = output.deliver([&](const auto &path) {
            return exportStep(*model, path, stop, progress);
        }, stop);
        std::lock_guard lock(impl_->stateMutex);
        impl_->project.lastExport = result;
        impl_->project.exportedPathUtf8 = utf8Path(output.modelPath());
    });
}

std::string ProjectOperations::submitExportCloud(const std::filesystem::path &path, std::uint64_t expectedRevision)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
        throw GeometryError("stale_revision", "工程已变化，请刷新后重试");
    if (!snapshot.cloud)
        throw GeometryError("no_cloud", "当前没有可导出的点云");
    const auto absolutePath = std::filesystem::absolute(path);
    auto *state = impl_.get();
    return state->launch("ExportPointCloud", expectedRevision,
                         [state, snapshot, absolutePath](auto stop, const auto &progress) {
                             const auto result = exportPointCloud(*snapshot.cloud, absolutePath, stop, progress);
                             std::lock_guard lock(state->stateMutex);
                             state->project.lastCloudExport = result;
                             state->project.exportedCloudPathUtf8 = utf8Path(absolutePath);
                         });
}

std::string ProjectOperations::submitDeviation(std::uint64_t expectedRevision)
{
    std::shared_ptr<const Model> model;
    std::shared_ptr<const CloudData> cloud;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != expectedRevision)
            throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
        model = impl_->model;
        cloud = impl_->project.cloud;
    }
    if (!model || !cloud)
        throw GeometryError("no_model", "A cloud and reconstructed model are required.");
    auto *state = impl_.get();
    return state->launch("EvaluateDeviation", expectedRevision, [state, model, cloud](auto stop, const auto &progress) {
        auto result = std::make_shared<const DeviationResult>(evaluateDeviation(*cloud, *model, stop, progress));
        checkCancelled(stop);
        std::lock_guard lock(state->stateMutex);
        state->project.deviation = std::move(result);
    });
}

void ProjectOperations::setContext(const rm::CommandContext &context)
{
    impl_->context = context;
}

void ProjectOperations::selectIndices(const rm::Array &indices, CloudSelectionMode mode)
{
    const auto snapshot = project();
    if (!snapshot.cloud)
    {
        throw GeometryError("no_cloud", "请先导入点云。");
    }
    std::set<std::size_t> selected;
    if (mode != CloudSelectionMode::Replace && mode != CloudSelectionMode::Clear && snapshot.selection)
    {
        selected.insert(snapshot.selection->begin(), snapshot.selection->end());
    }
    for (const auto &value : indices)
    {
        if (mode == CloudSelectionMode::Clear)
        {
            break;
        }
        checkCancelled(impl_->context.stop);
        const auto index = value.as<std::uint64_t>();
        if (index >= snapshot.cloud->points.size())
        {
            throw GeometryError("invalid_index", "点索引超出当前点云范围。");
        }
        if (mode == CloudSelectionMode::Remove)
        {
            selected.erase(static_cast<std::size_t>(index));
        }
        else
        {
            selected.insert(static_cast<std::size_t>(index));
        }
    }
    auto result = std::make_shared<const CloudSelection>(selected.begin(), selected.end());
    auto recipe = std::make_shared<CloudSelectionRecipe>();
    recipe->initialIndices = result;
    checkCancelled(impl_->context.stop);
    std::lock_guard lock(impl_->stateMutex);
    impl_->project.selection = std::move(result);
    impl_->project.selectionRecipe = std::move(recipe);
    ++impl_->project.revision;
}

ProjectSnapshot ProjectOperations::project() const
{
    std::lock_guard lock(impl_->stateMutex);
    auto snapshot = impl_->project;
    snapshot.canUndo = snapshot.canUndoCloud = impl_->history.canUndo();
    snapshot.canRedo = snapshot.canRedoCloud = impl_->history.canRedo();
    snapshot.canReset = snapshot.canResetCloud = impl_->history.hasBaseline() &&
        !impl_->history.baseline().sameContent(ProjectContent::capture(impl_->project, impl_->model));
    snapshot.undoLabel = impl_->history.undoLabel();
    snapshot.redoLabel = impl_->history.redoLabel();
    snapshot.historySteps = impl_->history.size();
    snapshot.historyArrayBytes = impl_->history.retainedArrayBytes();
    return snapshot;
}
} // namespace reverse
