"""启动已部署的主程序；不使用 SDK PATH 掩盖缺失的运行文件。"""

from pathlib import Path
import subprocess

from .deploy import verify_runtime
from .environment import BuildError, Environment


def launch(env: Environment, mode: str, dry_run: bool = False) -> int:
    """直接运行 Release 或开启可交互 CDB；成功返回仅表示进程创建。"""
    data = env.build_info(mode)
    executable = Path(data["executable"])
    if not executable.is_file():
        raise BuildError(f"程序不存在，请先 compile {mode}：{executable}")
    command = [str(executable)]
    if mode == "debug":
        command = [str(env.tool("cdb")), "-g", "-G", "-lines", "-y", str(executable.parent),
                   "-srcpath", str(env.root), str(executable)]
        if not executable.with_suffix(".pdb").is_file():
            print("提示：缺少主程序 PDB，源码调试信息可能受限。")
    print(subprocess.list2cmdline(command))
    if dry_run:
        return 0
    verify_runtime(executable)
    process = subprocess.Popen(command, cwd=executable.parent, close_fds=True,
                               stdin=subprocess.DEVNULL if mode == "release" else None,
                               stdout=subprocess.DEVNULL if mode == "release" else None,
                               stderr=subprocess.DEVNULL if mode == "release" else None,
                               creationflags=subprocess.CREATE_NEW_CONSOLE if mode == "debug" else 0)
    print(f"启动请求已提交，PID：{process.pid}")
    return 0
