"""调用 CPack 生成独立发布包，Python 仅编排并归档成功产物。"""

from argparse import Namespace
from datetime import UTC, datetime
from pathlib import Path
import uuid

from .build import compile_project
from .deploy import digest
from .environment import BuildError, Environment, checked_path


def package_project(env: Environment, options: Namespace) -> None:
    """打包已有配置；packageAll 先编译主程序，任一阶段失败即终止。"""
    if options.command == "packageAll":
        options.target = "ReverseModeler"
        compile_project(env, options)
    config = env.build_dir / "CPackConfig.cmake"
    if not config.is_file() and not options.dry_run:
        raise BuildError("缺少 CPackConfig.cmake，请先 compile")
    for mode in env.modes(options.mode):
        if options.dry_run and not env.info_path(mode).is_file():
            print(f"预览：构建后通过 CPack 收集 {mode} 产物及依赖到独立暂存目录。")
            continue
        data = env.build_info(mode)
        if not Path(data["executable"]).is_file() and not options.dry_run:
            raise BuildError(f"主程序未编译，请先 compile {mode}")
        stamp = datetime.now(UTC).strftime("%Y%m%d-%H%M%SZ") + "-" + uuid.uuid4().hex[:6]
        commit = data["commit"] + ("-dirty" if data["dirty"] else "")
        name = f"ReverseModeler-{data['version']}-win-x64-{data['tag']}-{mode}-{commit}-{stamp}"
        work = checked_path(env.root / "output/package-staging" / data["tag"] / mode / stamp,
                            env.root / "output")
        env.run([env.tool("cpack"), "--config", config, "-C", mode.title(), "-G", "ZIP",
                 "-B", work, "-D", "CPACK_PACKAGE_FILE_NAME=ReverseModeler",
                 "-D", "RM_PACKAGE_NAMED=TRUE"], options.dry_run)
        if options.dry_run:
            continue
        # CPack 内部用短名称，避免 Windows Qt 部署工具遇到超长暂存路径。
        # 对外名称仍包含配置、提交及时间；ZIP 内顶层目录统一为 ReverseModeler。
        archive = work / "ReverseModeler.zip"
        if not archive.is_file():
            raise BuildError(f"CPack 未生成预期文件：{archive}")
        destination = env.root / "output/packages"
        checked_path(destination, env.root / "output")
        destination.mkdir(parents=True, exist_ok=True)
        checksum = archive.with_suffix(".zip.sha256")
        values = checksum.read_text(encoding="utf-8").split() if checksum.is_file() else []
        if not values or values[0] != digest(archive):
            raise BuildError("CPack 发布包 SHA256 校验失败，不归档")
        target = destination / f"{name}.zip"
        target_checksum = target.with_suffix(".zip.sha256")
        if target.exists() or target_checksum.exists():
            raise BuildError(f"拒绝覆盖已有发布包：{target}")
        checksum.write_text(f"{digest(archive)}  {target.name}\n", encoding="utf-8")
        archive.rename(target)
        checksum.rename(target_checksum)
        print(f"发布产物：{target}\n校验文件：{target_checksum}")
