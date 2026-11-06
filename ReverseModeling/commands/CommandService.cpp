#include "commands/PlateEditCommands.h"
/* 文件描述：固定工作线程和有界 FIFO；界面、原生命令和 Python 使用同一执行队列。 */
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <utility>
#ifdef _WIN32
#include <Windows.h>
#endif
#include "CommandModules.h"
#include "commands/CommandBindings.h"
#include "commands/CommandFeedback.h"
#include "commands/CommandService.h"
#include "commands/MacroRecorder.h"
#include "commands/ReplayJournal.h"
#include "commands/ProjectOperations.h"
#include "commands/TaskOutput.h"
#include "commands/PythonRuntime.h"
#include "geometry/Geometry.h"

namespace reverse
{
namespace
{
std::filesystem::path executableDirectory()
{
#ifdef _WIN32
    std::wstring buffer(32768, L'\0');
    const auto count = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (count == 0 || count == buffer.size())
    {
        throw std::runtime_error("Cannot locate executable directory");
    }
    buffer.resize(count);
    return std::filesystem::path(buffer).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe").parent_path();
#endif
}
} // namespace
struct CommandService::Impl
{
    struct Job
    {
        std::string id;
        std::optional<std::uint64_t> revision;
        std::function<rm::CommandResult(const rm::CommandContext &, PythonRuntime &)> run;
        std::stop_source cancellation;
        std::optional<CommandText> uiCommand;
        bool exclusive{};
        std::string failureContext;
    };
    mutable std::mutex mutex;
    std::mutex logMutex;
    std::condition_variable ready;
    std::deque<Job> queue;
    std::map<std::string, TaskSnapshot> tasks;
    std::deque<std::string> taskOrder;
    std::deque<UiCommandEvent> uiEvents;
    std::string active;
    std::string activeUiTask;
    std::stop_source activeCancellation;
    bool closing{};
    bool submissionLocked{};
    bool outputInitialized{};
    std::optional<std::filesystem::path> outputDirectory;
    std::atomic_bool exitRequested{};
    ReplayJournal journal;
    std::atomic_bool workerExited{};
    bool committed{};
    bool inputReady{};
    std::optional<std::string> inputLine;
    std::uint64_t nextId{};
    ProjectOperations operations;
    TaskOutput taskOutput;
    rm::CommandManager manager;
    MacroRecorder recorder;
    RecordingState recording;
    CommandEnvironment environment;
    std::jthread worker;

