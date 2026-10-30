/*
 * 文件描述：
 * 串行调度耗时几何命令，通过不可变快照提交结果，处理版本冲突、失败保留和协作取消。
 */
#include "commands/CommandService.h"
#include <cctype>
#include "geometry/CloudEditing.h"
#include "geometry/Geometry.h"

#include <algorithm>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <random>
#include <sstream>
#include <thread>

namespace reverse {
namespace {
std::string utf8Path(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}
std::string sessionId() {
    std::random_device source;
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (int i = 0; i < 4; ++i) text << std::setw(8) << source();
    return text.str();
}
void checkCancelled(std::stop_token stop) {
    if (stop.stop_requested()) throw GeometryError("cancelled", "Task cancelled before committing results.");
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

struct CommandService::Impl {
    mutable std::mutex stateMutex;
    std::mutex submitMutex;
    ProjectSnapshot project;
    std::shared_ptr<const Model> model;
    struct HistoryEntry
    {
        ProjectSnapshot project;
        std::shared_ptr<const Model> model;
    };
    std::vector<HistoryEntry> undoCloud;
    std::vector<HistoryEntry> redoCloud;
    HistoryEntry importedCloud;
    std::map<std::string, TaskSnapshot> tasks;
    std::string activeTask;
    const std::string session{sessionId()};
    std::uint64_t nextTask{};
    bool active{};
    std::jthread worker;

    static void pushHistory(std::vector<HistoryEntry> &history, HistoryEntry entry)
    {
        history.push_back(std::move(entry));
        if (history.size() > 10)
        {
            history.erase(history.begin());
        }
    }

    ~Impl() {
        worker.request_stop();
        if (worker.joinable()) worker.join();
    }

    using Work = std::function<void(std::stop_token, const ProgressCallback&)>;
    std::string launch(std::string operation, std::uint64_t expectedRevision, Work work) {
        std::lock_guard submitLock(submitMutex);
        {
            std::lock_guard stateLock(stateMutex);
            if (active) throw GeometryError("busy", "Another task is running; wait or request cancellation.");
            if (project.revision != expectedRevision)
                throw GeometryError("stale_revision", "Project changed; refresh the project snapshot before submitting.");
        }
        if (worker.joinable()) worker.join();
        std::string id;
        {
            std::lock_guard stateLock(stateMutex);
            id = session + "-task-" + std::to_string(++nextTask);
            tasks.emplace(id, TaskSnapshot{id, std::move(operation)});
            activeTask = id;
            active = true;
        }
        try {
            worker = std::jthread([this, id, work = std::move(work)](std::stop_token stop) {
                {
                    std::lock_guard lock(stateMutex);
                    tasks.at(id).state = TaskState::Running;
                }
                const ProgressCallback progress = [this, id, stop](double value, std::string_view message) {
                    checkCancelled(stop);
                    std::lock_guard lock(stateMutex);
                    auto& task = tasks.at(id);
                    task.progress = std::clamp(value, 0.0, 1.0);
                    task.message = message;
                };
                try {
                    checkCancelled(stop);
                    work(stop, progress);
                    // 几何服务已完成提交时，迟到的取消请求不能把成功文件伪报为取消。
                    std::lock_guard lock(stateMutex);
                    auto& task = tasks.at(id);
                    task.state = TaskState::Succeeded;
                    task.progress = 1.0;
                    task.message = "Completed";
                    active = false;
                } catch (const GeometryError& error) {
                    std::lock_guard lock(stateMutex);
                    auto& task = tasks.at(id);
                    task.state = error.code() == "cancelled" ? TaskState::Cancelled : TaskState::Failed;
                    task.errorCode = error.code();
                    task.message = error.what();
                    active = false;
                } catch (const std::exception& error) {
                    std::lock_guard lock(stateMutex);
                    auto& task = tasks.at(id);
                    task.state = TaskState::Failed;
                    task.errorCode = "internal_error";
                    task.message = error.what();
                    active = false;
                } catch (...) {
                    std::lock_guard lock(stateMutex);
                    auto& task = tasks.at(id);
                    task.state = TaskState::Failed;
                    task.errorCode = "unknown_error";
                    task.message = "Unrecognized backend failure";
                    active = false;
                }
            });
        } catch (...) {
            std::lock_guard lock(stateMutex);
            active = false;
            tasks.erase(id);
            throw;
        }
        return id;
    }
};

CommandService::CommandService() : impl_(std::make_unique<Impl>()) {}
CommandService::~CommandService() = default;

std::string CommandService::submitImport(const std::filesystem::path& path, LengthUnit unit) {
    const auto version = project().revision;
    const auto absolutePath = std::filesystem::absolute(path);
    auto* state = impl_.get();
    auto extension = absolutePath.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".pcd" && extension != ".step" && extension != ".stp" && extension != ".cwrm")
        throw GeometryError("unsupported_format", "支持导入 PCD、STEP/STP 和 CWRM 文件");
    if (extension != ".pcd")
    {
        return state->launch("ImportModel", version, [state, absolutePath](auto stop, const auto& progress) {
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
            state->project = std::move(next);
            state->model = std::move(model);
            state->undoCloud.clear();
            state->redoCloud.clear();
            state->importedCloud = {};
        });
    }
    return state->launch("ImportPointCloud", version, [state, absolutePath, unit](auto stop, const auto& progress) {
        auto cloud = loadPointCloud(absolutePath, unit, stop, [&](double p, auto m) { progress(p * 0.65, m); });
        ProjectSnapshot next;
        next.cloud = std::move(cloud);
        analyzeCloud(next, stop, [&](double p, auto m) { progress(0.65 + p * 0.35, m); });
        checkCancelled(stop);
        next.sourcePathUtf8 = utf8Path(absolutePath);
        std::lock_guard lock(state->stateMutex);
        next.revision = state->project.revision + 1;
        next.cloudId = state->session + "-cloud-" + std::to_string(next.revision);
        state->project = std::move(next);
        state->model.reset();
        state->undoCloud.clear();
        state->redoCloud.clear();
        state->importedCloud = {state->project, {}};
    });
}

std::string CommandService::submitCloudSelection(const CloudSelectionRect &rectangle, CloudSelectionMode mode,
                                                 std::uint64_t expectedRevision)
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
    auto *state = impl_.get();
    return state->launch(
        "SelectCloud", expectedRevision,
        [state, snapshot, rectangle, mode](auto stop, const auto &progress)
        {
            auto selected = std::make_shared<const CloudSelection>(selectCloudPoints(
                *snapshot.cloud, rectangle, mode,
                snapshot.selection ? std::span<const std::size_t>(*snapshot.selection) : std::span<const std::size_t>{},
                stop, progress));
            checkCancelled(stop);
            std::lock_guard lock(state->stateMutex);
            state->project.selection = std::move(selected);
            ++state->project.revision;
        });
}

std::string CommandService::submitCloudEdit(CloudEditMode mode, std::uint64_t expectedRevision)
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
    if (!snapshot.selection || snapshot.selection->empty())
    {
        throw GeometryError("empty_selection", "请先框选需要处理的点云。");
    }
    auto *state = impl_.get();
    return state->launch(mode == CloudEditMode::KeepSelection ? "CropCloud" : "DeleteCloud", expectedRevision,
                         [state, snapshot, mode](auto stop, const auto &progress)
                         {
                             // 全选后截取不改变数据，也不丢弃已生成模型和重做记录。
                             if (mode == CloudEditMode::KeepSelection &&
                                 snapshot.selection->size() == snapshot.cloud->points.size())
                             {
                                 return;
                             }
                             ProjectSnapshot next;
                             next.cloud = editCloudPoints(*snapshot.cloud, *snapshot.selection, mode, stop,
                                                          [&](double p, auto m) { progress(p * 0.25, m); });
                             next.sourcePathUtf8 = snapshot.sourcePathUtf8;
                             analyzeCloud(next, stop, [&](double p, auto m) { progress(0.25 + p * 0.75, m); });
                             checkCancelled(stop);
                             std::lock_guard lock(state->stateMutex);
                             next.revision = state->project.revision + 1;
                             next.cloudId = state->session + "-cloud-" + std::to_string(next.revision);
                             Impl::pushHistory(state->undoCloud, {state->project, state->model});
                             state->redoCloud.clear();
                             state->project = std::move(next);
                             state->model.reset();
                         });
}

std::string CommandService::submitCloudHistory(CloudHistoryAction action, std::uint64_t expectedRevision)
{
    auto *state = impl_.get();
    return state->launch("CloudHistory", expectedRevision,
                         [state, action](auto stop, const auto &)
                         {
                             std::lock_guard lock(state->stateMutex);
                             checkCancelled(stop);
                             const auto revision = state->project.revision + 1;
                             Impl::HistoryEntry restored;
                             if (action == CloudHistoryAction::Undo)
                             {
                                 if (state->undoCloud.empty())
                                 {
                                     throw GeometryError("no_history", "没有可撤销的点云编辑。");
                                 }
                                 restored = state->undoCloud.back();
                                 Impl::pushHistory(state->redoCloud, {state->project, state->model});
                                 state->undoCloud.pop_back();
                             }
                             else if (action == CloudHistoryAction::Redo)
                             {
                                 if (state->redoCloud.empty())
                                 {
                                     throw GeometryError("no_history", "没有可重做的点云编辑。");
                                 }
                                 restored = state->redoCloud.back();
                                 Impl::pushHistory(state->undoCloud, {state->project, state->model});
                                 state->redoCloud.pop_back();
                             }
                             else if (action == CloudHistoryAction::Reset)
                             {
                                 if (!state->importedCloud.project.cloud)
                                 {
                                     throw GeometryError("no_cloud", "请先导入点云。");
                                 }
                                 if (state->project.cloud == state->importedCloud.project.cloud)
                                 {
                                     return;
                                 }
                                 restored = state->importedCloud;
                                 Impl::pushHistory(state->undoCloud, {state->project, state->model});
                                 state->redoCloud.clear();
                             }
                             else
                             {
                                 throw GeometryError("invalid_edit", "未知点云历史操作。");
                             }
                             restored.project.revision = revision;
                             state->project = std::move(restored.project);
                             state->model = std::move(restored.model);
                         });
}

std::string CommandService::submitReconstruct(const BeamParameters& parameters,
        std::uint64_t expectedRevision, std::optional<BeamFrame> frameOverride) {
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
        throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
    if (!snapshot.cloud || snapshot.cloud->points.empty())
    {
        throw GeometryError("no_cloud", "Import a nonempty point cloud before reconstruction.");
    }
    if (!snapshot.estimate && !frameOverride)
        throw GeometryError("no_frame", "Provide an explicit frame; this cloud has no usable PCA initial frame.");
    const auto frame = frameOverride ? *frameOverride : snapshot.estimate->frame;
    auto* state = impl_.get();
    return state->launch("Reconstruct", expectedRevision, [state, parameters, frame](auto stop, const auto& progress) {
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
        state->project = std::move(next);
        state->model = std::move(model);
    });
}

std::string CommandService::submitRecognizedReconstruct(bool mirrorSymmetry, std::uint64_t expectedRevision,
                                                       double endOverhangToleranceMm)
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
    return state->launch("ReconstructRecognized", expectedRevision,
        [state, recognition = *snapshot.recognition,
         options = PlateBuildOptions{mirrorSymmetry, endOverhangToleranceMm}](auto stop, const auto &progress)
        {
            auto model = buildRecognizedPlates(recognition, options, stop, progress);
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
            state->project = std::move(next);
            state->model = std::move(model);
        });
}

std::string CommandService::submitExport(const std::filesystem::path& path, std::uint64_t expectedRevision) {
    std::shared_ptr<const Model> model;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != expectedRevision)
            throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
        model = impl_->model;
    }
    if (!model) throw GeometryError("no_model", "Reconstruct a model before exporting STEP.");
    const auto absolutePath = std::filesystem::absolute(path);
    auto* state = impl_.get();
    return state->launch("ExportStep", expectedRevision, [state, model, absolutePath](auto stop, const auto& progress) {
        const auto result = exportStep(*model, absolutePath, stop, progress);
        std::lock_guard lock(state->stateMutex);
        state->project.lastExport = result;
        state->project.exportedPathUtf8 = utf8Path(absolutePath);
    });
}

std::string CommandService::submitExportCloud(const std::filesystem::path& path, std::uint64_t expectedRevision)
{
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
        throw GeometryError("stale_revision", "工程已变化，请刷新后重试");
    if (!snapshot.cloud) throw GeometryError("no_cloud", "当前没有可导出的点云");
    const auto absolutePath = std::filesystem::absolute(path);
    auto* state = impl_.get();
    return state->launch("ExportPointCloud", expectedRevision, [state, snapshot, absolutePath](auto stop, const auto& progress) {
        const auto result = exportPointCloud(*snapshot.cloud, absolutePath, stop, progress);
        std::lock_guard lock(state->stateMutex);
        state->project.lastCloudExport = result;
        state->project.exportedCloudPathUtf8 = utf8Path(absolutePath);
    });
}

std::string CommandService::submitDeviation(std::uint64_t expectedRevision) {
    std::shared_ptr<const Model> model;
    std::shared_ptr<const CloudData> cloud;
    {
        std::lock_guard lock(impl_->stateMutex);
        if (impl_->project.revision != expectedRevision)
            throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
        model = impl_->model;
        cloud = impl_->project.cloud;
    }
    if (!model || !cloud) throw GeometryError("no_model", "A cloud and reconstructed model are required.");
    auto* state = impl_.get();
    return state->launch("EvaluateDeviation", expectedRevision, [state, model, cloud](auto stop, const auto& progress) {
        auto result = std::make_shared<const DeviationResult>(evaluateDeviation(*cloud, *model, stop, progress));
        checkCancelled(stop);
        std::lock_guard lock(state->stateMutex);
        state->project.deviation = std::move(result);
    });
}

bool CommandService::cancel(const std::string& taskId) {
    std::lock_guard submitLock(impl_->submitMutex);
    std::lock_guard stateLock(impl_->stateMutex);
    if (!impl_->active || impl_->activeTask != taskId || impl_->tasks.at(taskId).finished()) return false;
    impl_->tasks.at(taskId).cancellationRequested = true;
    impl_->worker.request_stop();
    return true;
}

TaskSnapshot CommandService::task(const std::string& taskId) const {
    std::lock_guard lock(impl_->stateMutex);
    const auto found = impl_->tasks.find(taskId);
    if (found == impl_->tasks.end()) throw GeometryError("unknown_task", "Task ID was not found.");
    return found->second;
}

ProjectSnapshot CommandService::project() const {
    std::lock_guard lock(impl_->stateMutex);
    auto snapshot = impl_->project;
    snapshot.canUndoCloud = !impl_->undoCloud.empty();
    snapshot.canRedoCloud = !impl_->redoCloud.empty();
    snapshot.canResetCloud = snapshot.cloud && snapshot.cloud != impl_->importedCloud.project.cloud;
    return snapshot;
}
} // namespace reverse
