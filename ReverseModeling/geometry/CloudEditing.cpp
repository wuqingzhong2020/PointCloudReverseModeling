/*
 * 文件描述：投影完整点云并合并选择索引，筛选点时重新计算包围盒并保留原始属性。
 */
#include <algorithm>
#include <cmath>

#include "geometry/CloudEditing.h"

namespace reverse
{
namespace
{
void checkpoint(std::stop_token stop, const ProgressCallback &progress, double value)
{
    if (stop.stop_requested())
    {
        throw GeometryError("cancelled", "点云编辑已取消。");
    }
    if (progress)
    {
        progress(value, "正在处理完整点云…");
    }
}

void validateSelection(std::span<const std::size_t> selection, std::size_t size)
{
    if ((!selection.empty() && selection.back() >= size) ||
        std::adjacent_find(selection.begin(), selection.end(), std::greater_equal<>()) != selection.end())
    {
        throw GeometryError("invalid_selection", "选区索引无效，请重新框选。");
    }
}

bool contains(const CloudSelectionRect &rectangle, const Vec3 &point)
{
    const auto &m = rectangle.viewProjection;
    const double x = m[0] * point.x + m[1] * point.y + m[2] * point.z + m[3];
    const double y = m[4] * point.x + m[5] * point.y + m[6] * point.z + m[7];
    const double z = m[8] * point.x + m[9] * point.y + m[10] * point.z + m[11];
    const double w = m[12] * point.x + m[13] * point.y + m[14] * point.z + m[15];
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) && std::isfinite(w) && w > 1e-12 &&
           x >= rectangle.minX * w && x <= rectangle.maxX * w && y >= rectangle.minY * w && y <= rectangle.maxY * w &&
           z >= (rectangle.zeroToOneDepth ? 0 : -w) && z <= w;
}
} // namespace

CloudSelection selectCloudPoints(const CloudData &cloud, const CloudSelectionRect &rectangle, CloudSelectionMode mode,
                                 std::span<const std::size_t> previous, std::stop_token stop,
                                 const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0);
    if (mode == CloudSelectionMode::Clear)
    {
        return {};
    }
    if (mode != CloudSelectionMode::Replace && mode != CloudSelectionMode::Add && mode != CloudSelectionMode::Remove)
    {
        throw GeometryError("invalid_selection", "未知框选方式。");
    }
    validateSelection(previous, cloud.points.size());
    const std::array<double, 4> bounds{rectangle.minX, rectangle.minY, rectangle.maxX, rectangle.maxY};
    if (!std::all_of(rectangle.viewProjection.begin(), rectangle.viewProjection.end(),
                     [](double v) { return std::isfinite(v); }) ||
        !std::all_of(bounds.begin(), bounds.end(), [](double v) { return std::isfinite(v) && v >= -1 && v <= 1; }) ||
        rectangle.minX >= rectangle.maxX || rectangle.minY >= rectangle.maxY)
    {
        throw GeometryError("invalid_selection", "框选相机或矩形无效。");
    }
    CloudSelection result;
    std::size_t old = 0;
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 16384 == 0)
        {
            checkpoint(stop, progress, static_cast<double>(i) / cloud.points.size());
        }
        const bool wasSelected = old < previous.size() && previous[old] == i;
        old += wasSelected;
        const bool inside = contains(rectangle, cloud.points[i].position);
        if ((mode == CloudSelectionMode::Replace && inside) ||
            (mode == CloudSelectionMode::Add && (inside || wasSelected)) ||
            (mode == CloudSelectionMode::Remove && wasSelected && !inside))
        {
            result.push_back(i);
        }
    }
    checkpoint(stop, progress, 1);
    return result;
}

std::shared_ptr<const CloudData> editCloudPoints(const CloudData &cloud, std::span<const std::size_t> selection,
                                                 CloudEditMode mode, std::stop_token stop,
                                                 const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0);
    validateSelection(selection, cloud.points.size());
    if (selection.empty())
    {
        throw GeometryError("empty_selection", "请先框选需要处理的点云。");
    }
    if (mode != CloudEditMode::KeepSelection && mode != CloudEditMode::DeleteSelection)
    {
        throw GeometryError("invalid_edit", "未知点云编辑方式。");
    }
    auto result = std::make_shared<CloudData>();
    result->fields = cloud.fields;
    result->sourcePointCount = cloud.sourcePointCount;
    result->invalidPointCount = cloud.invalidPointCount;
    result->hasIntensity = cloud.hasIntensity;
    result->sourceUnit = cloud.sourceUnit;
    result->sourceScaleToMm = cloud.sourceScaleToMm;
    result->originMm = cloud.originMm;
    result->points.reserve(mode == CloudEditMode::KeepSelection ? selection.size()
                                                                : cloud.points.size() - selection.size());
    std::size_t selected = 0;
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 16384 == 0)
        {
            checkpoint(stop, progress, static_cast<double>(i) / cloud.points.size());
        }
        const bool inside = selected < selection.size() && selection[selected] == i;
        selected += inside;
        if (inside != (mode == CloudEditMode::KeepSelection))
        {
            continue;
        }
        const auto &point = cloud.points[i];
        if (result->points.empty())
        {
            result->boundsMinMm = result->boundsMaxMm = point.position;
        }
        else
        {
            auto &low = result->boundsMinMm;
            auto &high = result->boundsMaxMm;
            low = {std::min(low.x, point.position.x), std::min(low.y, point.position.y),
                   std::min(low.z, point.position.z)};
            high = {std::max(high.x, point.position.x), std::max(high.y, point.position.y),
                    std::max(high.z, point.position.z)};
        }
        result->points.push_back(point);
    }
    checkpoint(stop, progress, 1);
    return result;
}
} // namespace reverse
