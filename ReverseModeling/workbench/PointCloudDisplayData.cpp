/*
 * 文件描述：
 * 对点云作确定性显示采样，按截面短轴高度生成紫色、蓝紫到青色的逐点颜色。
 * 色域采用分位数范围，退化高度使用中间色；原始点坐标及业务数据保持不变。
 */
#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "workbench/PointCloudDisplayData.h"

namespace reverse::workbench
{
namespace
{
std::array<double, 3> heightColor(double value)
{
    constexpr std::array<unsigned int, 4> colors{0x49165f, 0x642382, 0x415a91, 0x2e909b};
    constexpr std::array<double, 4> stops{0.0, 0.5, 0.75, 1.0};
    const double position = std::clamp(value, 0.0, 1.0);
    std::size_t segment = 0;
    while (segment + 2 < stops.size() && position > stops[segment + 1])
    {
        ++segment;
    }
    const double fraction = (position - stops[segment]) / (stops[segment + 1] - stops[segment]);
    std::array<double, 3> color;
    for (std::size_t channel = 0; channel < color.size(); ++channel)
    {
        const auto shift = static_cast<unsigned int>((2 - channel) * 8);
        const double low = (colors[segment] >> shift) & 255;
        const double high = (colors[segment + 1] >> shift) & 255;
        color[channel] = (low + fraction * (high - low)) / 255.0;
    }
    return color;
}
}

std::vector<CloudDisplayPoint> makeCloudDisplayPoints(
    const CloudData &cloud, Vec3 heightAxis, std::size_t maximumPoints)
{
    const double length = std::hypot(heightAxis.x, heightAxis.y, heightAxis.z);
    if (maximumPoints == 0 || !std::isfinite(length) || length < 1e-12)
    {
        throw std::invalid_argument("点云显示采样上限或高度方向无效。");
    }
    // 高度正向尽量朝向项目 +Z，旋转视图不会改变点的颜色。
    const double scale = (heightAxis.z < 0 ? -1.0 : 1.0) / length;
    heightAxis = {heightAxis.x * scale, heightAxis.y * scale, heightAxis.z * scale};
    const auto stride = std::max<std::size_t>(1, cloud.points.size() / maximumPoints
        + (cloud.points.size() % maximumPoints != 0 ? 1 : 0));
    std::vector<CloudDisplayPoint> points;
    std::vector<double> heights;
    points.reserve(std::min(cloud.points.size(), maximumPoints));
    heights.reserve(points.capacity());
    for (std::size_t index = 0; index < cloud.points.size(); index += stride)
    {
        const auto &point = cloud.points[index].position;
        const double height = point.x * heightAxis.x + point.y * heightAxis.y + point.z * heightAxis.z;
        if (!std::isfinite(height))
        {
            throw std::invalid_argument("点云显示包含非有限坐标。");
        }
        points.push_back({point, {}, index});
        heights.push_back(height);
    }
    if (points.empty())
    {
        return points;
    }
    auto sortedHeights = heights;
    const auto lowIndex = (heights.size() - 1) * 2 / 100;
    const auto highIndex = (heights.size() - 1) * 98 / 100;
    std::nth_element(sortedHeights.begin(), sortedHeights.begin() + lowIndex, sortedHeights.end());
    const double minimum = sortedHeights[lowIndex];
    std::nth_element(sortedHeights.begin(), sortedHeights.begin() + highIndex, sortedHeights.end());
    const double range = sortedHeights[highIndex] - minimum;
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        points[index].colorSrgb = heightColor(range > 1e-9 ? (heights[index] - minimum) / range : 0.5);
    }
    return points;
}
}
