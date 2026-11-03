/*
 * 文件描述：
 * 为桌面工作台提供异步命令、任务状态和不可变工程快照接口。
 */
#pragma once

#include "core/Types.h"
#include "commands/RecordingState.h"
#include "commandManager/CommandManager.h"

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
    bool waitingForInput{};
    std::uint64_t inputRequest{};
    rm::CommandResult result;
    std::string output;
    std::string workerThread;
    double elapsedMs{};
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
    std::optional<CloudExportResult> lastCloudExport;
    std::string exportedCloudPathUtf8;
    std::shared_ptr<const CloudSelection> selection;
    std::shared_ptr<const CloudSelectionRecipe> selectionRecipe;
    bool canUndoCloud{};
    bool canRedoCloud{};
    bool canResetCloud{};
};

struct CommandEnvironment
{
    std::filesystem::path runtimeDirectory;
    std::filesystem::path logDirectory;
};

// Ordered UI command lifecycle and output; only the GUI consumes these events.
enum class UiCommandEventKind { Started, Output, Finished };
struct UiCommandEvent
{
    std::string taskId;
    std::string commandLine;
    std::string pythonLine;
    std::string message;
    bool succeeded{};
    UiCommandEventKind kind{UiCommandEventKind::Finished};
};

class CommandService {
public:
    explicit CommandService(CommandEnvironment environment = {});
    ~CommandService(); // 请求取消并等待工作线程结束；工作线程不访问 UI。
    // GUI 关闭时先请求退出，再异步查询；完成后析构不会等待正在执行的 Python。
    void requestShutdown();
    [[nodiscard]] bool shutdownComplete() const;
    [[nodiscard]] bool isIdle() const;
    [[nodiscard]] bool outputInitialized() const;
    [[nodiscard]] std::optional<std::filesystem::path> outputDirectory() const;
    [[nodiscard]] bool exitRequested() const;
    [[nodiscard]] std::string submitInitializeOutput(const std::filesystem::path &directory);
    [[nodiscard]] std::string submitStartupImport(const std::filesystem::path &path);
    // 独占提交；成功后服务停止接收任务并退出工作线程，GUI 只需完成无取消项的关闭。
    [[nodiscard]] std::string submitExportAndExit(std::uint64_t expectedRevision);
    CommandService(const CommandService&) = delete;
    CommandService& operator=(const CommandService&) = delete;

    [[nodiscard]] std::string submitImport(const std::filesystem::path& path, LengthUnit unit);
    [[nodiscard]] std::string submitCloudSelection(const CloudSelectionRect &rectangle, CloudSelectionMode mode,
                                                   std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitCloudEdit(CloudEditMode mode, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitCloudHistory(CloudHistoryAction action, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitReconstruct(const BeamParameters& parameters,
        std::uint64_t expectedRevision, std::optional<BeamFrame> frameOverride = {});
    [[nodiscard]] std::string submitRecognizedReconstruct(bool mirrorSymmetry, std::uint64_t expectedRevision,
                                                         double endOverhangToleranceMm = 30.0);
    [[nodiscard]] std::string submitExport(const std::filesystem::path& path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitExportCloud(const std::filesystem::path& path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitDeviation(std::uint64_t expectedRevision);
    [[nodiscard]] bool cancel(const std::string& taskId);
    [[nodiscard]] TaskSnapshot task(const std::string& taskId) const;
    [[nodiscard]] ProjectSnapshot project() const;
    [[nodiscard]] std::string submitCommand(std::string name, rm::Array positional = {}, rm::Arguments options = {},
                                           std::optional<std::uint64_t> expectedRevision = {});
    [[nodiscard]] std::string submitLine(std::string line);
    // One complete input string uses the unified console; multiple tokens retain native argv boundaries.
    [[nodiscard]] std::string submitCommandArguments(std::vector<std::string> arguments);
    [[nodiscard]] std::string submitPython(std::string code);
    [[nodiscard]] std::string submitPythonLine(std::string line);
    [[nodiscard]] std::string resetPythonConsole();
    // Reply to the current stdin request, without putting another job behind the waiting script.
    [[nodiscard]] bool provideInput(const std::string &taskId, std::uint64_t request,
                                    std::optional<std::string> line);
    // Recording and native command scripts (.py) share the FIFO; file I/O and parsing stay on the worker.
    [[nodiscard]] std::string submitStartRecording(const std::filesystem::path &path, bool overwrite = false);
    [[nodiscard]] std::string submitStopRecording();
    [[nodiscard]] std::string submitScriptFile(const std::filesystem::path &path,
                                                std::vector<std::string> arguments = {});
    [[nodiscard]] RecordingState recordingState() const;
    [[nodiscard]] std::vector<std::string> completions() const;
    [[nodiscard]] std::vector<UiCommandEvent> takeUiCommandEvents();

private:
    [[nodiscard]] std::string submitUiCommand(std::string name, rm::Array positional, rm::Arguments options,
                                             std::optional<std::uint64_t> expectedRevision);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverse
