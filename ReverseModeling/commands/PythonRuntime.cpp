/* 文件描述：从部署目录隔离初始化 CPython，在同一工作线程运行脚本并协作取消。 */
#include "commands/PythonRuntime.h"
#include "commands/CommandBindings.h"
#include "commands/CommandFeedback.h"
#include "commands/CommandScript.h"
#include <mutex>
#include <pybind11/embed.h>
#include <thread>
namespace py = pybind11;
namespace reverse
{
namespace
{
thread_local PythonRuntime *currentRuntime{};
std::mutex interpreterMutex;
int traceCancellation(PyObject *, PyFrameObject *, int, PyObject *)
{
    if (currentRuntime && currentRuntime->exitRequested())
    {
        PyErr_SetNone(PyExc_SystemExit);
        return -1;
    }
    if (currentRuntime && currentRuntime->context().stop.stop_requested())
    {
        PyErr_SetString(PyExc_KeyboardInterrupt, "Command cancelled");
        return -1;
    }
    return 0;
}
} // namespace
struct PythonRuntime::Impl
{
    rm::CommandManager &manager;
    std::filesystem::path directory;
    rm::CommandContext context;
    std::function<bool()> exitRequested;
    bool initialized{};
    std::unique_ptr<py::dict> globals;
    std::unique_ptr<py::object> compiler;
    std::unique_ptr<py::object> transform;
    bool scriptCommands{};
    std::string pendingSource;
    std::function<std::optional<std::string>()> inputReader;
    bool interactive{};
    py::object displayValue;
    std::string displayText;
    std::unique_lock<std::mutex> ownership;
    Impl(rm::CommandManager &manager, std::filesystem::path directory)
        : manager(manager), directory(std::move(directory))
    {
    }
    void initialize()
    {
        if (initialized)
        {
            return;
        }
        ownership = std::unique_lock<std::mutex>(interpreterMutex, std::try_to_lock);
        if (!ownership.owns_lock())
        {
            throw std::runtime_error("Another command service owns the Python interpreter");
        }
        linkPythonCommandBindings();
        const auto home = directory / "python";
        if (!std::filesystem::is_directory(home / "Lib/encodings"))
        {
            ownership.unlock();
            throw std::runtime_error("Python runtime missing: use prepareRun to deploy python/Lib");
        }
        PyConfig config;
        PyConfig_InitIsolatedConfig(&config);
        config.site_import = 0;
        config.write_bytecode = 0;
        config.install_signal_handlers = 0;
        config.module_search_paths_set = 1;
        const auto set = [](PyStatus status) {
            if (PyStatus_Exception(status))
            {
                throw std::runtime_error(status.err_msg ? status.err_msg : "Python configuration error");
            }
        };
        try
        {
            set(PyConfig_SetString(&config, &config.home, home.wstring().c_str()));
            for (const auto &path : {home / "Lib", home / "DLLs"})
            {
                set(PyWideStringList_Append(&config.module_search_paths, path.wstring().c_str()));
            }
        }
        catch (...)
        {
            PyConfig_Clear(&config);
            ownership.unlock();
            throw;
        }
        // pybind11 clears PyConfig both on success and initialization failure.
        try
        {
            py::initialize_interpreter(&config, 0, nullptr, false);
        }
        catch (...)
        {
            ownership.unlock();
            throw;
        }
        initialized = true;
        globals = std::make_unique<py::dict>();
        (*globals)["__builtins__"] = py::module_::import("builtins");
        (*globals)["__name__"] = "__main__";
        py::exec("import rm\n", *globals);
        compiler = std::make_unique<py::object>(py::module_::import("codeop").attr("CommandCompiler")());
        py::dict transformGlobals;
        transformGlobals["is_native"] = py::cpp_function([this](const std::string &line) {
            return isNativeCommandLine(manager, line);
        });
        // Tokenize one complete Python statement at a time. Strings, f-strings, brackets
        // and explicit continuations consume their own lines before native detection resumes.
        py::exec(R"PY(
import tokenize

def transform(source):
    parts = source.split('\n')
    lines = [line + '\n' for line in parts[:-1]]
    if parts[-1]:
        lines.append(parts[-1])
    result = []
    i = 0
    while i < len(lines):
        line = lines[i]
        stripped = line.lstrip(' \t')
        if not stripped.strip() or stripped.startswith('#'):
            result.append(line)
            i += 1
            continue
        if is_native(stripped.rstrip('\r\n')):
            indent = line[:len(line) - len(stripped)]
            ending = '\n' if line.endswith('\n') else ''
            result.append(indent + "__import__('rm')._execute_native(" +
                          repr(stripped.rstrip('\r\n')) + ', ' + str(i + 1) + ')' + ending)
            i += 1
            continue
        cursor = i
        def read_line():
            nonlocal cursor
            if cursor >= len(lines):
                return ''
            value = lines[cursor]
            cursor += 1
            return value
        try:
            for token in tokenize.generate_tokens(read_line):
                if token.type == tokenize.NEWLINE:
                    break
        except (tokenize.TokenError, IndentationError, SyntaxError):
            # Let CPython compile/codeop report the original error or request continuation.
            result.extend(lines[i:])
            break
        result.extend(lines[i:cursor])
        i = cursor
    return ''.join(result)
)PY", transformGlobals);
        transform = std::make_unique<py::object>(transformGlobals["transform"]);
    }
    ~Impl()
    {
        if (initialized)
        {
            displayValue = py::object{};
            transform.reset();
            compiler.reset();
            globals.reset();
            py::finalize_interpreter();
        }
    }
};
namespace
{
rm::CommandResult pythonFailure(const py::error_already_set &error)
{
    if (error.matches(PyExc_KeyboardInterrupt))
    {
        return rm::CommandResult::error("cancelled", "KeyboardInterrupt");
    }
    const auto value = error.value();
    if (error.matches(py::module_::import("rm").attr("CommandError")) && py::hasattr(value, "code") &&
        py::hasattr(value, "committed") && py::hasattr(value, "details"))
    {
        auto result =
            rm::CommandResult::error(py::cast<std::string>(value.attr("code")), py::str(value).cast<std::string>());
        result.committed = py::cast<bool>(value.attr("committed"));
        const auto details = py::module_::import("json").attr("dumps")(value.attr("details"));
        result.value = commandValue(nlohmann::json::parse(details.cast<std::string>()));
        return result;
    }
    // Keep user-facing exception type/message; pybind11's internal frames are diagnostic-only.
    return rm::CommandResult::error("python_error", py::str(error.type().attr("__name__")).cast<std::string>() + ": " +
                                                        py::str(value).cast<std::string>());
}
} // namespace
PythonRuntime::PythonRuntime(rm::CommandManager &manager, std::filesystem::path directory,
                             std::function<bool()> exitRequested)
    : m_impl(std::make_unique<Impl>(manager, std::move(directory)))
{
    m_impl->exitRequested = std::move(exitRequested);
}
bool PythonRuntime::exitRequested() const
{
    return m_impl->exitRequested && m_impl->exitRequested();
}
PythonRuntime::~PythonRuntime() = default;
PythonRuntime &PythonRuntime::current()
{
    if (!currentRuntime)
    {
        throw std::runtime_error("wrong_thread: rm is available only on the command worker thread");
    }
    return *currentRuntime;
}
rm::CommandManager &PythonRuntime::manager()
{
    return m_impl->manager;
}
const rm::CommandContext &PythonRuntime::context() const
{
    return m_impl->context;
}
void PythonRuntime::resetConsole()
{
    m_impl->pendingSource.clear();
}
void PythonRuntime::setInputReader(std::function<std::optional<std::string>()> reader)
{
    m_impl->inputReader = std::move(reader);
}
std::optional<std::string> PythonRuntime::readInput()
{
    if (!m_impl->inputReader)
    {
        throw std::runtime_error("This host does not provide interactive input");
    }
    std::optional<std::string> line;
    {
        py::gil_scoped_release release;
        line = m_impl->inputReader();
    }
    if (m_impl->context.stop.stop_requested())
    {
        PyErr_SetString(PyExc_KeyboardInterrupt, "Input cancelled");
        throw py::error_already_set();
    }
    return line;
}
void PythonRuntime::rememberCommandResult(const py::object &value, const rm::CommandResult &result)
{
    if (m_impl->interactive)
    {
        m_impl->displayValue = result.message.empty() ? py::object{} : value;
        m_impl->displayText = result.message;
    }
}
rm::CommandResult PythonRuntime::executeConsole(const std::string &line, const rm::CommandContext &context)
{
    if (m_impl->pendingSource.empty())
    {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            return {};
        }
        if (isNativeCommandLine(m_impl->manager, line))
        {
            if (context.output) context.output(commandExecutionEcho(line));
            return m_impl->manager.executeLine(line, context);
        }
    }
    return execute(line, context, true, true);
}
rm::CommandResult PythonRuntime::executeScript(const std::string &source, const rm::CommandContext &context,
                                                 const std::string &scriptPath, const std::vector<std::string> &arguments)
{
    if (isNativeCommandScript(m_impl->manager, source))
    {
        return executeCommandScript(m_impl->manager, source, context, m_impl->exitRequested);
    }
    const auto code = source.starts_with("\xEF\xBB\xBF") ? source.substr(3) : source;
    return execute(code, context, false, true, scriptPath, arguments);
}
rm::CommandResult PythonRuntime::executeNative(const std::string &line, std::size_t lineNumber)
{
    const auto output = [this](const std::string &text) {
        if (m_impl->context.output)
        {
            m_impl->context.output(text);
        }
    };
    // Also log native commands executed inside an interactive Python block.
    output(commandExecutionEcho(line));
    auto result = m_impl->manager.executeLine(line, m_impl->context);
    if (!result.ok() && m_impl->scriptCommands)
    {
        result.message = "第 " + std::to_string(lineNumber) + " 行：" + result.message;
        auto details = result.value.is<rm::Arguments>() ? result.value.as<rm::Arguments>() : rm::Arguments{};
        details["scriptLine"] = static_cast<std::uint64_t>(lineNumber);
        result.value = std::move(details);
        output("未完成：" + result.message + '\n');
    }
    else if (result.ok())
    {
        const auto feedback = commandFeedback(result);
        if (!feedback.empty())
        {
            output(feedback + '\n');
        }
    }
    return result;
}
rm::CommandResult PythonRuntime::execute(const std::string &code, const rm::CommandContext &context, bool interactive,
                                        bool mixedCommands, const std::string &scriptPath,
                                        const std::vector<std::string> &arguments)
{
    currentRuntime = this;
    m_impl->context = context;
    m_impl->interactive = interactive;
    m_impl->scriptCommands = mixedCommands && !interactive;
    struct Reset
    {
        Impl &impl;
        ~Reset()
        {
            impl.displayValue = py::object{};
            impl.displayText.clear();
            impl.interactive = false;
            impl.scriptCommands = false;
            currentRuntime = nullptr;
        }
    } reset{*m_impl};
    try
    {
        m_impl->initialize();
        if (context.stop.stop_requested())
        {
            resetConsole();
            return rm::CommandResult::error("cancelled", "Script cancelled");
        }
        auto sys = py::module_::import("sys");
        // Script invocation metadata is local to this run, even on error/cancellation or input().
        struct ScriptScope
        {
            py::object sys;
            py::dict globals;
            py::object argv;
            py::object file;
            bool hadFile{};
            bool active{};
            ~ScriptScope()
            {
                if (!active) return;
                sys.attr("argv") = argv;
                if (hadFile) globals["__file__"] = file;
                else globals.attr("pop")("__file__", py::none());
            }
        } scriptScope{sys, *m_impl->globals, {}, {}, false, false};
        if (!scriptPath.empty())
        {
            scriptScope.argv = sys.attr("argv");
            scriptScope.hadFile = m_impl->globals->contains("__file__");
            if (scriptScope.hadFile) scriptScope.file = (*m_impl->globals)["__file__"];
            scriptScope.active = true;
            py::list argv;
            argv.append(py::str(scriptPath));
            for (const auto &argument : arguments) argv.append(py::str(argument));
            sys.attr("argv") = argv;
            (*m_impl->globals)["__file__"] = py::str(scriptPath);
        }
        py::object compiled;
        if (interactive)
        {
            if (!m_impl->pendingSource.empty())
            {
                m_impl->pendingSource += '\n';
            }
            m_impl->pendingSource += code;
            const auto source = mixedCommands ? (*m_impl->transform)(m_impl->pendingSource).cast<std::string>()
                                              : m_impl->pendingSource;
            compiled = (*m_impl->compiler)(source, "<console>", "single");
            if (compiled.is_none())
            {
                return {rm::ResultKind::Success, {}, {}, rm::Arguments{{"more", true}}, false};
            }
            resetConsole();
        }
        py::object oldOut = sys.attr("stdout");
        py::object oldError = sys.attr("stderr");
        py::object oldInput = sys.attr("stdin");
        py::object oldDisplay = sys.attr("displayhook");
        py::exec("class _RmWriter:\n    encoding = 'utf-8'\n    def write(self, text):\n        rm._write(text)\n      "
                 "  return len(text)\n    "
                 "def flush(self):\n        pass\n    def isatty(self):\n        return False\n"
                 "class _RmReader:\n    encoding = 'utf-8'\n    def readline(self, size=-1):\n        return "
                 "rm._readline()\n"
                 "    def isatty(self):\n        return False\n",
                 *m_impl->globals);
        sys.attr("stdout") = (*m_impl->globals)["_RmWriter"]();
        sys.attr("stderr") = sys.attr("stdout");
        sys.attr("stdin") = (*m_impl->globals)["_RmReader"]();
        struct Restore
        {
            py::object sys, out, error, input, display;
            ~Restore()
            {
                PyEval_SetTrace(nullptr, nullptr);
                sys.attr("stdout") = out;
                sys.attr("stderr") = error;
                sys.attr("stdin") = input;
                sys.attr("displayhook") = display;
            }
        } restore{sys, oldOut, oldError, oldInput, oldDisplay};
        if (interactive)
        {
            sys.attr("displayhook") = py::cpp_function([this, oldDisplay](const py::object &value) {
                if (!value.is_none() && m_impl->displayValue && value.is(m_impl->displayValue))
                {
                    if (m_impl->context.output)
                    {
                        m_impl->context.output(m_impl->displayText + "\n");
                    }
                    py::module_::import("builtins").attr("_") = value;
                }
                else
                {
                    oldDisplay(value);
                }
            });
        }
        PyEval_SetTrace(traceCancellation, Py_None);
        if (interactive)
        {
            const auto result = py::reinterpret_steal<py::object>(
                PyEval_EvalCode(compiled.ptr(), m_impl->globals->ptr(), m_impl->globals->ptr()));
            if (!result)
            {
                throw py::error_already_set();
            }
            return {rm::ResultKind::Success, {}, {}, rm::Arguments{{"more", false}}, false};
        }
        if (mixedCommands)
        {
            const auto source = (*m_impl->transform)(code).cast<std::string>();
            auto script = py::reinterpret_steal<py::object>(Py_CompileString(source.c_str(), scriptPath.empty() ? "<script>" : scriptPath.c_str(), Py_file_input));
            if (!script)
            {
                throw py::error_already_set();
            }
            auto result = py::reinterpret_steal<py::object>(
                PyEval_EvalCode(script.ptr(), m_impl->globals->ptr(), m_impl->globals->ptr()));
            if (!result)
            {
                throw py::error_already_set();
            }
            return {rm::ResultKind::Success, {}, "脚本执行完成", {}, false};
        }
        py::exec(code, *m_impl->globals);
        return {rm::ResultKind::Success, {}, "Python 脚本执行完成", {}, false};
    }
    catch (const py::error_already_set &error)
    {
        resetConsole();
        if (exitRequested() && error.matches(PyExc_SystemExit))
            return {rm::ResultKind::Success, {}, "STEP 与结果 JSON 已提交，正在退出。", {}, true};
        if (context.diagnostic)
        {
            context.diagnostic(error.what());
        }
        auto result = pythonFailure(error);
        if (m_impl->scriptCommands && result.code == "python_error")
        {
            std::size_t lineNumber = 0;
            const auto value = error.value();
            if (py::hasattr(value, "lineno") && !value.attr("lineno").is_none())
            {
                lineNumber = value.attr("lineno").cast<std::size_t>();
            }
            py::object trace = value.attr("__traceback__");
            while (!trace.is_none())
            {
                if (py::str(trace.attr("tb_frame").attr("f_code").attr("co_filename")).cast<std::string>() ==
                    (scriptPath.empty() ? "<script>" : scriptPath))
                {
                    lineNumber = trace.attr("tb_lineno").cast<std::size_t>();
                }
                trace = trace.attr("tb_next");
            }
            if (lineNumber)
            {
                result.message = "第 " + std::to_string(lineNumber) + " 行：" + result.message;
                result.value = rm::Arguments{{"scriptLine", static_cast<std::uint64_t>(lineNumber)}};
            }
        }
        return result;
    }
    catch (const std::exception &error)
    {
        resetConsole();
        return rm::CommandResult::error("python_runtime_error", error.what());
    }
}
} // namespace reverse
