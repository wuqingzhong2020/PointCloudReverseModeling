"""Read literal-only rm_define declarations; never execute definition files."""
from __future__ import annotations
import ast
import json
import math
from pathlib import Path
import re

IDENTIFIER = re.compile(r"[a-z][a-zA-Z0-9_]*\Z")
CPP_RESERVED = set("class struct enum union template typename namespace using public private protected auto bool int float double char void const return new delete default operator switch case this virtual static inline friend try catch throw if else while for do break continue true false null nullptr requires concept co_await import module export".split())
CPP_RESERVED.update("alignas alignof and and_eq asm bitand bitor char8_t char16_t char32_t compl consteval constexpr constinit const_cast co_return co_yield decltype dynamic_cast explicit extern goto long mutable noexcept not not_eq or or_eq register reinterpret_cast short signed sizeof static_assert static_cast thread_local typedef typeid unsigned volatile wchar_t xor xor_eq".split())
TYPES = {"bool", "int", "uint", "float", "string", "path", "enum", "array", "object"}


class DefinitionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise DefinitionError(message)


def identifier(value):
    return isinstance(value, str) and bool(IDENTIFIER.fullmatch(value)) and "__" not in value and value not in CPP_RESERVED


def generated_class(name):
    return "".join(word[:1].upper() + word[1:] for word in name.split("_"))


def literal(node, depth=0):
    require(depth <= 32, "declaration nesting exceeds 32")
    if isinstance(node, ast.Constant) and type(node.value) in (str, int, float, bool, type(None)):
        return node.value
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.USub, ast.UAdd)):
        value = literal(node.operand, depth + 1)
        require(type(value) in (int, float), "unary signs require a number")
        return -value if isinstance(node.op, ast.USub) else value
    if isinstance(node, (ast.List, ast.Tuple)):
        return [literal(item, depth + 1) for item in node.elts]
    if isinstance(node, ast.Dict):
        result = {}
        for key_node, value_node in zip(node.keys, node.values):
            require(key_node is not None, "dictionary expansion is forbidden")
            key = literal(key_node, depth + 1)
            require(isinstance(key, str) and key not in result, "duplicate/non-string dictionary key")
            result[key] = literal(value_node, depth + 1)
        return result
    raise DefinitionError(f"line {getattr(node, 'lineno', '?')}: only literal declarations are allowed")


def valid_value(rule, value):
    kind = rule["type"]
    if value is None:
        return rule.get("nullable", False)
    correct = {
        "bool": lambda: type(value) is bool,
        "int": lambda: type(value) is int and -(2**63) <= value < 2**63,
        "uint": lambda: type(value) is int and 0 <= value < 2**64,
        "float": lambda: type(value) in (int, float) and math.isfinite(value),
        "string": lambda: isinstance(value, str) and "\0" not in value,
        "path": lambda: isinstance(value, str) and "\0" not in value,
        "enum": lambda: isinstance(value, str) and value in rule["values"],
        "array": lambda: isinstance(value, list) and all(valid_value(rule["items"], v) for v in value),
        "object": lambda: isinstance(value, dict) and value.keys() == rule.get("properties", {}).keys()
            and all(valid_value(rule["properties"][k], v) for k, v in value.items()),
    }[kind]()
    if not correct:
        return False
    if "ranges" in rule and not any(lo <= value <= hi for lo, hi in rule["ranges"]):
        return False
    return not isinstance(value, (str, list)) or rule.get("min_size", 0) <= len(value) <= rule.get("max_size", 100000)


def check_rule(rule, depth=0):
    require(isinstance(rule, dict) and depth <= 32, "invalid/deep rule")
    require(rule.get("type") in TYPES, "unknown parameter type")
    kind = rule["type"]
    require(type(rule.get("nullable", False)) is bool, "nullable must be bool")
    if kind == "enum":
        values = rule.get("values")
        require(isinstance(values, list) and values and all(isinstance(v, str) and v and "\0" not in v for v in values), "enum requires values")
        require(len(set(values)) == len(values), "duplicate enum values")
    if "ranges" in rule:
        require(kind in ("int", "uint", "float") and isinstance(rule["ranges"], list) and rule["ranges"], "ranges require a numeric type")
        for interval in rule["ranges"]:
            require(isinstance(interval, list) and len(interval) == 2, "range must contain two endpoints")
            base = {"type": kind}
            require(all(valid_value(base, x) for x in interval) and interval[0] <= interval[1], "invalid range endpoints")
    minimum, maximum = rule.get("min_size", 0), rule.get("max_size", 100000)
    require(type(minimum) is int and type(maximum) is int and 0 <= minimum <= maximum, "invalid size bounds")
    if kind == "array":
        check_rule(rule.get("items"), depth + 1)
    if kind == "object":
        require(isinstance(rule.get("properties"), dict), "object requires declared properties")
        for name, field in rule["properties"].items():
            require(identifier(name), "invalid property name")
            check_rule(field, depth + 1)


