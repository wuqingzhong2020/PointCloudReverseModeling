/*
 * 文件描述：
 * 定义点云显示采样与紫青高度配色数据，不修改原始点云或建模输入。
 */
#pragma once

#include <array>
#include <cstddef>
#include <vector>

#include "core/Types.h"

namespace reverse::workbench
{
struct CloudDisplayPoint
{
    Vec3 position; // 沿用 CloudData 的局部毫米坐标。
    std::array<double, 3> colorSrgb;
    std::size_t pointIndex{}; // 当前完整点云索引，用于映射选区；不等同于原文件 sourceIndex。
};

// 沿点云截面短轴分层，使用显示采样的 2% / 98% 分位数压制离群点对色域的影响。
[[nodiscard]] std::vector<CloudDisplayPoint> makeCloudDisplayPoints(
    const CloudData &cloud, Vec3 heightAxis, std::size_t maximumPoints = 200000);
}
