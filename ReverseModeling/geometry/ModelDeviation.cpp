/* 文件描述：计算原始点云到参数化有限矩形板件的偏差统计，不适用于已改变轮廓的模型。 */

#include <algorithm>
#include <cmath>
#include <limits>

#include "geometry/ModelState.h"

namespace reverse
{
using detail::model::checkpoint;
using detail::model::Plate;

namespace
{
double boxSurfaceDistance(Vec3 point, const Plate &plate)
{
    const double dx = std::max({plate.minimum.x - point.x, 0.0, point.x - plate.maximum.x});
    const double dy = std::max({plate.minimum.y - point.y, 0.0, point.y - plate.maximum.y});
    const double dz = std::max({plate.minimum.z - point.z, 0.0, point.z - plate.maximum.z});
    if (dx > 0 || dy > 0 || dz > 0)
        return std::hypot(dx, dy, dz);
    return std::min({point.x - plate.minimum.x, plate.maximum.x - point.x, point.y - plate.minimum.y,
                     plate.maximum.y - point.y, point.z - plate.minimum.z, plate.maximum.z - point.z});
}
} // namespace

DeviationResult evaluateDeviation(const CloudData &cloud, const Model &model, std::stop_token stop,
                                  const ProgressCallback &progress)
{
    if (std::any_of(model.summary().plates.begin(), model.summary().plates.end(), [](const auto &plate)
                    { return !plate.outline.empty() || !plate.holes.empty() || !plate.uprightFootprint.empty(); }))
        throw GeometryError("unsupported_deviation",
                            "含识别孔或直线/圆弧轮廓的模型不适用矩形板件偏差算法；不能用包围盒代替真实轮廓计算。");
    if (!model.summary().mirrorCompletedParts.empty() || !model.summary().mirroredParts.empty())
        throw GeometryError("unsupported_deviation", "镜像补全后不再适用原矩形板件偏差算法。");
    if (!model.summary().contourExpandedParts.empty() || !model.summary().locallyReconstructedParts.empty())
    {
        throw GeometryError("unsupported_deviation", "外扩轮廓后的模型暂不适用原矩形板件偏差算法。");
    }
    if (!model.summary().thicknessOverrides.empty())
        throw GeometryError("unsupported_deviation", "修改板厚后的模型暂不适用矩形板件偏差算法。");
    if (!model.summary().edgeTreatedParts.empty() || !model.summary().holeEditedParts.empty())
        throw GeometryError("unsupported_deviation", "圆角/倒角后的模型不适用矩形板件偏差算法。");
    if (!model.summary().importedFormat.empty())
        throw GeometryError("unsupported_deviation", "导入模型不适用矩形板件偏差算法");
    if (cloud.points.empty())
        throw GeometryError("empty_cloud", "没有可比较的有效点");
    const auto &source = *model.implementation_;
    std::vector<BeamFrame> frames;
    for (const auto &plate : source.plates)
        frames.push_back(modelPartFrame(source.summary, plate.id));
    DeviationResult result;
    result.pointCount = cloud.points.size();
    result.distancesMm.reserve(cloud.points.size());
    result.nearestPartIndices.reserve(cloud.points.size());
    long double sum = 0, squaredSum = 0;
    for (std::size_t index = 0; index < cloud.points.size(); ++index)
    {
        if (index % 4096 == 0)
            checkpoint(stop, progress, 0.95 * index / cloud.points.size(), "计算原始有效点到有限板件表面的偏差");
        const auto &point = cloud.points[index].position;
        const Vec3 world{point.x + cloud.originMm.x, point.y + cloud.originMm.y, point.z + cloud.originMm.z};
        double distance = std::numeric_limits<double>::infinity();
        std::uint32_t nearest{};
        for (std::size_t part = 0; part < source.plates.size(); ++part)
        {
            const double candidate =
                boxSurfaceDistance(inverseTransformPoint(frames[part], world), source.plates[part]);
            if (candidate < distance)
            {
                distance = candidate;
                nearest = static_cast<std::uint32_t>(part);
            }
        }
        if (!std::isfinite(distance))
            throw GeometryError("deviation_failed", "偏差计算出现无效数值");
        result.distancesMm.push_back(distance);
        result.nearestPartIndices.push_back(nearest);
        sum += distance;
        squaredSum += static_cast<long double>(distance) * distance;
        result.maximumMm = std::max(result.maximumMm, distance);
    }
    result.meanMm = static_cast<double>(sum / cloud.points.size());
    result.rmsMm = std::sqrt(static_cast<double>(squaredSum / cloud.points.size()));
    auto sorted = result.distancesMm;
    const auto quantile = static_cast<std::size_t>(std::ceil(0.95 * sorted.size())) - 1;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(quantile), sorted.end());
    result.p95Mm = sorted[quantile];
    checkpoint(stop, progress, 1.0, "有限板件表面偏差计算完成");
    return result;
}

} // namespace reverse
