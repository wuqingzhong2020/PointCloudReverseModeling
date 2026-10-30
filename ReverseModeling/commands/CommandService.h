/*
 * 文件描述：
 * 为桌面工作台提供异步命令、任务状态和不可变工程快照接口。
 */
#pragma once

#include "core/Types.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace reverse {

enum class TaskState { Queued, Running, Succeeded, Failed, Cancelled };

struct TaskSnapshot {
    std::string id;
    std::string operation;
    TaskState state{TaskState::Queued};
    double progress{};
    std::string message;
    std::string errorCode;
    bool cancellationRequested{};
    [[nodiscard]] bool finished() const noexcept {
        return state == TaskState::Succeeded || state == TaskState::Failed || state == TaskState::Cancelled;
    }
};

struct ProjectSnapshot {
    std::uint64_t revision{};
    std::string cloudId;
    std::string modelId;
    std::string sourcePathUtf8;
    std::string exportedPathUtf8;
    std::vector<std::string> warnings;
    std::shared_ptr<const CloudData> cloud;
    std::optional<BeamEstimate> estimate;
    std::optional<PlateRecognition> recognition;
    std::shared_ptr<const ModelSummary> model;
    std::shared_ptr<const MeshData> mesh;
    std::shared_ptr<const DeviationResult> deviation;
    std::optional<StepExportResult> lastExport;
};

class CommandService {
public:
    CommandService();
    ~CommandService(); // 请求取消并等待工作线程结束；工作线程不访问 UI。
    CommandService(const CommandService&) = delete;
    CommandService& operator=(const CommandService&) = delete;

    [[nodiscard]] std::string submitImport(const std::filesystem::path& path, LengthUnit unit);
    [[nodiscard]] std::string submitReconstruct(const BeamParameters& parameters,
        std::uint64_t expectedRevision, std::optional<BeamFrame> frameOverride = {});
    [[nodiscard]] std::string submitRecognizedReconstruct(bool mirrorSymmetry, std::uint64_t expectedRevision,
                                                         double endOverhangToleranceMm = 30.0);
    [[nodiscard]] std::string submitExport(const std::filesystem::path& path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitDeviation(std::uint64_t expectedRevision);
    [[nodiscard]] bool cancel(const std::string& taskId);
    [[nodiscard]] TaskSnapshot task(const std::string& taskId) const;
    [[nodiscard]] ProjectSnapshot project() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverse
