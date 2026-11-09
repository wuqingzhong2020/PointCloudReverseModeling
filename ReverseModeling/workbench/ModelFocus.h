/* 文件描述：选中部件决定可见观察方向，整体显示网格决定居中和缩放，不改变模型或选区。 */
#pragma once

#include <optional>
#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
struct ModelFocusView
{
    Vec3 center;
    Vec3 towardEye;
    Vec3 up;
    double scale{}; // 正交视图高度，宽度为 scale * aspect。
};
std::optional<ModelFocusView> modelFocusView(const std::vector<ModelDisplayPart> &parts,
    const std::string &partId, Vec3 currentTowardEye, Vec3 currentUp, double aspect);
}
