"""生成两类仅有俯视可见点的焊件 PCD（基础版及障碍物版）和尺寸说明，不覆盖已有文件。

用法：python generate_other_weldment_samples.py <输出目录>
加 --slot-only 仅新增照片对应的长圆孔筋板样例，保留已有圆孔对照样例。
这些样例用于检查重建流程，不代表照片实物尺寸或现场扫描精度。
"""

import argparse
import json
import math
import random
import struct
from pathlib import Path


def rounded_distance(x: float, y: float, half_x: float, half_y: float, radius: float) -> float:
    qx, qy = abs(x) - (half_x - radius), abs(y) - (half_y - radius)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - radius


def generate(ring: bool, obstacles: bool = False, *, slot: bool = False) -> list[tuple[float, float, float]]:
    rng = random.Random(79)
    points = []
    yaw, tilt = 0.27, 0.04

    def add(x: float, y: float, height: float) -> None:
        x += rng.gauss(0, 0.025)
        y += rng.gauss(0, 0.025)
        height += rng.gauss(0, 0.025)
        a = math.cos(yaw) * x - math.sin(yaw) * y
        b = math.sin(yaw) * x + math.cos(yaw) * y
        points.append((2300 + a, -1400 + math.cos(tilt) * b - math.sin(tilt) * height,
                       800 + math.sin(tilt) * b + math.cos(tilt) * height))

    for x in range(-350, 351, 2):
        for y in range(-260, 261, 2):
            height = 0.0
            if ring:
                if (rounded_distance(x, y, 210, 170, 40) <= 0
                        and rounded_distance(x, y, 198, 158, 28) >= 0):
                    height = 100
                if 210 <= abs(x) <= 310 and abs(y) <= 6:
                    height = 65
                # 围板内放置一个独立矮障碍物，检查内腔没有被填实。
                if obstacles and abs(x) <= 30 and abs(y) <= 25:
                    height = 25
            else:
                if abs(x) <= 270 and abs(abs(y) - 180) <= 6:
                    height = 100 - max(0, abs(x) - 220) * 0.5
                if abs(abs(x) - 176) <= 6 and abs(y) <= 174:
                    height = 60 - max(0, abs(y) - 125) * 0.4
                # 长圆孔沿两块纵向侧板之间的横向展开：240×60 矩形 + 两端 R120 半圆。
                hole_distance = math.hypot(x, max(abs(y) - 30, 0)) if slot else math.hypot(x, y)
                if hole_distance < 120:
                    continue
            add(x, y, height)
    # 底板范围外的独立物体。
    if obstacles:
        for x in range(420, 481, 2):
            for y in range(-30, 31, 2):
                add(x, y, 45)
    return points


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--slot-only", action="store_true", help="只新增底板筋板长圆孔 PCD 和独立尺寸说明")
    args = parser.parse_args()
    directory = args.directory
    if args.slot_only:
        variants = [(False, False)]
        destinations = [directory / "其他模型_底板筋板_长圆孔.pcd"]
        manifest = directory / "其他模型_底板筋板_长圆孔_尺寸.json"
    else:
        variants = [(ring, obstacles) for obstacles in (False, True) for ring in (False, True)]
        destinations = [directory / ("其他模型_" + ("圆角围板" if ring else "底板筋板")
                        + ("_含障碍物" if obstacles else "") + ".pcd") for ring, obstacles in variants]
        manifest = directory / "其他模型样例尺寸.json"
    for destination in [*destinations, manifest]:
        if destination.exists():
            parser.error(f"输出已存在，不覆盖：{destination}")
    directory.mkdir(parents=True, exist_ok=True)
    descriptions = []
    for (ring, obstacles), destination in zip(variants, destinations, strict=True):
        points = generate(ring, obstacles, slot=args.slot_only)
        header = ("# .PCD v0.7\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
                  f"WIDTH {len(points)}\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS {len(points)}\nDATA binary\n")
        with destination.open("xb") as stream:
            stream.write(header.encode("ascii"))
            for point in points:
                stream.write(struct.pack("<fff", *point))
        description = {
            "file": destination.name, "pointCount": len(points), "units": "mm", "type": "other",
            "baseOutlineMm": [700, 520], "baseThickness": "unobserved",
            "scan": "top visible samples only; 2 mm grid; no side/back surfaces",
            "yawRadians": 0.27, "tiltRadians": 0.04, "originMm": [2300, -1400, 800],
            "noiseStdMm": 0.025, "expectedWorkpieceSolids": 4 if ring else 5,
            "defaultBaseThicknessMm": 1, "expectedBaseSurfaces": 0,
            "expectedObstacles": (2 if ring else 1) if obstacles else 0,
            "referenceImages": [3, 4] if ring else [1, 2],
            "dimensionsAreSynthetic": True,
        }
        if ring:
            description.update({"outerSizeMm": [420, 340], "outerRadiusMm": 40,
                                "wallThicknessMm": 12, "wallHeightMm": 100,
                                "externalRibThicknessMm": 12, "externalRibHeightMm": 65})
        else:
            description.update({"sideLengthMm": 540, "sideCenterYmm": [-180, 180],
                                "ribCenterXmm": [-176, 176], "plateThicknessMm": 12,
                                "maximumPlateHeightsMm": [100, 60],
                                "holeRadiusMm": 120, "holeShape": "slot" if args.slot_only else "circle"})
            if args.slot_only:
                description.update({"holeStraightLengthMm": 60, "holeOverallLengthMm": 300,
                                    "holeWidthMm": 240, "holeRectangleSizeMm": [240, 60],
                                    "holeAxis": "local Y (across the two longitudinal side plates)",
                                    "holeDefinition": "240x60 rectangle + two R120 semicircles; not an ellipse"})
        descriptions.append(description)
    with manifest.open("x", encoding="utf-8") as stream:
        json.dump(descriptions, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    print(manifest)


if __name__ == "__main__":
    main()
