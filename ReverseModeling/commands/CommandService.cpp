/* 文件描述：固定工作线程和有界 FIFO；界面、原生命令和 Python 使用同一执行队列。 */
#include <algorithm>
#include <atomic>
#include <chrono>
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
    };
    mutable std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> queue;
    std::map<std::string, TaskSnapshot> tasks;
    std::deque<std::string> taskOrder;
    std::deque<UiCommandEvent> uiEvents;
    std::string active;
    std::stop_source activeCancellation;
    bool closing{};
    std::atomic_bool workerExited{};
    bool committed{};
    bool inputReady{};
    std::optional<std::string> inputLine;
    std::uint64_t nextId{};
    ProjectOperations operations;
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
            output.append(message.substr(0, limit - output.size()));
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
    void record(const rm::ValidatedCommand &command)
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
        recorder.record(definition, recorded);
    }
    void publishRecording()
    {
        std::lock_guard lock(mutex);
        recording = recorder.state();
    }
    rm::CommandResult execute(const rm::CommandContext &, const rm::ValidatedCommand &command,
                              const rm::CommandManager::Next &next)
    {
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
        return result;
    }
    std::string submit(std::string operation, std::optional<std::uint64_t> revision, decltype(Job::run) run,
                       std::optional<CommandText> uiCommand = {})
    {
        std::lock_guard lock(mutex);
        if (closing)
        {
            throw GeometryError("shutting_down", "命令服务正在退出。");
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
        queue.push_back({id, revision, std::move(run), {}, std::move(uiCommand)});
        ready.notify_one();
        return id;
    }
    void run(Job &job, PythonRuntime &python)
    {
        const auto start = std::chrono::steady_clock::now();
        committed = false;
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
            while (uiEvents.size() > 256)
            {
                uiEvents.pop_front();
            }
        }
        active.clear();
    }
    void loop()
    {
        PythonRuntime python(manager, environment.runtimeDirectory);
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
