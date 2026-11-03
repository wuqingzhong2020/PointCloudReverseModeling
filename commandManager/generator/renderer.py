"""Pure descriptor rendering. Generated code contains no product handlers."""
import json
import re
from pathlib import Path
from jinja2 import Environment, FileSystemLoader, StrictUndefined


def cpp_string(value):
    # JSON escapes are C++ compatible except ensure_ascii surrogate pairs; keep UTF-8.
    text = json.dumps(value, ensure_ascii=False)
    return re.sub(r"\\u00([0-9a-f]{2})", lambda m: "\\" + format(int(m[1], 16), "03o"), text)


def cpp_value(value):
    if value is None:
        return "rm::Value{}"
    if type(value) is bool:
        return "rm::Value{" + str(value).lower() + "}"
    if type(value) is int:
        if value == -(2**63):
            return "rm::Value{std::int64_t{(-9223372036854775807LL - 1)}}"
        kind, suffix = ("uint64_t", "ULL") if value > 2**63 - 1 else ("int64_t", "LL")
        return f"rm::Value{{std::{kind}{{{value}{suffix}}}}}"
    if type(value) is float:
        return f"rm::Value{{double{{{value!r}}}}}"
    if isinstance(value, str):
        return f"rm::Value{{{cpp_string(value)}}}"
    if isinstance(value, list):
        return "rm::Value{rm::Array{" + ", ".join(cpp_value(v) for v in value) + "}}"
    return "rm::Value{rm::Arguments{" + ", ".join("{" + cpp_string(k) + ", " + cpp_value(v) + "}" for k, v in value.items()) + "}}"


def cpp_rule(rule):
    kinds = {"bool": "Boolean", "int": "Integer", "uint": "Unsigned", "float": "Number", "string": "String", "path": "Path", "enum": "Enum", "array": "Array", "object": "Object"}
    body = [f"rm::Rule r; r.type = rm::ValueType::{kinds[rule['type']]};"]
    if rule.get("nullable"):
        body.append("r.nullable = true;")
    if "values" in rule:
        body.append("r.choices = {" + ", ".join(cpp_string(v) for v in rule["values"]) + "};")
    if "ranges" in rule:
        body.append("r.ranges = {" + ", ".join("{" + cpp_value(lo) + ", " + cpp_value(hi) + "}" for lo, hi in rule["ranges"]) + "};")
    if "items" in rule:
        body.append("r.items = std::make_shared<rm::Rule>(" + cpp_rule(rule["items"]) + ");")
    if "properties" in rule:
        body.append("r.properties = {" + ", ".join("{" + cpp_string(k) + ", " + cpp_rule(v) + "}" for k, v in rule["properties"].items()) + "};")
    for key, field in (("min_size", "minSize"), ("max_size", "maxSize")):
        if key in rule:
            body.append(f"r.{field} = {rule[key]};")
    return "[] { " + " ".join(body) + " return r; }()"


def class_name(name):
    return "".join(word[:1].upper() + word[1:] for word in name.split("_"))


def decorate(command):
    command = dict(command, class_name=class_name(command["name"]) + "Command")
    params = []
    for p in command["params"]:
        p = dict(p)
        kind = p["type"]
        if p.get("nullable"):
            cpp_type = "rm::Value"
        elif kind == "enum":
            cpp_type = class_name(p["name"]) + "Value"
        else:
            cpp_type = {"bool": "bool", "int": "std::int64_t", "uint": "std::uint64_t", "float": "double", "string": "std::string", "path": "std::string", "array": "rm::Array", "object": "rm::Arguments"}[kind]
        p["base_type"] = cpp_type
        p["optional"] = not p.get("required", False) and "default" not in p
        p["cpp_type"] = f"std::optional<{cpp_type}>" if p["optional"] else cpp_type
        params.append(p)
    command["params"] = params
    return command


def render_all(modules, template_dir, *, sources=True):
    env = Environment(loader=FileSystemLoader(template_dir), undefined=StrictUndefined,
                      keep_trailing_newline=True, autoescape=False, trim_blocks=True, lstrip_blocks=True)
    env.filters.update(cpp=cpp_string, value=cpp_value, rule=cpp_rule)
    help_json = json.dumps({"schema": 1, "modules": modules}, ensure_ascii=False, indent=2) + "\n"
    modules = [dict(m, class_name=class_name(m["name"]) + "Module",
                    commands=[decorate(c) for c in m["commands"]]) for m in modules]
    files, docs = {}, {"commands.help.json": help_json}
    for module in modules:
        for command in module["commands"]:
            if not sources:
                continue
            for ext in ("h", "cpp"):
                files[f"{module['name']}/{command['class_name']}.{ext}"] = env.get_template(f"command.{ext}.j2").render(module=module, command=command)
        for ext in ("h", "cpp") if sources else ():
            files[f"{module['name']}/{module['class_name']}.{ext}"] = env.get_template(f"module.{ext}.j2").render(module=module)
        docs[f"{module['name']}.md"] = env.get_template("module.md.j2").render(module=module)
    for ext in ("h", "cpp") if sources else ():
        files[f"CommandModules.{ext}"] = env.get_template(f"command_modules.{ext}.j2").render(modules=modules)
    return files, docs
