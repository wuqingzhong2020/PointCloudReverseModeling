"""本地配置、CMake 预设解析、受控路径及外部工具执行。"""

import configparser
from dataclasses import dataclass
import json
import os
from pathlib import Path
import shutil
import subprocess


class BuildError(RuntimeError):
    """可直接向命令行用户显示的操作错误。"""


def read_cache(path: Path) -> dict[str, str]:
    """只读解析 CMake 缓存中的键值。"""
    if not path.is_file():
        return {}
    return {line.split(":", 1)[0]: line.split("=", 1)[1]
            for line in path.read_text(encoding="utf-8-sig").splitlines()
            if line and not line.startswith(("#", "//")) and ":" in line and "=" in line}


def checked_path(path: Path, boundary: Path) -> Path:
    """拒绝越界、边界本身以及任何目录联接/符号链接。"""
    boundary = boundary.absolute()
    if boundary.is_symlink() or boundary.is_junction():
        raise BuildError(f"拒绝链接边界：{boundary}")
    boundary = boundary.resolve()
    path = path.absolute()
    for ancestor in (path, *path.parents):
        if ancestor.is_symlink() or ancestor.is_junction():
            raise BuildError(f"拒绝操作链接或联接路径：{ancestor}")
        if ancestor == boundary:
            break
    resolved = path.resolve()
    if resolved == boundary.resolve() or not resolved.is_relative_to(boundary.resolve()):
        raise BuildError(f"路径解析后越界：{path}")
    return resolved


def resolve_preset(root: Path, name: str) -> dict:
    """读取工程预设及继承字段，不另写一套输出目录约定。"""
    data = json.loads((root / "CMakePresets.json").read_text(encoding="utf-8-sig"))
    presets = {p["name"]: p for p in data["configurePresets"]}

    def visit(key: str, seen: set[str]) -> dict:
        if key in seen or key not in presets:
            raise BuildError(f"未知或循环继承的 CMake 预设：{key}")
        entry = presets[key]
        parents = entry.get("inherits", [])
        parents = [parents] if isinstance(parents, str) else parents
        merged = {}
        for parent in reversed(parents):
            merged.update(visit(parent, seen | {key}))
        merged.update(entry)
        return merged

    result = visit(name, set())
    binary = result.get("binaryDir", "")
    binary = binary.replace("${sourceDir}", root.as_posix()).replace("${presetName}", name)
    if not binary or "$" in binary:
        raise BuildError("预设 binaryDir 必须能由 sourceDir/presetName 解析")
    result["binaryDir"] = checked_path(Path(binary), root / "build")
    return result


