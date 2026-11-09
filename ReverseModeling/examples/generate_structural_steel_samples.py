"""通过实际几何服务生成槽钢/角钢验证 STEP、PCD 和说明，不覆盖已有文件。

先构建 ReverseServiceTests，再运行：
python generate_structural_steel_samples.py D:/projectWQZ/other-resource/cloud
"""
import argparse
import os
import subprocess
from pathlib import Path

NAMES = ["structural_steel_source.step", "structural_steel_topscan.pcd",
         "structural_steel_channel.step", "structural_steel_channel_surface.pcd",
         "structural_steel_angle.step", "structural_steel_angle_surface.pcd"]

README = """# 槽钢 / 角钢验证样例

尺寸单位 mm，人工构造的测试尺寸，不代表照片实物测量结果。

| 文件 | 用途 |
| --- | --- |
| structural_steel_source.step | 操作输入：矩形源件 + 支撑底板，共 2 个实体 |
| structural_steel_topscan.pcd | 2 mm 网格俯视扫描，仅最高可见面，Z 含 ±0.02 mm 确定性噪声 |
| structural_steel_channel.step | D=10、自动高度的槽钢预期结果 + 原支撑底板 |
| structural_steel_angle.step | D=10、自动高度的角钢预期结果 + 原支撑底板 |
| structural_steel_channel_surface.pcd | 槽钢预期结果的完整表面采样，含内侧和底面，用于几何对照 |
| structural_steel_angle_surface.pcd | 角钢预期结果的完整表面采样，含内侧和底面，用于几何对照 |

源件：X=0…800、Y=0…60、Z=32…172；底板：X=-60…860、Y=-130…190、Z=0…12。
源件与底板故意留 20 mm 间隙，用于验证自动找支撑。源 STEP 不是最终焊接装配。

## STEP 操作

1. 打开 structural_steel_source.step，进入“模型调整 → 设为槽钢”或“设为角钢”。
2. 选长边朝 -Y 的竖直面（imported-part-1 的面 3），D=10，自动高度，预览后应用。
3. 应仍为 2 个实体：原件替换为型钢，底板不变。型钢长 800、宽 60、高 160，Z=12…172。
   槽钢保留上下翼板；角钢保留上翼板。开口朝 -Y，腹板位于 Y=50…60。
4. 改选相反侧面，开口反向；关闭自动高度并设 H=100，型钢 Z=12…112。
5. 可撤销、重做、录制/重放，导出 STEP/CWRM 后重新打开应显示壁厚 10 mm。

命令示例（每条需从原 source.step 开始）：

```text
model.set_channel_steel --part imported-part-1 --face 3 --thickness 10 --auto-height true
model.set_angle_steel --part imported-part-1 --face 3 --thickness 10 --auto-height false --height 100
```

## 点云操作与边界

打开 structural_steel_topscan.pcd，重建类型选“其他模型”，开始重建；进入型钢调整，
选高处长方体的竖直开口面。不要用仅含顶部的点再次拟合板件高度。
俯视扫描看不到下方 20 mm 间隙，也不能确定槽钢还是角钢，
类型与壁厚由用户提供；本例两种最终型钢的俯视可见面相同。

两个 *_surface.pcd 是完整 CAD 表面采样，不冒充俯视线激光数据；可与对应 STEP 对照截面。
所有 PCD 坐标为毫米。生成器调用当前项目的 OCCT 导出/回读服务及点云导出服务。
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--tests", type=Path, default=Path(__file__).resolve().parents[2] /
                        "output/vc143-cpp20/release/ReverseServiceTests.exe")
    args = parser.parse_args()
    if not args.tests.is_file():
        parser.error(f"请先构建 ReverseServiceTests：{args.tests}")
    folder = args.directory.resolve()
    for name in [*NAMES, "structural_steel_README.md"]:
        if (folder / name).exists():
            parser.error(f"输出已存在，不覆盖：{folder / name}")
    environment = os.environ.copy()
    environment["RM_STEEL_SAMPLE_DIR"] = str(folder)
    subprocess.run([str(args.tests.resolve()), "--gtest_filter=StructuralSteel.ExportValidationFixtures"],
                   env=environment, check=True)
    (folder / "structural_steel_README.md").write_text(README, encoding="utf-8")
    for name in NAMES:
        print(f"{folder / name} ({(folder / name).stat().st_size} bytes)")


if __name__ == "__main__":
    main()
