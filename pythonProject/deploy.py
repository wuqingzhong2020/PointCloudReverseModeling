"""手动补部署及清单验证；实际收集/复制由 CMake 完成。"""

import hashlib
import json
from pathlib import Path

from .environment import BuildError, Environment, checked_path


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def prepare(env: Environment, mode: str, dry_run: bool = False) -> None:
    """补部署已有主程序，不配置或编译 CMake。"""
    if dry_run and not env.info_path(mode).is_file():
        print(f"预览：{mode} 构建后解析 CMake 部署脚本及 DLL 清单，不执行构建。")
        return
    data = env.build_info(mode)
    if not Path(data["executable"]).is_file():
        raise BuildError(f"主程序不存在，请先 compile {mode}")
    env.run([env.tool("cmake"), "-DRM_FORCE=TRUE", "-P", data["deploy_script"]], dry_run)


def verify_runtime(executable: Path) -> None:
    """确认启动所需的已登记文件存在且未被修改。"""
    manifest = executable.parent / f".runtime-{executable.stem}.json"
    if not manifest.is_file():
        raise BuildError("缺少运行部署清单，请先执行对应配置的 prepareRun")
    data = json.loads(manifest.read_text(encoding="utf-8"))
    if digest(executable) != data["executable_sha256"]:
        raise BuildError("程序已更新，请执行对应配置的 prepareRun")
    for item in data["files"]:
        path = checked_path(executable.parent / item["path"], executable.parent)
        if not path.is_file() or digest(path) != item["sha256"]:
            raise BuildError(f"运行文件缺失或变化：{path}；请执行对应配置的 prepareRun")


def clean_runtime(env: Environment, mode: str, dry_run: bool = False) -> None:
    """仅删除清单登记且内容未改变的部署文件，保留未知/修改文件。"""
    directory = Path(env.build_info(mode)["executable"]).parent
    manifests = list(directory.glob(".runtime-*.json"))
    if not manifests:
        print(f"无部署清单，保留未知文件：{directory}")
        return
    targets: set[Path] = set()
    for manifest in manifests:
        checked_path(manifest, env.root / "output")
        data = json.loads(manifest.read_text(encoding="utf-8"))
        for item in data["files"]:
            path = checked_path(directory / item["path"], directory)
            if path.is_file():
                if digest(path) == item["sha256"]:
                    targets.add(path)
                else:
                    print(f"保留已修改文件：{path}")
        targets.add(manifest)
    for path in sorted(targets):
        print(f"删除部署文件：{path}")
        if not dry_run:
            path.unlink(missing_ok=True)
