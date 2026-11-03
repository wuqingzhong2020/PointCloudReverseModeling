"""RmCommandGen: safe definitions -> C++ descriptors and Markdown, using Jinja2."""
from __future__ import annotations
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile
import time
from definition_loader import load_manifest, load_definition, DefinitionError, require


@contextmanager
def generation_lock(path):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a+b") as lock:
        if lock.tell() == 0:
            lock.write(b"0")
            lock.flush()
        deadline = time.monotonic() + 60
        while True:
            try:
                lock.seek(0)
                if os.name == "nt":
                    import msvcrt
                    msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
                else:
                    import fcntl
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                break
            except OSError:
                if time.monotonic() >= deadline:
                    raise DefinitionError("another generator holds the source-tree lock")
                time.sleep(0.1)
        try:
            yield
        finally:
            lock.seek(0)
            if os.name == "nt":
                msvcrt.locking(lock.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(lock, fcntl.LOCK_UN)


def safe_child(root, relative):
    relative = Path(relative)
    require(not relative.is_absolute() and ".." not in relative.parts, "unsafe generated path")
    path = root / relative
    require(path.resolve().is_relative_to(root.resolve()), "generated path escapes output")
    for ancestor in [path, *path.parents]:
        if ancestor == root.parent:
            break
        require(not ancestor.is_symlink(), "generated output traverses a symlink")
    return path


def write_changed(path, data, staging):
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    staging.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=staging, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", nargs="?", choices=("check", "generate", "docs"), default="generate",
                        help="check 仅检查声明；generate 生成源码；docs 仅生成 Markdown/帮助元数据")
    parser.add_argument("--manifest", type=Path, help="唯一模块清单 modules.json")
    parser.add_argument("--input", type=Path, help="check 使用的单个 .rmdef 文件")
    parser.add_argument("--output", type=Path, help="生成的 execCommonds 源码目录")
    parser.add_argument("--docs", "--docs-output", dest="docs", type=Path, help="Markdown 和 commands.help.json 输出目录")
    work = parser.add_mutually_exclusive_group()
    work.add_argument("--build-dir", type=Path, help="当前 CMake build 根目录（暂存放在 commandManager/generator 下）")
    work.add_argument("--work-dir", type=Path, help="直接指定生成临时目录，须位于构建树")
    parser.add_argument("--templates", type=Path, default=Path(__file__).resolve().parent.parent / "templates",
                        help="Jinja 模板目录，默认使用脚本旁的 templates")
    parser.add_argument("--module", help="仅刷新一个模块；总注册清单仍保留全部启用模块")
    parser.add_argument("--check", action="store_true", help="检查定义与生成文件一致性，不写源码")
    argv = sys.argv[1:] if argv is None else argv
    if not argv:
        parser.print_help()
        return 0
    args = parser.parse_args(argv)
    try:
        if args.action == "check":
            require(bool(args.input) != bool(args.manifest), "check requires exactly one of --input or --manifest")
            modules = [load_definition(args.input)] if args.input else load_manifest(args.manifest)
            print(f"RmCommandGen: {len(modules)} modules; declarations valid")
            return 0
        require(args.manifest and not args.input, "generation requires --manifest; --input is only for check")
        require(args.action != "generate" or args.output, "generate requires --output")
        require(args.action != "docs" or args.docs, "docs requires --docs-output")
        require(not args.output or args.action != "docs", "docs does not write C++ sources; omit --output")
        modules = load_manifest(args.manifest)
        require(not args.module or args.module in {m["name"] for m in modules}, "unknown module")
        from renderer import render_all
        from jinja2 import TemplateError
        try:
            files, docs = render_all(modules, args.templates.resolve(), sources=args.action != "docs")
        except TemplateError as error:
            raise DefinitionError(f"template: {error}") from error
        output = args.output.resolve() if args.output else None
        docroot = args.docs.resolve() if args.docs else None
        # Shared for every CMake configuration/build directory targeting this source tree.
        project_root = Path(__file__).resolve().parents[2]
        lock = project_root / "build/commandManager/execCommonds.lock"
        with generation_lock(lock):
            staging = (args.work_dir.resolve() if args.work_dir else
                       (args.build_dir or project_root / "build").resolve() / "commandManager/generator")
            require(not output or not staging.is_relative_to(output), "temporary directory must be outside source output")
            require(not docroot or not staging.is_relative_to(docroot), "temporary directory must be outside docs output")
            pending_writes, pending_deletes = [], []
            plans = [(root, content) for root, content in [(output, files), (docroot, docs)] if root]
            for root, contents in plans:
                manifest_path = safe_child(root, ".rm-generated.json")
                old = json.loads(manifest_path.read_text("utf-8")) if manifest_path.exists() else {}
                new = dict(old) if args.module else {}
                for name, text in contents.items():
                    if args.module and "/" in name and name.split("/")[0] != args.module:
                        continue
                    if args.module and root == docroot and name not in (args.module + ".md", "commands.help.json"):
                        continue
                    path = safe_child(root, name)
                    data = text.replace("\r\n", "\n").encode("utf-8-sig" if path.suffix in (".h", ".cpp") else "utf-8")
                    if args.check:
                        require(path.exists() and path.read_bytes() == data, f"generated file is stale: {path}")
                    elif path.exists() and name not in old:
                        require(path.read_bytes() == data, f"refusing to overwrite unmanaged file: {path}")
                    if not args.check:
                        pending_writes.append((path, data))
                    new[name] = hashlib.sha256(data).hexdigest()
                for name, digest in old.items():
                    selected = not args.module or name.startswith(args.module + "/") or name == args.module + ".md"
                    if name not in contents and selected:
                        path = safe_child(root, name)
                        require(not args.check, f"obsolete generated file: {path}")
                        if path.exists():
                            require(hashlib.sha256(path.read_bytes()).hexdigest() == digest, f"obsolete file has local edits: {path}")
                            pending_deletes.append(path)
                        new.pop(name, None)
                if not args.check:
                    pending_writes.append((manifest_path, (json.dumps(new, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode()))
            if not args.check and output:
                cmake = "# Generated from the complete module manifest.\n"
                for name in files:
                    variable = "RM_GENERATED_SOURCES" if name.endswith(".cpp") else "RM_GENERATED_HEADERS"
                    cmake += f'list(APPEND {variable} "${{RM_COMMAND_OUTPUT}}/{name}")\n'
                if docroot:
                    for name in docs:
                        cmake += f'list(APPEND RM_GENERATED_DOCS "${{RM_COMMAND_DOCS}}/{name}")\n'
                pending_writes.append((staging / "sources.cmake", cmake.encode()))
            if not args.check:
                # Finish validation before publishing anything. Staging stays in the build tree.
                for path, data in pending_writes:
                    write_changed(path, data, staging / "staging")
                for path in pending_deletes:
                    path.unlink()
        print(f"RmCommandGen: {len(modules)} modules; {'checked' if args.check else 'generated'}")
        return 0
    except (DefinitionError, OSError, ValueError, ImportError) as error:
        print(f"RmCommandGen: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