    explicit Impl(CommandEnvironment env) : environment(std::move(env))
    {
        if (environment.runtimeDirectory.empty())
        {
            environment.runtimeDirectory = executableDirectory();
        }
        rmcmd::registerAllCommandModules(manager);
        registerApplicationCommandHandlers(manager, operations);
        manager.bindHandler("project.set_output_dir", [this](const auto &, const auto &args) {
            try
            {
                taskOutput.initialize(commandPath(args.at("path").template as<std::string>()));
                const auto path = taskOutput.modelPath().parent_path();
                const auto bytes = path.u8string();
                const std::string value(reinterpret_cast<const char *>(bytes.data()), bytes.size());
                { std::lock_guard lock(mutex); outputInitialized = true; outputDirectory = path; }
                return rm::CommandResult{rm::ResultKind::Success, {}, "任务输出目录：" + value,
                                         rm::Arguments{{"path", value}}, true};
            }
            catch (const GeometryError &error) { return rm::CommandResult::error(error.code(), error.what()); }
        });
        manager.bindHandler("project.export_and_exit", [this](const auto &context, const auto &) {
            try
            {
                operations.setContext(context);
                operations.exportTaskOutput(taskOutput, context.expectedRevision.value_or(operations.project().revision));
                { std::lock_guard lock(mutex); outputInitialized = false; }
                if (context.output) context.output(commandDetails("model.export_step", operations.project()));
                return rm::CommandResult{rm::ResultKind::Success, {}, "STEP 与结果 JSON 已提交。", {}, true};
            }
            catch (const GeometryError &error) { return rm::CommandResult::error(error.code(), error.what()); }
        });
        manager.registerExecutor([this](const auto &context, const auto &command, const auto &next) {
            return execute(context, command, next);
        });
        manager.freeze();
        worker = std::jthread([this] {
            loop();
            workerExited.store(true);
        });
    }
    ~Impl()
    {
        requestShutdown();
        worker.join();
    }
    void requestShutdown()
    {
        {
            std::lock_guard lock(mutex);
            closing = true;
            for (auto &job : queue)
            {
                job.cancellation.request_stop();
            }
            activeCancellation.request_stop();
        }
        ready.notify_all();
    }
    void appendOutput(const std::string &id, std::string_view message)
    {
        std::lock_guard lock(mutex);
        auto &output = tasks.at(id).output;
        constexpr std::size_t limit = 1024 * 1024;
        if (output.size() < limit)
        {
            const auto chunk = message.substr(0, limit - output.size());
            output.append(chunk);
            if (activeUiTask == id)
            {
                // Coalesce writes without crossing the command start/finish boundaries.
                if (!uiEvents.empty() && uiEvents.back().taskId == id &&
                    uiEvents.back().kind == UiCommandEventKind::Output)
                {
                    uiEvents.back().message.append(chunk);
                }
                else
                {
                    uiEvents.push_back({id, {}, {}, std::string(chunk), false, UiCommandEventKind::Output});
                }
            }
        }
    }
    std::optional<std::string> readInput(const std::string &id, std::stop_token stop)
    {
        std::unique_lock lock(mutex);
        auto &task = tasks.at(id);
        inputReady = false;
        inputLine.reset();
        task.waitingForInput = true;
        ++task.inputRequest;
        ready.wait(lock, [&] { return closing || stop.stop_requested() || inputReady; });
        task.waitingForInput = false;
        inputReady = false;
        return std::move(inputLine);
    }
    void log(std::string_view text)
    {
        std::lock_guard lock(logMutex);
        if (environment.logDirectory.empty())
        {
            return;
        }
        std::error_code error;
        std::filesystem::create_directories(environment.logDirectory, error);
        if (error)
        {
            return;
        }
        std::ofstream file(environment.logDirectory / "commands.log", std::ios::app | std::ios::binary);
        file << text << '\n';
    }
    void reportFailure(std::string_view operation, std::string_view reason, const std::string &taskId = {})
    {
        const auto message = std::string(operation) + "失败：" + std::string(reason) + '\n';
        log(message);
        std::lock_guard lock(mutex);
        uiEvents.push_back({taskId, {}, {}, message, false, UiCommandEventKind::Output});
        while (uiEvents.size() > 1024)
        {
            uiEvents.pop_front();
        }
    }
    rm::ValidatedCommand absoluteCommand(const rm::ValidatedCommand &command)
    {
        auto recorded = command;
        const auto &definition = manager.definitions().at(recorded.name);
        for (const auto &parameter : definition.parameters)
        {
            if (parameter.rule.type == rm::ValueType::Path && recorded.arguments.contains(parameter.name))
            {
                const auto absolute =
                    std::filesystem::absolute(commandPath(recorded.arguments.at(parameter.name).as<std::string>()))
                        .u8string();
                recorded.arguments[parameter.name] =
                    std::string(reinterpret_cast<const char *>(absolute.data()), absolute.size());
            }
        }
        recorded.name = canonicalHistoryCommand(recorded.name);
        return recorded;
    }
    void recordSuccess(const rm::ValidatedCommand &command, const rm::CommandContext &context)
    {
        try
        {
            const auto normalized = absoluteCommand(command);
            const auto line = recordingLine(manager.definitions().at(normalized.name), normalized);
            if (line.find_first_of("\r\n") != std::string::npos)
                throw std::runtime_error("命令参数含换行，无法完整记录当前会话");
            const bool wasComplete = journal.error().empty();
            journal.append(normalized.name, line);
            if (wasComplete && !journal.error().empty() && context.output)
                context.output("会话补写不可用：" + journal.error() + "\n");
            recorder.recordLine(line);
        }
        catch (const std::exception &error)
        {
            journal.invalidate(error.what());
            if (recorder.recording()) recorder.fail(error.what());
            if (context.output) context.output("会话记录失败：" + journal.error() + "\n");
        }
        publishRecording();
        if (!recorder.state().error.empty()) log(recorder.state().error);
    }
    void publishRecording()
    {
        std::lock_guard lock(mutex);
        recording = recorder.state();
    }
    rm::CommandResult execute(const rm::CommandContext &context, const rm::ValidatedCommand &command,
                              const rm::CommandManager::Next &next)
    {
        if (exitRequested.load()) return rm::CommandResult::error("shutting_down", "交付已完成，程序正在退出。");
        const auto start = std::chrono::steady_clock::now();
        log("begin " + command.name);
        rm::CommandResult result;
        try
        {
            result = next();
        }
        catch (const std::exception &error)
        {
            result = rm::CommandResult::error("execution_exception", error.what());
        }
        catch (...)
        {
            result = rm::CommandResult::error("execution_exception", "Unknown handler failure");
        }
        const double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        const auto message =
            command.name + " [" + (result.ok() ? "ok" : result.code) + "] " + std::to_string(elapsed) + " ms\n";
        log(message);
        committed = committed || result.committed;
        const bool emptyTransform = !result.committed && (command.name == "model.move" || command.name == "model.rotate");
        if (result.kind == rm::ResultKind::Success && !emptyTransform) recordSuccess(command, context);
        if (result.kind == rm::ResultKind::Success && command.name == "project.export_and_exit")
        {
            const bool wasRecording = recorder.recording();
            if (wasRecording) (void)recorder.stop();
            publishRecording();
            if (!recorder.state().error.empty())
            {
                result = rm::CommandResult::error("recording_write_failed",
                    "模型已交付，但录制未完整保存，已停止自动退出：" + recorder.state().error);
                result.committed = true;
                return result;
            }
            if (wasRecording && context.output) context.output("录制脚本已保存。\n");
            result.message = "STEP 与结果 JSON 已提交，正在退出。";
            std::lock_guard lock(mutex);
            submissionLocked = true;
            exitRequested.store(true);
        }
        return result;
    }
    rm::CommandResult executeStartup(const std::string &name, const rm::Array &args,
                                     const rm::Arguments &options, const rm::CommandContext &context)
    {
        return manager.execute(name, args, options, context);
    }
    std::string submit(std::string operation, std::optional<std::uint64_t> revision, decltype(Job::run) run,
                       std::optional<CommandText> uiCommand = {}, bool exclusive = false,
                       std::string failureContext = {})
    {
        std::lock_guard lock(mutex);
        if (closing || exitRequested.load())
        {
            throw GeometryError("shutting_down", "命令服务正在退出。");
        }
        if (submissionLocked || (exclusive && (!active.empty() || !queue.empty())))
        {
            throw GeometryError("task_busy", "请等待当前任务结束，导出并退出期间不能执行其他命令。");
        }
        if (queue.size() >= 64)
        {
            throw GeometryError("queue_full", "命令队列已满（64），请等待或取消任务。");
        }
        const auto id = "task-" + std::to_string(++nextId);
        tasks.emplace(id, TaskSnapshot{id, std::move(operation)});
        taskOrder.push_back(id);
        while (taskOrder.size() > 256 && tasks.at(taskOrder.front()).finished())
        {
            tasks.erase(taskOrder.front());
            taskOrder.pop_front();
        }
        queue.push_back({id, revision, std::move(run), {}, std::move(uiCommand), exclusive,
                         std::move(failureContext)});
        submissionLocked = exclusive;
        ready.notify_one();
        return id;
    }
    void run(Job &job, PythonRuntime &python)
    {
        const auto start = std::chrono::steady_clock::now();
        committed = false;
        if (!job.failureContext.empty())
        {
            // 内部检查/预览也将逐件诊断送到命令窗口，但不伪造可录制命令。
            std::lock_guard lock(mutex);
            activeUiTask = job.id;
        }
        if (job.uiCommand)
        {
            std::lock_guard lock(mutex);
            activeUiTask = job.id;
            uiEvents.push_back({job.id, job.uiCommand->native, job.uiCommand->python, {}, false,
                                UiCommandEventKind::Started});
        }
        rm::CommandContext context;
        context.stop = job.cancellation.get_token();
        context.expectedRevision = job.revision;
        python.setInputReader([this, id = job.id, stop = context.stop] { return readInput(id, stop); });
        struct ClearInputReader
        {
            PythonRuntime &python;
            ~ClearInputReader()
            {
                python.setInputReader({});
            }
        } clearInputReader{python};
        context.output = [this, id = job.id](std::string_view message) { appendOutput(id, message); };
        context.diagnostic = [this](std::string_view message) { log(message); };
        context.progress = [this, id = job.id, stop = context.stop](double progress, std::string_view message) {
            if (stop.stop_requested())
            {
                throw GeometryError("cancelled", "Command cancelled");
            }
            std::lock_guard lock(mutex);
            tasks.at(id).progress = std::clamp(progress, 0.0, 1.0);
            tasks.at(id).message = message;
        };
        rm::CommandResult result;
        try
        {
            if (context.stop.stop_requested())
            {
                result = rm::CommandResult::error("cancelled", "排队任务已取消");
            }
            else
            {
                result = job.run(context, python);
            }
        }
        catch (const GeometryError &error)
        {
            result = rm::CommandResult::error(error.code(), error.what());
        }
        catch (const std::exception &error)
        {
            result = rm::CommandResult::error("internal_error", error.what());
        }
        catch (...)
        {
            result = rm::CommandResult::error("unknown_error", "Unknown command failure");
        }
        // 内部预览/识别没有正式命令回显，但失败原因必须进入同一命令窗口。
        // 取消不算生成失败，正式命令仍只由 Finished 事件报告一次。
        if (!result.ok() && result.code != "cancelled" && !job.failureContext.empty())
        {
            reportFailure(job.failureContext, result.message, job.id);
        }
        result.committed = result.committed || committed;
        if (!result.ok())
        {
            log(job.id + " [" + result.code + "] " + result.message + "\n" + rm::formatValue(result.value));
        }
        std::lock_guard lock(mutex);
        auto &task = tasks.at(job.id);
        task.result = std::move(result);
        task.waitingForInput = false;
        task.errorCode = task.result.code;
        task.message = task.result.message;
        task.state = task.result.ok()                ? TaskState::Succeeded
                     : task.errorCode == "cancelled" ? TaskState::Cancelled
                                                     : TaskState::Failed;
        if (task.state == TaskState::Succeeded)
        {
            task.progress = 1;
        }
        task.elapsedMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (job.uiCommand)
        {
            // Only UI adapters enqueue echoes; console/Python calls already have their own transcript.
            uiEvents.push_back({job.id, std::move(job.uiCommand->native), std::move(job.uiCommand->python),
                                commandFeedback(task.result), task.result.ok()});
            while (uiEvents.size() > 1024)
            {
                uiEvents.pop_front();
            }
        }
        if (job.exclusive)
        {
            submissionLocked = false;
        }
        if (exitRequested.load())
        {
            // 交付成功后不存在等待任务，也不再接受新命令；不触发可取消的关闭检查。
            closing = true;
            for (const auto &pending : queue)
            {
                auto &cancelled = tasks.at(pending.id);
                cancelled.state = TaskState::Cancelled;
                cancelled.errorCode = "shutting_down";
                cancelled.message = "交付已完成，未执行后续任务。";
                cancelled.result = rm::CommandResult::error(cancelled.errorCode, cancelled.message);
            }
            queue.clear();
        }
        activeUiTask.clear();
        active.clear();
    }
    void loop()
    {
        PythonRuntime python(manager, environment.runtimeDirectory, [this] { return exitRequested.load(); });
        while (true)
        {
            Job job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [this] { return closing || !queue.empty(); });
                if (queue.empty())
                {
                    break;
                }
                job = std::move(queue.front());
                queue.pop_front();
                active = job.id;
                activeCancellation = job.cancellation;
                auto &task = tasks.at(job.id);
                task.state = TaskState::Running;
                std::ostringstream thread;
                thread << std::this_thread::get_id();
                task.workerThread = thread.str();
            }
            run(job, python);
        }
        if (recorder.state().active)
        {
            const auto result = recorder.stop();
            if (!result.ok())
            {
                log(result.message);
            }
            publishRecording();
        }
    }
};
CommandService::CommandService(CommandEnvironment environment) : impl_(std::make_unique<Impl>(std::move(environment)))
{
}
CommandService::~CommandService() = default;
void CommandService::requestShutdown()
{
    impl_->requestShutdown();
}
bool CommandService::shutdownComplete() const
{
    return impl_->workerExited.load();
}
bool CommandService::isIdle() const
{
    std::lock_guard lock(impl_->mutex);
    return !impl_->closing && impl_->active.empty() && impl_->queue.empty() && !impl_->submissionLocked;
}
bool CommandService::outputInitialized() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->outputInitialized;
}
std::optional<std::filesystem::path> CommandService::outputDirectory() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->outputDirectory;
}
bool CommandService::exitRequested() const { return impl_->exitRequested.load(); }
std::string CommandService::submitInitializeOutput(const std::filesystem::path &directory)
{
    auto *state = impl_.get();
    const auto bytes = std::filesystem::absolute(directory).lexically_normal().u8string();
    const std::string path(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    auto echo = commandText(state->manager.definitions().at("project.set_output_dir"), {path}, {});
    return state->submit("InitializeOutput", {}, [state, path](const auto &context, auto &) {
        return state->executeStartup("project.set_output_dir", {path}, {}, context);
    }, std::move(echo), true);
}
std::string CommandService::submitStartupImport(const std::filesystem::path &requestedPath)
{
    auto *state = impl_.get();
    const auto path = std::filesystem::absolute(requestedPath).lexically_normal();
    const auto bytes = path.u8string();
    const std::string value(reinterpret_cast<const char *>(bytes.data()), bytes.size());
    auto echo = commandText(state->manager.definitions().at("cloud.import"), {value}, {});
    return state->submit("StartupImport", {}, [state, path, value](const auto &context, auto &) {
        auto extension = path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::error_code error;
        if (!std::filesystem::is_regular_file(path, error))
        {
            return rm::CommandResult::error("input_not_file", "--input_file 文件不存在或不是普通文件：" + value);
        }
        if (extension != ".pcd")
        {
            return rm::CommandResult::error("unsupported_format", "--input_file 仅接受 PCD 点云：" + value);
        }
        auto result = state->executeStartup("cloud.import", {value}, {}, context);
        if (!result.ok())
        {
            result.message = "--input_file " + value + "：" + result.message;
        }
        return result;
    }, std::move(echo));
}
std::string CommandService::submitExportAndExit(std::uint64_t expectedRevision)
{
    auto *state = impl_.get();
    auto echo = commandText(state->manager.definitions().at("project.export_and_exit"), {}, {});
    return state->submit("ExportAndExit", expectedRevision, [state](const auto &context, auto &) {
        return state->manager.execute("project.export_and_exit", {}, {}, context);
    }, std::move(echo), true);
}
std::string CommandService::submitCommand(std::string name, rm::Array positional, rm::Arguments options,
                                          std::optional<std::uint64_t> revision)
{
    auto *state = impl_.get();
    return state->submit(
        name, revision,
        [state, name, positional = std::move(positional), options = std::move(options)](const auto &context, auto &) {
            return state->manager.execute(name, positional, options, context);
        });
}
void CommandService::reportUiFailure(std::string_view operation, std::string_view reason)
{
    impl_->reportFailure(operation, reason);
}
std::string CommandService::submitEdgePreview(const EdgeTreatment &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("EdgePreview", revision, [state,request,revision](const auto &context,auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewEdgeTreatment(request,revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；点击应用才修改模型。", {}, false};
    }, {}, false, request.kind == EdgeTreatmentKind::Fillet ? "圆角预览" : "倒角预览");
}
std::string CommandService::submitThicknessPreview(const PlateThicknessChange &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("ThicknessPreview", revision, [state,request,revision](const auto &context,auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewThickness(request,revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；点击应用才修改模型。", {}, false};
    }, {}, false, "修改板厚预览");
}
std::string CommandService::submitContourExpansionPreview(const PlateContourExpansion &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("ContourExpansionPreview", revision, [state,request,revision](const auto &context,auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewContourExpansion(request,revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；点击应用才修改模型。", {}, false};
    }, {}, false, "外扩板件轮廓预览");
}
std::string CommandService::submitMirrorCompletionPreview(const MirrorCompletion &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("MirrorCompletionPreview", revision, [state,request,revision](const auto &context,auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewMirrorCompletion(request,revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；点击应用才修改模型。", {}, false};
    }, {}, false, "镜像补全预览");
}
std::string CommandService::submitDirectPlateEdit(const PlateEdit &request, std::uint64_t revision, bool all)
{
    auto *state = impl_.get();
    const auto name = std::string("model.edit_plate");
    return state->submit(name, revision, [state, request, revision, all, name](const auto &context, auto &) {
        state->operations.setContext(context);
        {
            std::lock_guard lock(state->mutex);
            state->activeUiTask = state->active;
        }
        PlateEdit prepared;
        try
        {
            prepared = state->operations.prepareDirectPlateEdit(request, all, revision);
        }
        catch (const GeometryError &error)
        {
            if (error.code() != "cancelled")
                state->reportFailure(plateEditTitle(request.operation), error.what(), state->active);
            throw;
        }
        const auto args = plateEditArguments(prepared);
        const auto echo = commandText(state->manager.definitions().at(name), {}, args);
        {
            std::lock_guard lock(state->mutex);
            state->activeUiTask = state->active;
            state->uiEvents.push_back({state->active, echo.native, echo.python, {}, false, UiCommandEventKind::Started});
        }
        const auto result = state->manager.execute(name, {}, args, context);
        {
            std::lock_guard lock(state->mutex);
            state->uiEvents.push_back({state->active, echo.native, echo.python, commandFeedback(result), result.ok()});
        }
        return result;
    });
}

std::string CommandService::submitPlateEditPreview(const PlateEdit &request,std::uint64_t revision)
{
    auto *s=impl_.get();
    return s->submit("PlateEditPreview",revision,[s,request,revision](const auto &context,auto &){
        s->operations.setContext(context);
        auto result=s->operations.previewPlateEdit(request,revision);
        for (const auto &line:result.reports) if (context.output) context.output(line+"\n");
        const auto value=rm::Value(plateEditArguments(result.request));
        std::lock_guard lock(s->mutex);
        s->tasks.at(s->active).edgePreview=result.changed?std::move(result.model):nullptr;
        return rm::CommandResult{rm::ResultKind::Success,{},result.changed?"预览完成；应用后整批提交，可一次撤销。":"无需修改，模型保持不变。",value,false};
    },{},false,plateEditTitle(request.operation)+"预览");
}
std::string CommandService::submitPlateReview(const std::vector<std::string> &parts,const PlateEdit &request,std::uint64_t revision)
{
    auto *s=impl_.get();
    return s->submit("PlateReview",revision,[s,parts,request,revision](const auto &context,auto &){
        s->operations.setContext(context);nlohmann::json rows=nlohmann::json::array();
        for (const auto &id:parts)
        {
            if (context.stop.stop_requested()) throw GeometryError("cancelled","检查已取消。");
            nlohmann::json row{{"part",id},{"supported",false},{"changed",false}};
            try {
                if (request.operation=="holes") {
                    const auto bases=s->operations.inspectHoleBases(id,revision);row["edges"]=nlohmann::json::array();
                    for (const auto &b:bases) row["edges"].push_back(b.edge);
                    { std::lock_guard lock(s->mutex);s->tasks.at(s->active).batchHoleBases[id]=bases; }
                    row["supported"]=!bases.empty();row["changed"]=!bases.empty();row["message"]="请逐板核对底边编号";
                } else {
                    auto single=request;single.parts={id};auto result=s->operations.previewPlateEdit(single,revision);
                    row["supported"]=true;row["changed"]=result.changed;row["message"]=result.reports.empty()?"无需修改":result.reports.front();
                }
            } catch (const GeometryError &e) { if(e.code()=="cancelled") throw;row["message"]=e.what(); }
            if(context.output) {
                const auto message=row.at("message").get<std::string>();
                context.output((message.starts_with(id)?message:id+"："+message)+"\n");
            }
            rows.push_back(std::move(row));
        }
        return rm::CommandResult{rm::ResultKind::Success,{},"检查完成，请核对并勾选处理项。",commandValue(rows),false};
    },{},false,"板件检查");
}
std::string CommandService::submitLocalReconstructionPreview(const LocalReconstruction &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("LocalReconstructionPreview", revision, [state, request, revision](const auto &context, auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewLocalReconstruction(request, revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "局部重建预览完成；未选部件保持不变，点击应用才提交。", {}, false};
    }, {}, false, "局部重建预览");
}

std::string CommandService::submitPlateGenerationPreview(const PlateGeneration &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("PlateGenerationPreview", revision, [state, request, revision](const auto &context, auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewPlateGeneration(request, revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；绿色为新板件，应用后加入模型。", {}, false};
    }, {}, false, (request.kind == PlateGenerationKind::Cover ? "生成外扩贴板预览（部件 " : "生成下底板预览（部件 ") +
        request.partId + (request.secondPartId.empty() ? "" : "、" + request.secondPartId) + "）");
}
std::string CommandService::submitThicknessInspection(const std::vector<std::string> &parts, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("ThicknessInspection", revision, [state, parts, revision](const auto &context, auto &) {
        state->operations.setContext(context);
        auto values = state->operations.inspectThickness(parts, revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).plateThicknesses = std::move(values);
        return rm::CommandResult{rm::ResultKind::Success, {}, "已读取当前实体板厚。", {}, false};
    }, {}, false, "板厚识别");
}
std::string CommandService::submitHolePreview(const PlateHole &request, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("HolePreview", revision, [state,request,revision](const auto &context,auto &) {
        state->operations.setContext(context);
        auto model = state->operations.previewHole(request,revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).edgePreview = std::move(model);
        return rm::CommandResult{rm::ResultKind::Success, {}, "预览完成；点击应用才修改模型。", {}, false};
    }, {}, false, (request.kind == PlateHoleKind::Weld ? "过焊孔预览（部件 " : "底边孔预览（部件 ") + request.partId + "）");
}
std::string CommandService::submitHoleInspection(const std::string &partId, std::uint64_t revision)
{
    auto *state = impl_.get();
    return state->submit("HoleInspection", revision, [state,partId,revision](const auto &context, auto &) {
        state->operations.setContext(context);
        auto bases = state->operations.inspectHoleBases(partId, revision);
        std::lock_guard lock(state->mutex);
        state->tasks.at(state->active).holeBases = std::move(bases);
        return rm::CommandResult{rm::ResultKind::Success, {}, "底边已识别；可切换底边并预览。", {}, false};
    }, {}, false, "开孔底边识别（部件 " + partId + "）");
}
std::shared_ptr<const Model> CommandService::takeEdgePreview(const std::string &taskId)
{
    std::lock_guard lock(impl_->mutex);
    return std::exchange(impl_->tasks.at(taskId).edgePreview, {});
}
std::string CommandService::submitLine(std::string line)
{
    auto *state = impl_.get();
    return state->submit("Command", {}, [line = std::move(line)](const auto &context, PythonRuntime &python) {
        return python.executeConsole(line, context);
    });
}
std::string CommandService::submitCommandArguments(std::vector<std::string> arguments)
{
    if (arguments.size() == 1)
        return submitLine(std::move(arguments.front()));
    auto *state = impl_.get();
    return state->submit("Command", {}, [state, arguments = std::move(arguments)](const auto &context, auto &) {
        if (context.output) context.output(commandExecutionEcho(commandArgumentsText(arguments)));
        return state->manager.executeArgv(arguments, context);
    });
}
std::string CommandService::submitUiCommand(std::string name, rm::Array positional, rm::Arguments options,
                                            std::optional<std::uint64_t> revision)
{
    auto *state = impl_.get();
    auto echo = commandText(state->manager.definitions().at(name), positional, options);
    return state->submit(
        name, revision,
        [state, name, positional = std::move(positional), options = std::move(options)](const auto &context, auto &) {
            return state->manager.execute(name, positional, options, context);
        },
        std::move(echo));
}
std::vector<UiCommandEvent> CommandService::takeUiCommandEvents()
{
    std::lock_guard lock(impl_->mutex);
    std::vector<UiCommandEvent> events;
    events.reserve(impl_->uiEvents.size());
    while (!impl_->uiEvents.empty())
    {
        events.push_back(std::move(impl_->uiEvents.front()));
        impl_->uiEvents.pop_front();
    }
    return events;
}
std::string CommandService::submitPython(std::string code)
{
    return impl_->submit("Python", {}, [code = std::move(code)](const auto &context, PythonRuntime &python) {
        return python.execute(code, context);
    });
}
std::string CommandService::submitPythonLine(std::string line)
{
    return impl_->submit("PythonConsole", {}, [line = std::move(line)](const auto &context, PythonRuntime &python) {
        return python.execute(line, context, true);
    });
}
std::string CommandService::resetPythonConsole()
{
    return impl_->submit("PythonConsoleReset", {}, [](const auto &, PythonRuntime &python) {
        python.resetConsole();
        return rm::CommandResult{rm::ResultKind::Success, {}, {}, {}, false};
    });
}
bool CommandService::provideInput(const std::string &id, std::uint64_t request, std::optional<std::string> line)
{
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->tasks.find(id);
    if (impl_->active != id || found == impl_->tasks.end() || !found->second.waitingForInput ||
        found->second.inputRequest != request || found->second.cancellationRequested || impl_->inputReady ||
        (line && line->size() > 1024 * 1024))
    {
        return false;
    }
    impl_->inputLine = std::move(line);
    impl_->inputReady = true;
    found->second.waitingForInput = false;
    impl_->ready.notify_all();
    return true;
}
std::string CommandService::submitStartRecording(const std::filesystem::path &path, bool overwrite)
{
    auto *state = impl_.get();
    return state->submit("StartRecording", {}, [state, path, overwrite](const auto &context, auto &) {
        if (state->recorder.recording())
            return rm::CommandResult::error("already_recording", "已在录制中，请先停止录制。");
        bool opened{};
        rm::CommandResult result;
        try
        {
            const auto prefix = state->journal.prefix(); // 先校验完整性与大小，再打开/覆盖文件。
            if (context.stop.stop_requested()) return rm::CommandResult::error("cancelled", "录制任务已取消。");
            result = state->recorder.start(path, overwrite);
            opened = result.ok();
            if (opened)
            {
                for (const auto &line : prefix)
                {
                    if (context.stop.stop_requested()) throw std::runtime_error("补写被取消，脚本前序命令不完整");
                    state->recorder.recordLine(line);
                    if (!state->recorder.recording()) throw std::runtime_error(state->recorder.state().error);
                }
                result.message += "\n已补写 " + std::to_string(prefix.size()) + " 条前序命令，后续命令将逐条录制。";
            }
        }
        catch (const std::exception &error)
        {
            if (opened) state->recorder.fail(std::string("前序命令补写未完成：") + error.what());
            result = rm::CommandResult::error("recording_prefix_failed",
                std::string("未开始完整录制：") + error.what());
        }
        state->publishRecording();
        return result;
    });
}
std::string CommandService::submitStopRecording()
{
    auto *state = impl_.get();
    return state->submit("StopRecording", {}, [state](const auto &, auto &) {
        auto result = state->recorder.stop();
        state->publishRecording();
        return result;
    });
}
RecordingState CommandService::recordingState() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->recording;
}
std::string CommandService::submitScriptFile(const std::filesystem::path &path, std::vector<std::string> arguments)
{
    auto *state = impl_.get();
    const auto absolutePath = std::filesystem::absolute(path).lexically_normal();
    return state->submit("ExecuteScript", {}, [path = absolutePath, arguments = std::move(arguments)](
                                                const auto &context, PythonRuntime &python) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return rm::CommandResult::error("script_open_failed", "无法读取脚本文件。");
        }
        const auto length = file.tellg();
        constexpr std::streamoff limit = 64 * 1024 * 1024;
        if (length < 0 || length > limit)
        {
            return rm::CommandResult::error("script_size_limit", "脚本读取失败或超过 64 MiB。");
        }
        std::string code(static_cast<std::size_t>(length), '\0');
        file.seekg(0);
        if (!file.read(code.data(), static_cast<std::streamsize>(code.size())))
        {
            return rm::CommandResult::error("script_read_failed", "脚本未能完整读取。");
        }
        // Execute a fixed snapshot, even if this is the current recording file.
        // Commands appended during execution must not extend this run's input.
        file.close();
        if (context.stop.stop_requested())
        {
            return rm::CommandResult::error("cancelled", "脚本任务已取消。");
        }
        const auto utf8 = path.u8string();
        return python.executeScript(code, context,
            std::string(reinterpret_cast<const char *>(utf8.data()), utf8.size()), arguments);
    });
}
bool CommandService::cancel(const std::string &id)
{
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->tasks.find(id);
    if (found == impl_->tasks.end() || found->second.finished())
    {
        return false;
    }
    found->second.cancellationRequested = true;
    if (impl_->active == id)
    {
        impl_->activeCancellation.request_stop();
        impl_->ready.notify_all();
        return true;
    }
    for (auto &job : impl_->queue)
    {
        if (job.id == id)
        {
            job.cancellation.request_stop();
            return true;
        }
    }
    return false;
}
TaskSnapshot CommandService::task(const std::string &id) const
{
    std::lock_guard lock(impl_->mutex);
    const auto found = impl_->tasks.find(id);
    if (found == impl_->tasks.end())
    {
        throw GeometryError("unknown_task", "Task ID was not found.");
    }
    return found->second;
}
ProjectSnapshot CommandService::project() const
{
    return impl_->operations.project();
}
std::vector<std::string> CommandService::completions() const
{
    return impl_->manager.completions();
}
} // namespace reverse
