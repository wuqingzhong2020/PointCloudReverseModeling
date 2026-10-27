/*
 * 文件描述：
 * 串行调度耗时几何命令，通过不可变快照提交结果，处理版本冲突、失败保留和协作取消。
 */
#include "reverse/CommandService.h"
#include "reverse/Geometry.h"
#include "reverse/ParameterDefaults.h"

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
} // namespace

struct CommandService::Impl {
    mutable std::mutex stateMutex;
    std::mutex submitMutex;
    ProjectSnapshot project;
    std::shared_ptr<const Model> model;
    std::map<std::string, TaskSnapshot> tasks;
    std::string activeTask;
    const std::string session{sessionId()};
    std::uint64_t nextTask{};
    bool active{};
    std::jthread worker;

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
    return state->launch("ImportPointCloud", version, [state, absolutePath, unit](auto stop, const auto& progress) {
        auto cloud = loadPointCloud(absolutePath, unit, stop, [&](double p, auto m) { progress(p * 0.65, m); });
        std::optional<BeamEstimate> estimate;
        std::vector<std::string> warnings;
        try {
            estimate = estimateBeam(*cloud, stop, [&](double p, auto m) { progress(0.65 + p * 0.35, m); });
        } catch (const GeometryError& error) {
            if (error.code() == "cancelled") throw;
            warnings.emplace_back(std::string("Point cloud imported; beam frame unavailable: ") + error.what());
        }
        checkCancelled(stop);
        ProjectSnapshot next;
        next.cloud = std::move(cloud);
        next.estimate = std::move(estimate);
        next.warnings = std::move(warnings);
        if (next.estimate) {
            next.suggestedParameters = suggestBeamParameters(*next.estimate);
            if (!next.suggestedParameters)
                next.warnings.emplace_back("尺寸超出参考参数的适用范围，未自动填入板厚和肋板位置；可手动填写建模参数。");
        }
        next.sourcePathUtf8 = utf8Path(absolutePath);
        std::lock_guard lock(state->stateMutex);
        next.revision = state->project.revision + 1;
        next.cloudId = state->session + "-cloud-" + std::to_string(next.revision);
        state->project = std::move(next);
        state->model.reset();
    });
}

std::string CommandService::submitReconstruct(const BeamParameters& parameters,
        std::uint64_t expectedRevision, std::optional<BeamFrame> frameOverride) {
    const auto snapshot = project();
    if (snapshot.revision != expectedRevision)
        throw GeometryError("stale_revision", "Refresh the project snapshot before submitting.");
    if (!snapshot.cloud) throw GeometryError("no_cloud", "Import a point cloud before reconstruction.");
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
    return impl_->project;
}
} // namespace reverse
