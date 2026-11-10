"""Validate generated-file ownership and plan publication without modifying output files."""

from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from definition_loader import require


@dataclass(frozen=True)
class OutputPlan:
    writes: tuple[tuple[Path, bytes], ...]
    deletes: tuple[Path, ...]


def safe_child(root: Path, relative: str) -> Path:
    """Resolve an owned output path, rejecting traversal and symlinks."""
    relative_path = Path(relative)
    require(not relative_path.is_absolute() and ".." not in relative_path.parts,
            "unsafe generated path")
    path = root / relative_path
    require(path.resolve().is_relative_to(root.resolve()), "generated path escapes output")
    for ancestor in [path, *path.parents]:
        if ancestor == root.parent:
            break
        require(not ancestor.is_symlink(), "generated output traverses a symlink")
    return path


def _load_ownership(path: Path) -> dict[str, str]:
    if not path.exists():
        return {}
    records = json.loads(path.read_text(encoding="utf-8"))
    require(isinstance(records, dict), f"invalid generated-file manifest: {path}")
    for name, digest in records.items():
        require(isinstance(name, str) and bool(name) and isinstance(digest, str)
                and len(digest) == 64 and all(c in "0123456789abcdef" for c in digest),
                f"invalid generated-file ownership record: {path}")
        safe_child(path.parent, name)
    return records


def _selected_output(name: str, module: str | None, is_docs: bool) -> bool:
    if not module:
        return True
    if "/" in name and name.split("/")[0] != module:
        return False
    return not is_docs or name in (module + ".md", "commands.help.json")


def plan_output(root: Path, contents: dict[str, str], *, module: str | None = None,
                is_docs: bool = False, check: bool = False) -> OutputPlan:
    """Read and validate one output tree; return writes/deletions without publishing them.

    Callers must hold the generation lock until all plans have been published. A check
    validates the same ownership and content rules and returns an empty plan.
    """
    manifest_path = safe_child(root, ".rm-generated.json")
    previous = _load_ownership(manifest_path)
    updated = dict(previous) if module else {}
    writes: list[tuple[Path, bytes]] = []
    deletes: list[Path] = []
    for name, text in contents.items():
        if not _selected_output(name, module, is_docs):
            continue
        path = safe_child(root, name)
        encoding = "utf-8-sig" if path.suffix in (".h", ".cpp") else "utf-8"
        data = text.replace("\r\n", "\n").encode(encoding)
        if check:
            require(path.exists() and path.read_bytes() == data,
                    f"generated file is stale: {path}")
        elif path.exists() and name not in previous:
            require(path.read_bytes() == data, f"refusing to overwrite unmanaged file: {path}")
        if not check:
            writes.append((path, data))
        updated[name] = hashlib.sha256(data).hexdigest()
    for name, digest in previous.items():
        selected = not module or name.startswith(module + "/") or name == module + ".md"
        if name in contents or not selected:
            continue
        path = safe_child(root, name)
        require(not check, f"obsolete generated file: {path}")
        if path.exists():
            require(hashlib.sha256(path.read_bytes()).hexdigest() == digest,
                    f"obsolete file has local edits: {path}")
            deletes.append(path)
        updated.pop(name, None)
    if not check:
        manifest = json.dumps(updated, ensure_ascii=False, indent=2, sort_keys=True) + "\n"
        writes.append((manifest_path, manifest.encode("utf-8")))
    return OutputPlan(tuple(writes), tuple(deletes))


def source_list(sources: dict[str, str], docs: dict[str, str]) -> bytes:
    """Render the complete CMake output inventory, including unselected modules."""
    lines = ["# Generated from the complete module manifest.\n"]
    for name in sources:
        variable = "RM_GENERATED_SOURCES" if name.endswith(".cpp") else "RM_GENERATED_HEADERS"
        lines.append(f'list(APPEND {variable} "${{RM_COMMAND_OUTPUT}}/{name}")\n')
    for name in docs:
        lines.append(f'list(APPEND RM_GENERATED_DOCS "${{RM_COMMAND_DOCS}}/{name}")\n')
    return "".join(lines).encode("utf-8")
