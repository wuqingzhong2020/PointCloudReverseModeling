"""工程构建入口：中文帮助、严格参数校验及九个命令的分发。"""

import argparse
import difflib
from pathlib import Path
import sys

from pythonProject.cli_help import CONFIG_CLEAN_KINDS, SHARED_CLEAN_KINDS, HelpParser
from pythonProject.environment import BuildError, Environment, initialize, show_info

MODES = ("release", "debug", "release_debug")
DESCRIPTIONS = {
    "info": "只读显示环境、配置、工具和产物状态",
    "init": "创建缺失的本地配置并检查环境，不编译",
    "debug": "用 CDB 调试 Debug 主程序，不编译",
    "release": "直接运行 Release 主程序，不编译",
    "clean": "清理指定产物；默认保留运行依赖和发布包",
    "compile": "配置、编译并自动部署主程序运行环境，不运行测试",
    "prepareRun": "手动补齐已有程序的 DLL、Qt 插件和资源，不编译",
    "package": "用 CPack 安装已有产物及依赖并生成 ZIP，不编译",
    "packageAll": "增量编译主程序、自动部署并用 CPack 打包",
}


EXAMPLES = {
    "info": ("info",),
    "init": ("init --dry-run", "init"),
    "debug": ("compile debug", "debug --dry-run", "debug"),
    "release": ("compile release", "release --dry-run", "release"),
    "clean": ("clean out release --dry-run", "clean out debug", "clean 3rd release",
              "clean out_3rd release_debug", "clean build --dry-run", "clean package",
              "clean all --dry-run"),
    "compile": ("compile release", "compile debug --target ReverseModeler --jobs 8",
                "compile release --target ReverseServiceTests", "compile release_debug",
                "compile release --rebuild", "compile release --fresh --dry-run"),
    "prepareRun": ("prepareRun release", "prepareRun debug --dry-run", "prepareRun release_debug"),
    "package": ("package release", "package debug", "package release_debug --dry-run"),
    "packageAll": ("packageAll release --jobs 8", "packageAll release_debug",
                   "packageAll release --rebuild --dry-run"),
}


def positive_int(value: str) -> int:
    """解析用户显式指定的正整数并行数。"""
    try:
        result = int(value)
    except ValueError as error:
        raise argparse.ArgumentTypeError("并行数必须是正整数") from error
    if result < 1:
        raise argparse.ArgumentTypeError("并行数必须是正整数")
    return result


