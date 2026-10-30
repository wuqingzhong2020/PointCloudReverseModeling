"""根据 argparse 参数定义生成完整命令速查及中文子命令帮助。"""

import argparse
import shutil
import sys

CONFIG_CLEAN_KINDS = ("out", "3rd", "out_3rd")
SHARED_CLEAN_KINDS = ("build", "package", "all")
GUIDE_PATH = "docs/envBuild命令使用指南.md"


class HelpParser(argparse.ArgumentParser):
    """帮助与输入校验共用参数定义；输入错误只显示帮助，不执行操作。"""

    def __init__(self, *args, **kwargs) -> None:
        """使用纯文本中文帮助，禁用长选项缩写。"""
        kwargs["allow_abbrev"] = False
        kwargs["color"] = False
        super().__init__(*args, **kwargs)
        self._optionals.title = "选项"
        self._positionals.title = "位置参数"
        for action in self._actions:
            if isinstance(action, argparse._HelpAction):
                action.help = "显示本帮助并退出"

    def command_forms(self) -> list[list[str]]:
        """从已注册参数生成合法命令形式，清理共享目录时不附带模式。"""
        positionals = [a for a in self._actions if not a.option_strings]
        options = [a for a in self._actions if a.option_strings]
        # 把主要操作参数放在前面，预览和帮助放在最后。
        options.sort(key=lambda a: (a.dest in ("dry_run", "help"), a.dest == "help"))
        suffix = []
        for action in options:
            if isinstance(action, argparse._HelpAction):
                suffix.append("[-h | --help]")
                continue
            token = action.option_strings[0]
            if action.nargs != 0:
                value = ("{" + " | ".join(action.choices) + "}" if action.choices
                         else action.metavar or action.dest.upper())
                token += " " + value
            suffix.append(f"[{token}]")
        if any(a.dest == "kind" for a in positionals):
            modes = next(a.choices for a in positionals if a.dest == "mode")
            return [
                [self.prog, "[" + " | ".join(CONFIG_CLEAN_KINDS) + "]",
                 "[" + " | ".join(modes) + "]", *suffix],
                [self.prog, "{" + " | ".join(SHARED_CLEAN_KINDS) + "}", *suffix],
            ]
        values = []
        for action in positionals:
            token = " | ".join(action.choices) if action.choices else action.dest
            values.append(f"[{token}]" if action.nargs == "?" else token)
        return [[self.prog, *values, *suffix]]

    def format_help(self) -> str:
        """总帮助列出每条完整语法，子帮助补充参数含义与命令示例。"""
        width = max(76, min(shutil.get_terminal_size().columns, 220))

        def syntax(tokens: list[str]) -> str:
            lines, line = [], "  " + tokens[0]
            for token in tokens[1:]:
                if len(line) + 1 + len(token) > width:
                    lines.append(line)
                    line = "      " + token
                else:
                    line += " " + token
            return "\n".join([*lines, line])

        commands = next((a for a in self._actions
                         if isinstance(a, argparse._SubParsersAction)), None)
        lines = [self.description or self.prog, "", "用法："]
        if commands:
            lines += [f"  {self.prog} [-h | --help]", f"  {self.prog} <命令> [参数]",
                      "", "命令、可跟参数及作用："]
            for child in commands.choices.values():
                lines.extend(syntax(form) for form in child.command_forms())
                lines += [f"      作用：{child.description}", ""]
            lines += ["参数约定：",
                      "  [ ] 表示可选；{ } 表示必选其一；| 表示任选其一；<数量> 填实际正整数。",
                      "  模式省略时读取 envBuild.local.ini 的 default_mode，初始为 release。",
                      "  release_debug 分别处理两个配置；debug/release 启动命令固定配置。",
                      "  clean 默认 out；build/package/all 清理共享范围，不接受模式。",
                      "  --dry-run 只预览；--rebuild 先清理再编译；--fresh 重建共享配置缓存。"]
        else:
            lines.extend(syntax(form) for form in self.command_forms())
            lines += ["", "参数说明："]
            for action in self._actions:
                name = (", ".join(action.option_strings) if action.option_strings
                        else {"mode": "模式", "kind": "清理类别"}.get(action.dest, action.dest))
                if action.option_strings and action.nargs != 0:
                    name += " " + (action.metavar or action.dest.upper())
                lines += [f"  {name}", f"      {action.help}"]
        if self.epilog:
            lines += ["", self.epilog]
        lines += ["", f"详细配置与更多示例：{GUIDE_PATH}"]
        return "\n".join(lines) + "\n"

    def error(self, message: str) -> None:
        print(f"错误：{message}", file=sys.stderr)
        self.print_help(sys.stderr)
        raise SystemExit(2)
