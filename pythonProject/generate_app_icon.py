"""从自有 SVG 生成应用 PNG 和多尺寸 ICO；仅维护图标时需要 Inkscape，不参与正常构建。"""

import argparse
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile


ICON_SIZES = (16, 20, 24, 32, 40, 48, 64, 128, 256)
ASSET_DIRECTORY = (
    Path(__file__).resolve().parents[1] / "ReverseModeling/workbench/resources/app"
)


def generate_icon(inkscape: str) -> None:
    """渲染各尺寸并打包透明 PNG 图层为 ICO，替换资源目录内的生成文件。"""
    source = ASSET_DIRECTORY / "ReverseModeler.svg"
    with tempfile.TemporaryDirectory(prefix="reversemodeler-icon-") as temporary:
        images: list[tuple[int, bytes]] = []
        for size in ICON_SIZES:
            output = Path(temporary) / f"icon-{size}.png"
            subprocess.run(
                [inkscape, str(source), "--export-type=png", "--export-area-page",
                 f"--export-width={size}", "--export-background-opacity=0",
                 f"--export-filename={output}"],
                check=True, capture_output=True,
            )
            data = output.read_bytes()
            if data[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", data[16:24]) != (size, size):
                raise ValueError(f"Inkscape 未输出所需尺寸的 PNG：{size}×{size}")
            images.append((size, data))

        offset = 6 + 16 * len(images)
        directory = bytearray(struct.pack("<HHH", 0, 1, len(images)))
        for size, data in images:
            directory.extend(struct.pack(
                "<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(data), offset
            ))
            offset += len(data)
        (ASSET_DIRECTORY / "ReverseModeler.ico").write_bytes(
            directory + b"".join(data for _, data in images)
        )
        (ASSET_DIRECTORY / "ReverseModeler.png").write_bytes(images[-1][1])
    print(f"已生成应用图标：{ASSET_DIRECTORY}（{len(ICON_SIZES)} 个尺寸）")


def main() -> None:
    """解析维护命令，使用本机已有 Inkscape，不下载依赖。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inkscape", default=shutil.which("inkscape"),
                        help="Inkscape 命令完整路径；默认从 PATH 查找")
    arguments = parser.parse_args()
    if not arguments.inkscape:
        parser.error("未找到 Inkscape，请用 --inkscape 指定 inkscape.com 或 inkscape 可执行文件")
    generate_icon(arguments.inkscape)


if __name__ == "__main__":
    main()