@dataclass(frozen=True)
class Environment:
    root: Path
    config: configparser.ConfigParser
    preset: dict
    preset_name: str
    build_dir: Path
    prefixes: tuple[str, ...]

    @classmethod
    def read(cls, root: Path) -> "Environment":
        """加载本机配置；缺失时使用示例默认值及已有缓存。"""
        root = root.resolve()
        config = configparser.ConfigParser(interpolation=None)
        try:
            config.read(root / "envBuild.example.ini", encoding="utf-8-sig")
            config.read(root / "envBuild.local.ini", encoding="utf-8-sig")
            name = config.get("build", "configure_preset", fallback="vc143-cpp20")
            preset = resolve_preset(root, name)
            build_dir = preset["binaryDir"]
            raw = config.get("dependencies", "prefix_paths", fallback="")
            if not raw.strip():
                raw = read_cache(build_dir / "CMakeCache.txt").get("CMAKE_PREFIX_PATH", "")
            if not raw.strip():
                raw = os.environ.get("CMAKE_PREFIX_PATH", "")
            paths = [Path(s.strip()).expanduser()
                     for s in raw.replace(";", "\n").splitlines() if s.strip()]
            prefixes = tuple((p if p.is_absolute() else root / p).resolve().as_posix()
                             for p in paths)
            default = config.get("build", "default_mode", fallback="release")
            if default not in ("release", "debug", "release_debug"):
                raise BuildError("default_mode 应为 release、debug 或 release_debug")
            if config.getint("build", "parallel_jobs", fallback=0) < 0:
                raise BuildError("parallel_jobs 不得为负数")
        except configparser.Error as error:
            raise BuildError(f"本地配置错误：{error}") from error
        return cls(root, config, preset, name, build_dir, prefixes)

    def tool(self, name: str) -> Path:
        """查找所需工具，显式错误配置不静默回退。"""
        configured = self.config.get("tools", name, fallback="").strip()
        if configured:
            path = Path(configured)
            path = path if path.is_absolute() else self.root / path
        elif name == "cpack":
            path = self.tool("cmake").with_name("cpack.exe")
        else:
            found = shutil.which(name + ".exe") if name == "cdb" else shutil.which(name)
            if not found and name == "cmake":
                found = read_cache(self.build_dir / "CMakeCache.txt").get("CMAKE_COMMAND")
            if not found and name == "cdb":
                for base in (os.environ.get("ProgramFiles(x86)", ""),
                             os.environ.get("ProgramFiles", "")):
                    candidate = Path(base) / "Windows Kits/10/Debuggers/x64/cdb.exe"
                    if candidate.is_file():
                        found = str(candidate)
                        break
            if not found:
                raise BuildError(f"找不到 {name}，请配置 envBuild.local.ini [tools]")
            path = Path(found)
        if not path.is_file():
            raise BuildError(f"工具不存在：{path}")
        if name == "cdb" and path.suffix.lower() != ".exe":
            raise BuildError("cdb 请配置原生 cdb.exe 路径，不使用批处理包装器")
        return path.resolve()

    def modes(self, requested: str | None) -> tuple[str, ...]:
        mode = requested or self.config.get("build", "default_mode", fallback="release")
        return ("release", "debug") if mode == "release_debug" else (mode,)

    def info_path(self, mode: str) -> Path:
        return self.build_dir / "envbuild" / mode.title() / "build-info.json"

    def build_info(self, mode: str) -> dict:
        path = self.info_path(mode)
        if not path.is_file():
            raise BuildError(f"缺少构建信息，请先 compile {mode}：{path}")
        result = json.loads(path.read_text(encoding="utf-8"))
        if Path(result["source"]).resolve() != self.root or result["configuration"] != mode.title():
            raise BuildError(f"构建信息不属于当前工程或配置：{path}")
        checked_path(Path(result["executable"]), self.root / "output")
        checked_path(Path(result["deploy_script"]), self.build_dir)
        return result

    def run(self, command: list[str | Path], dry_run: bool = False) -> None:
        """参数列表调用工具，保持中文/空格路径，失败立即停止。"""
        args = [str(value) for value in command]
        print(subprocess.list2cmdline(args), flush=True)
        if dry_run:
            return
        result = subprocess.run(args, cwd=self.root, check=False)
        if result.returncode:
            raise BuildError(f"命令失败（退出码 {result.returncode}）：{args[0]}")


def initialize(root: Path, dry_run: bool = False) -> int:
    """创建缺失配置，保持已有文件原样；不下载、配置或编译。"""
    path = root / "envBuild.local.ini"
    if path.exists():
        print(f"保留已有配置：{path}")
    else:
        env = Environment.read(root)
        env.config.set("dependencies", "prefix_paths", "\n" + "\n".join(env.prefixes))
        print(f"创建本地配置：{path}")
        if not dry_run:
            with path.open("w", encoding="utf-8", newline="\n") as output:
                env.config.write(output)
    return show_info(root)


def show_info(root: Path) -> int:
    """只读报告当前配置；缺失工具单独标识，不自动修复。"""
    env = Environment.read(root)
    print(f"工程：{root}\n预设：{env.preset_name}\n构建目录：{env.build_dir}")
    print("SDK 前缀：" + (";".join(env.prefixes) or "未配置"))
    cache = read_cache(env.build_dir / "CMakeCache.txt")
    print("缓存 SDK：" + cache.get("CMAKE_PREFIX_PATH", "尚未配置"))
    for name in ("cmake", "cpack", "cdb"):
        try:
            print(f"{name}：{env.tool(name)}")
        except BuildError as error:
            print(f"{name}：{error}")
    for mode in ("release", "debug"):
        try:
            data = env.build_info(mode)
            exe = Path(data["executable"])
            print(f"{mode}：{exe}（{'存在' if exe.is_file() else '尚未编译'}）")
            manifest = exe.parent / ".runtime-ReverseModeler.json"
            print(f"  部署清单：{'已生成（启动前校验内容）' if manifest.is_file() else '尚未生成'}")
        except BuildError:
            print(f"{mode}：尚无 envBuild 构建信息")
    return 0