def make_parser() -> HelpParser:
    """建立不依赖本地配置或 SDK 的完整帮助。"""
    parser = HelpParser(
        prog="python envBuild.py",
        description="ReverseModeler 构建、运行与打包工具",
        epilog="常用示例：\n"
               "  python envBuild.py init\n"
               "  python envBuild.py compile debug --target ReverseModeler --jobs 8\n"
               "  python envBuild.py clean out release --dry-run\n"
               "  python envBuild.py packageAll release\n"
               "命令详情：python envBuild.py <命令> -h",
    )
    commands = parser.add_subparsers(dest="command", title="命令及作用")
    for name, description in DESCRIPTIONS.items():
        child = commands.add_parser(name, help=description, description=description)
        if name != "info":
            child.add_argument("--dry-run", action="store_true", help="仅预览，不修改或启动")
        if name in ("compile", "prepareRun", "package", "packageAll"):
            child.add_argument("mode", nargs="?", choices=MODES,
                               help="构建模式；省略时读取本地配置，初始 release")
        if name == "compile":
            child.add_argument("--target", choices=("ReverseModeler", "ReverseServiceTests"),
                               metavar="<目标>",
                               help="只编译指定目标；默认编译两者，不运行测试")
        if name in ("compile", "packageAll"):
            child.add_argument("--jobs", type=positive_int, metavar="<数量>",
                               help="并行数（正整数）；省略时读取 parallel_jobs，0 表示工具默认")
            child.add_argument("--rebuild", action="store_true", help="清理所选配置后编译")
            child.add_argument("--fresh", action="store_true",
                               help="重建共享 CMake 配置缓存；Debug/Release 的配置均受影响")
        if name == "clean":
            child.add_argument("kind", nargs="?", default="out",
                               choices=CONFIG_CLEAN_KINDS + SHARED_CLEAN_KINDS,
                               help="out 编译产物；3rd 已登记依赖；out_3rd 两者；"
                                    "build 共享缓存；package 打包暂存；all 上述全部，保留 ZIP")
            child.add_argument("mode", nargs="?", choices=MODES, help="仅前三种类别可指定模式")
        child.epilog = "示例：\n" + "\n".join("  python envBuild.py " + s for s in EXAMPLES[name])
        if name in ("package", "packageAll"):
            child.epilog += "\n输出：output/packages/*.zip 及 SHA256；ZIP 内从 bin/ReverseModeler.exe 运行。"
        elif name in ("debug", "release"):
            child.epilog += f"\n前置：先 compile {name}；运行文件缺失时执行 prepareRun {name}。"
        elif name == "clean":
            child.epilog += (
                "\n清理范围：\n"
                "  out      清理所选配置的编译产物，保留已部署依赖。默认类别。\n"
                "  3rd      清理所选配置已登记且未被修改的运行依赖，保留程序。\n"
                "  out_3rd  同时执行上述两项。\n"
                "  build    删除当前预设的整个构建目录，Debug/Release 共用；保留 output。\n"
                "  package  删除 output/package-staging 暂存目录，保留发布 ZIP。\n"
                "  all      清理两配置的编译产物、已登记依赖、构建目录及打包暂存。\n"
                "保留源码、本地配置、外部 SDK 和已完成 ZIP；未登记的运行文件保留。\n"
                "build 和 package 删除整个相应目录，请勿在构建或暂存目录存放个人文件。"
            )
    return parser


def main(argv: list[str] | None = None, root: Path | None = None) -> int:
    """执行一次 CLI 命令；返回 0/1/2，不在模块导入时产生副作用。"""
    arguments = list(sys.argv[1:] if argv is None else argv)
    parser = make_parser()
    if not arguments:
        parser.print_help()
        return 0
    if arguments[0] not in DESCRIPTIONS and arguments[0] not in ("-h", "--help"):
        suggestion = difflib.get_close_matches(arguments[0], DESCRIPTIONS, n=1)
        extra = "Qt 已并入 prepareRun，编译后也会自动部署。" if arguments[0] == "deployqt" else (
            f"可能想输入：{suggestion[0]}" if suggestion else ""
        )
        parser.error(f"未知命令 {arguments[0]!r}。{extra}")
    options, unknown = parser.parse_known_args(arguments)
    children = next(a for a in parser._actions if isinstance(a, argparse._SubParsersAction))
    if unknown:
        child = children.choices.get(options.command, parser)
        flags = [flag for action in child._actions for flag in action.option_strings]
        matches = difflib.get_close_matches(unknown[0], flags, n=1)
        hint = f"；可能想输入 {matches[0]}" if matches else ""
        child.error(f"未知参数：{' '.join(unknown)}{hint}")
    if options.command == "clean" and options.kind in SHARED_CLEAN_KINDS and options.mode:
        children.choices["clean"].error("该清理类别作用于共享目录，不接受单配置参数")
    project_root = (root or Path(__file__).resolve().parent).resolve()
    try:
        if options.command == "init":
            return initialize(project_root, options.dry_run)
        if options.command == "info":
            return show_info(project_root)
        env = Environment.read(project_root)
        if options.command in ("debug", "release"):
            from pythonProject.program_launcher import launch
            return launch(env, options.command, options.dry_run)
        if options.command == "clean":
            from pythonProject.build import clean
            clean(env, options.kind, options.mode, options.dry_run)
        elif options.command == "prepareRun":
            from pythonProject.deploy import prepare
            for mode in env.modes(options.mode):
                prepare(env, mode, options.dry_run)
        elif options.command == "compile":
            from pythonProject.build import compile_project
            compile_project(env, options)
        else:
            from pythonProject.package import package_project
            package_project(env, options)
        return 0
    except (BuildError, OSError, ValueError) as error:
        print(f"错误：{error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("操作已中止。", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
