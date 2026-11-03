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
#ifdef _WIN32
#include <Windows.h>
#endif
#include "CommandModules.h"
#include "commands/CommandBindings.h"
#include "commands/CommandFeedback.h"
#include "commands/CommandService.h"
#include "commands/MacroRecorder.h"
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
    };
    mutable std::mutex mutex;
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
    std::vector<rm::ValidatedCommand> startupCommands;
    std::uint64_t startupRevision{};
    bool capturingStartup{};
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
        return recorded;
    }
    void record(const rm::ValidatedCommand &command)
    {
        recorder.record(manager.definitions().at(command.name), absoluteCommand(command));
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
        if (result.kind == rm::ResultKind::Success && capturingStartup)
        {
            startupCommands.push_back(absoluteCommand(command));
            startupRevision = operations.project().revision;
        }
        if (result.kind == rm::ResultKind::Success && recorder.recording())
        {
            try
            {
                record(command);
            }
            catch (const std::exception &error)
            {
                recorder.fail(error.what());
            }
            publishRecording();
            if (!recorder.state().error.empty())
            {
                log(recorder.state().error);
            }
        }
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
        capturingStartup = true;
        struct Reset { bool &flag; ~Reset() { flag = false; } } reset{capturingStartup};
        return manager.execute(name, args, options, context);
    }
    std::string submit(std::string operation, std::optional<std::uint64_t> revision, decltype(Job::run) run,
                       std::optional<CommandText> uiCommand = {}, bool exclusive = false)
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
        queue.push_back({id, revision, std::move(run), {}, std::move(uiCommand), exclusive});
        submissionLocked = exclusive;
        ready.notify_one();
        return id;
    }
    void run(Job &job, PythonRuntime &python)
    {
        const auto start = std::chrono::steady_clock::now();
        committed = false;
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
    auto echo = commandText(state->manager.definitions().at("cloud.import"), {value}, {{"unit", "mm"}});
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
        auto result = state->executeStartup("cloud.import", {value}, {{"unit", "mm"}}, context);
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
    return state->submit("StartRecording", {}, [state, path, overwrite](const auto &, auto &) {
        auto result = state->recorder.start(path, overwrite);
        if (result.ok())
        {
            for (const auto &command : state->startupCommands) state->record(command);
            if (!state->recorder.state().error.empty())
                result = rm::CommandResult::error("recording_write_failed", state->recorder.state().error);
            else if (state->operations.project().revision != state->startupRevision)
                result.message += "\n已补写成功的启动命令；开始录制前的编辑未包含，请重新执行这些操作以完整复现。";
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
