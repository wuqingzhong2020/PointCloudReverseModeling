/* 文件描述：内建 rm 模块：严格转换 Python 类型，在命令线程同步使用库 A。 */
#include "commands/PythonRuntime.h"
#include <cmath>
#include <pybind11/embed.h>
namespace py = pybind11;
namespace reverse
{
namespace
{
rm::Value fromPython(py::handle object, const std::string &path, int depth = 0)
{
    if (depth > 32)
    {
        throw rm::InputError("nesting_limit", path, "嵌套深度 <= 32", "嵌套过深");
    }
    if (object.is_none())
    {
        return {};
    }
    if (PyBool_Check(object.ptr()))
    {
        return object.is(py::bool_(true));
    }
    if (PyLong_Check(object.ptr()))
    {
        int overflow{};
        const auto signedValue = PyLong_AsLongLongAndOverflow(object.ptr(), &overflow);
        if (!overflow && !PyErr_Occurred())
        {
            return static_cast<std::int64_t>(signedValue);
        }
        if (PyErr_Occurred())
        {
            PyErr_Clear();
        }
        if (overflow > 0)
        {
            const auto unsignedValue = PyLong_AsUnsignedLongLong(object.ptr());
            if (!PyErr_Occurred())
            {
                return static_cast<std::uint64_t>(unsignedValue);
            }
            PyErr_Clear();
        }
        throw rm::InputError("invalid_value", path, "int64/uint64", "超出 64 位范围的整数");
    }
    if (PyFloat_Check(object.ptr()))
    {
        return py::cast<double>(object);
    }
    if (PyUnicode_Check(object.ptr()))
    {
        return py::cast<std::string>(object);
    }
    if (PyList_Check(object.ptr()) || PyTuple_Check(object.ptr()))
    {
        rm::Array values;
        for (const auto item : py::reinterpret_borrow<py::sequence>(object))
        {
            values.push_back(fromPython(item, path + "[" + std::to_string(values.size()) + "]", depth + 1));
        }
        return values;
    }
    if (PyDict_Check(object.ptr()))
    {
        rm::Arguments values;
        for (const auto item : py::reinterpret_borrow<py::dict>(object))
        {
            if (!PyUnicode_Check(item.first.ptr()))
            {
                throw rm::InputError("invalid_value", path, "字符串类型的对象键", "非字符串键");
            }
            const auto key = py::cast<std::string>(item.first);
            values.emplace(key, fromPython(item.second, path.empty() ? key : path + "." + key, depth + 1));
        }
        return values;
    }
    throw rm::InputError("invalid_value", path, "bool/int/float/string/list/tuple/dict/None",
                         py::str(py::type::of(object).attr("__name__")).cast<std::string>());
}
py::object resultValue(const rm::CommandResult &result)
{
    if (!result.ok())
    {
        auto error = py::module_::import("rm").attr("CommandError")(result.message);
        error.attr("code") = result.code;
        error.attr("committed") = result.committed;
        error.attr("details") = py::module_::import("json").attr("loads")(rm::formatValue(result.value));
        PyErr_SetObject(py::module_::import("rm").attr("CommandError").ptr(), error.ptr());
        throw py::error_already_set();
    }
    py::object value = result.kind == rm::ResultKind::Help
                           ? py::object(py::str(result.message))
                           : py::module_::import("json").attr("loads")(rm::formatValue(result.value));
    PythonRuntime::current().rememberCommandResult(value, result);
    return value;
}
} // namespace
void linkPythonCommandBindings()
{
}
PYBIND11_EMBEDDED_MODULE(rm, module, py::mod_gil_not_used())
{
    py::exec("class CommandError(RuntimeError):\n    pass\n", module.attr("__dict__"));
    module.def("execute", [](py::args args, py::kwargs kwargs) {
        auto &runtime = PythonRuntime::current();
        if (args.empty() || !PyUnicode_Check(args[0].ptr()))
        {
            throw py::type_error("execute requires a positional command name");
        }
        const auto name = py::cast<std::string>(args[0]);
        try
        {
            const auto definition = runtime.manager().definitions().find(name);
            rm::Array positional;
            for (py::size_t i = 1; i < args.size(); ++i)
            {
                auto path = "position " + std::to_string(i);
                if (definition != runtime.manager().definitions().end())
                {
                    for (const auto &parameter : definition->second.parameters)
                    {
                        if (parameter.binding == rm::Binding::Positional && parameter.position == i)
                        {
                            path = parameter.name;
                            break;
                        }
                    }
                }
                positional.push_back(fromPython(args[i], path));
            }
            const auto options = fromPython(kwargs, {}).as<rm::Arguments>();
            return resultValue(runtime.manager().execute(name, positional, options, runtime.context()));
        }
        catch (const rm::InputError &error)
        {
            auto result = rm::CommandResult::error(error.code, error.what());
            result.value =
                rm::Arguments{{"parameter", error.path}, {"expected", error.expected}, {"actual", error.actual}};
            return resultValue(result);
        }
    });
    module.def("execute_line", [](const std::string &line) {
        auto &runtime = PythonRuntime::current();
        return resultValue(runtime.manager().executeLine(line, runtime.context()));
    });
    module.def(
        "help", [](const std::string &target) {
            auto &runtime = PythonRuntime::current();
            return resultValue(runtime.manager().help(target));
        },
        py::arg("target") = "");
    module.def("_execute_native", [](const std::string &line, std::size_t lineNumber) {
        const auto result = PythonRuntime::current().executeNative(line, lineNumber);
        if (!result.ok())
        {
            (void)resultValue(result);
        }
    });
    module.def("_write", [](const std::string &text) {
        const auto &context = PythonRuntime::current().context();
        if (context.output)
        {
            context.output(text);
        }
    });
    module.def("_readline", [] {
        const auto line = PythonRuntime::current().readInput();
        return line ? *line + "\n" : std::string{};
    });
}
} // namespace reverse