def check_command(command):
    require(not (command.keys() - {"name", "summary", "usage", "description", "params", "returns", "examples"}), "unknown rm_define field (business handlers do not belong in definitions)")
    require(isinstance(command.get("name"), str) and IDENTIFIER.fullmatch(command["name"])
            and command["name"] != "help", "invalid/reserved command name")
    for field in ("summary", "usage"):
        require(isinstance(command.get(field), str) and command[field].strip(), f"{field} must be a nonblank string")
    command.setdefault("description", "")
    command.setdefault("params", [])
    command.setdefault("returns", {"type": "object", "description": "命令结果"})
    command.setdefault("examples", [])
    require(isinstance(command["description"], str), "description must be a string")
    require(isinstance(command["returns"], dict) and isinstance(command["returns"].get("type"), str)
            and isinstance(command["returns"].get("description", ""), str), "invalid returns")
    require(isinstance(command["examples"], list) and all(isinstance(e, str) for e in command["examples"]), "invalid examples")
    require(isinstance(command["params"], list), "params must be a list")
    names, options, positions, enum_types = set(), set(), {}, set()
    allowed = {"name", "binding", "option", "position", "type", "nullable", "values", "ranges", "items", "properties", "min_size", "max_size", "required", "default", "description", "unit"}
    for param in command["params"]:
        require(isinstance(param, dict) and not (param.keys() - allowed), "unknown parameter field")
        require(identifier(param.get("name")) and param["name"] not in names, "invalid/duplicate parameter name")
        names.add(param["name"])
        binding = param.get("binding")
        require(binding in ("option", "positional"), "binding must be option or positional")
        if binding == "option":
            option = param.get("option", "")
            require(isinstance(option, str) and re.fullmatch(r"--[a-zA-Z][\w-]*", option)
                    and option not in options and option != "--help" and "position" not in param, "invalid/duplicate/reserved option")
            options.add(option)
        else:
            position = param.get("position")
            require(type(position) is int and position > 0 and position not in positions and "option" not in param, "invalid/duplicate position")
            positions[position] = param.get("required", False)
        require(type(param.get("required", False)) is bool, "required must be bool")
        require(not (param.get("required") and "default" in param), "required cannot have a default")
        for field in ("description", "unit"):
            require(isinstance(param.get(field, ""), str), f"{field} must be a string")
        check_rule(param)
        if param["type"] == "enum":
            enum_type = generated_class(param["name"])
            require(enum_type not in enum_types, "generated enum type name collision")
            enum_types.add(enum_type)
        require("default" not in param or valid_value(param, param["default"]), "default violates parameter rule")
    require(sorted(positions) == list(range(1, len(positions) + 1)), "positions must be contiguous from 1")
    seen_optional = False
    for index in sorted(positions):
        require(not (seen_optional and positions[index]), "required positional after optional")
        seen_optional = not positions[index]


def load_definition(path):
    path = Path(path)
    require(path.stat().st_size <= 1024 * 1024, "definition exceeds 1 MiB")
    try:
        tree = ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
        module = None
        commands = []
        for statement in tree.body:
            require(isinstance(statement, ast.Expr) and isinstance(statement.value, ast.Call), "only rm_module/rm_define declarations allowed")
            call = statement.value
            require(isinstance(call.func, ast.Name) and call.func.id in ("rm_module", "rm_define") and not call.args, "invalid declaration call")
            args = {}
            for keyword in call.keywords:
                require(keyword.arg is not None and keyword.arg not in args, "duplicate/expanded keyword")
                args[keyword.arg] = literal(keyword.value)
            if call.func.id == "rm_module":
                require(module is None and not commands, "exactly one rm_module must precede commands")
                require(not (args.keys() - {"name", "summary"}) and identifier(args.get("name"))
                        and args["name"] != "help", "invalid module")
                require(isinstance(args.get("summary"), str) and args["summary"].strip(), "module summary required")
                module = args
            else:
                require(module is not None, "rm_module required first")
                check_command(args)
                require(all(generated_class(c["name"]).casefold() != generated_class(args["name"]).casefold()
                            for c in commands), "duplicate command or generated class/file name collision")
                commands.append(args)
        require(module is not None and commands, "module requires commands")
        return {**module, "commands": commands}
    except (SyntaxError, ValueError, TypeError, RecursionError) as error:
        line = getattr(error, "lineno", None) or (call.lineno if 'call' in locals() else 1)
        raise DefinitionError(f"{path.name}:{line}: {error}") from error


def load_manifest(path):
    path = Path(path).resolve()
    data = json.loads(path.read_text(encoding="utf-8-sig"))
    require(data.get("schema") == 1 and isinstance(data.get("modules"), list), "invalid modules manifest")
    modules, names = [], set()
    for entry in data["modules"]:
        require(isinstance(entry, dict) and identifier(entry.get("name"))
                and entry["name"].casefold() not in names, "duplicate/invalid module name")
        names.add(entry["name"].casefold())
        require(type(entry.get("enabled", True)) is bool, "enabled must be bool")
        source = (path.parent / entry["file"]).resolve()
        require(source.is_relative_to(path.parent), "module path escapes definitions directory")
        if entry.get("enabled", True):
            module = load_definition(source)
            require(module["name"] == entry["name"], "manifest/module name mismatch")
            modules.append(module)
    return modules
