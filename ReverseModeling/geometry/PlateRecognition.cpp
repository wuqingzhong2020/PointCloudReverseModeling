/*
 * 文件描述：
 * 从单侧正交板件点云识别底部观测面、两条纵向顶边及实际横向肋板站位。
 * 沿条带分段估计横向宽度作为板厚，并保存支持点数及截面离散度。
 * 底板按两侧板厚最大值向观测面背侧加厚，夹在翼板内侧；翼板下沿对齐底板底面。
 * 保留各翼板独立观测端部，供建模阶段有界收齐底板突出；侧板证据不足时仅保留底部观测面。
 * 当前限定于可见连续顶边的近似矩形板件，不由高度包围盒或经验常数补出板厚。
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include <Eigen/Dense>

#include "geometry/Geometry.h"

namespace reverse
{
namespace
{
using Point = Eigen::Vector3d;

Point eigen(const Vec3 &value)
{
    return {value.x, value.y, value.z};
}
Vec3 vector(const Point &value)
{
    return {value.x(), value.y(), value.z()};
}

void checkpoint(std::stop_token stop, const ProgressCallback &progress, double value, std::string_view message)
{
    if (stop.stop_requested())
    {
        throw GeometryError("cancelled", "点云板件识别已取消。");
    }
    if (progress)
    {
        progress(value, message);
    }
}

double quantile(std::vector<double> values, double fraction)
{
    if (values.empty())
    {
        throw GeometryError("insufficient_evidence", "板件识别缺少有效支持点。");
    }
    const auto index = static_cast<std::size_t>(fraction * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + index, values.end());
    return values[index];
}

double coordinate(const std::vector<Point> &points, int axis, double fraction)
{
    std::vector<double> values;
    values.reserve(points.size());
    for (const auto &point : points)
    {
        values.push_back(point[axis]);
    }
    return quantile(std::move(values), fraction);
}

std::vector<Point> project(const CloudData &cloud, const BeamFrame &frame, std::stop_token stop)
{
    const Point origin = eigen(cloud.originMm) - eigen(frame.originMm);
    Eigen::Matrix3d axes;
    axes << eigen(frame.xAxis), eigen(frame.yAxis), eigen(frame.zAxis);
    std::vector<Point> points;
    points.reserve(cloud.points.size());
    for (std::size_t index = 0; index < cloud.points.size(); ++index)
    {
        if (index % 16384 == 0)
        {
            checkpoint(stop, {}, 0, {});
        }
        points.push_back(axes.transpose() * (eigen(cloud.points[index].position) + origin));
    }
    return points;
}

// 主体大平面的法线精修，避免 PCA 的轻微倾斜把长板高度误差放大。
BeamFrame refineFrame(const CloudData &cloud, const BeamEstimate &estimate, std::stop_token stop)
{
    BeamFrame frame = estimate.frame;
    frame.yAxis = estimate.frame.zAxis;
    frame.zAxis = vector(eigen(frame.xAxis).cross(eigen(frame.yAxis)));
    if (frame.yAxis.z < 0)
    {
        frame.yAxis = vector(-eigen(frame.yAxis));
        frame.zAxis = vector(-eigen(frame.zAxis));
    }
    const auto points = project(cloud, frame, stop);
    const double median = coordinate(points, 1, 0.5);
    const double range = coordinate(points, 1, 0.99) - coordinate(points, 1, 0.01);
    const double band = std::max(1.0, range * 0.08);
    Eigen::Matrix3d matrix = Eigen::Matrix3d::Zero();
    Point right = Point::Zero();
    std::size_t count = 0;
    for (const auto &point : points)
    {
        if (std::abs(point.y() - median) > band)
        {
            continue;
        }
        const Point row{point.x(), point.z(), 1};
        matrix += row * row.transpose();
        right += row * point.y();
        ++count;
    }
    if (count < 100 || !matrix.fullPivLu().isInvertible())
    {
        throw GeometryError("insufficient_evidence", "点云不足以确定底板观测面。");
    }
    const Point fit = matrix.ldlt().solve(right);
    const Point normal =
        (eigen(frame.yAxis) - fit.x() * eigen(frame.xAxis) - fit.y() * eigen(frame.zAxis)).normalized();
    const Point longitudinal = (eigen(frame.xAxis) - normal.dot(eigen(frame.xAxis)) * normal).normalized();
    frame.xAxis = vector(longitudinal);
    frame.yAxis = vector(normal);
    frame.zAxis = vector(longitudinal.cross(normal));
    return frame;
}

struct Strip
{
    double center{};
    double thickness{};
    double spread{};
    double height{};
    std::uint64_t support{};
};

std::optional<Strip> measureStrip(const std::vector<Point> &points, int along, int across, double start, double end,
                                  int sections)
{
    if (points.size() < 100 || end - start <= 0.01)
    {
        return {};
    }
    std::vector<std::vector<double>> slices(sections);
    for (const auto &point : points)
    {
        const int section =
            std::clamp(static_cast<int>((point[along] - start) / (end - start) * sections), 0, sections - 1);
        slices[section].push_back(point[across]);
    }
    std::vector<double> widths, centers;
    for (const auto &slice : slices)
    {
        if (slice.size() < 20)
        {
            continue;
        }
        const double low = quantile(slice, 0.02);
        const double high = quantile(slice, 0.98);
        widths.push_back(high - low);
        centers.push_back((high + low) / 2.0);
    }
    if (widths.size() < static_cast<std::size_t>(sections * 0.6))
    {
        return {};
    }
    const double width = quantile(widths, 0.5);
    const double spread = quantile(widths, 0.75) - quantile(widths, 0.25);
    // 横向点带须可分辨且在各截面稳定，宽点团不能伪报为板厚。
    if (width < 0.5 || width > (end - start) * 0.2 || spread > width * 0.3)
    {
        return {};
    }
    return Strip{quantile(centers, 0.5), width, spread, coordinate(points, 1, 0.5), points.size()};
}

RecognizedPlate sidePlate(std::string id, const Strip &strip, double x0, double x1, double base)
{
    return {std::move(id),
            {x0, base, strip.center - strip.thickness / 2.0},
            {x1, strip.height, strip.center + strip.thickness / 2.0},
            2,
            strip.thickness,
            strip.spread,
            strip.support,
            "observed_edge_width"};
}

void addRibs(PlateRecognition &result, std::vector<Point> points, double base, double z0, double z1, double length,
             std::stop_token stop)
{
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.x() < b.x(); });
    const double gap = std::max(2.0, length * 0.0005);
    std::size_t start = 0;
    std::size_t station = 0;
    while (start < points.size())
    {
        checkpoint(stop, {}, 0, {});
        std::size_t end = start + 1;
        while (end < points.size() && points[end].x() - points[end - 1].x() <= gap)
        {
            ++end;
        }
        const std::vector<Point> cluster(points.begin() + start, points.begin() + end);
        if (const auto strip = measureStrip(cluster, 2, 0, z0, z1, 8))
        {
            ++station;
            result.plates.push_back({"rib-" + std::to_string(station) + "-right",
                                     {strip->center - strip->thickness / 2.0, base, z0},
                                     {strip->center + strip->thickness / 2.0, strip->height, z1},
                                     0,
                                     strip->thickness,
                                     strip->spread,
                                     strip->support,
                                     "observed_edge_width"});
        }
        start = end;
    }
}
} // namespace

PlateRecognition recognizePlates(const CloudData &cloud, const BeamEstimate &estimate, std::stop_token stop,
                                 const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0.0, "从点云精修底部观测面方向");
    if (cloud.points.size() < 100)
    {
        throw GeometryError("insufficient_evidence", "板件识别至少需要 100 个有效点。");
    }
    PlateRecognition result;
    result.frame = refineFrame(cloud, estimate, stop);
    auto points = project(cloud, result.frame, stop);
    const double base = coordinate(points, 1, 0.5);
    const double upper = coordinate(points, 1, 0.99);
    const double x0 = coordinate(points, 0, 0.001), x1 = coordinate(points, 0, 0.999);
    const double z0 = coordinate(points, 2, 0.001), z1 = coordinate(points, 2, 0.999);
    const double baseBand = std::max(1.0, (upper - base) * 0.08);
    const auto baseSupport = std::count_if(points.begin(), points.end(),
                                           [&](const auto &point) { return std::abs(point.y() - base) <= baseBand; });
    result.plates.push_back({"web",
                             {x0, base, z0},
                             {x1, base, z1},
                             1,
                             {},
                             0,
                             static_cast<std::uint64_t>(baseSupport),
                             "observed_surface_thickness_unresolved"});
    checkpoint(stop, progress, 0.4, "识别可见顶边条带及各板件厚度");
    if (upper - base < 5)
    {
        result.warnings.emplace_back("点云没有足够高度分离的顶边，未推断侧板或肋板；底板保留观测面，板厚未识别。");
        return result;
    }
    std::vector<Point> left, right, top;
    for (const auto &point : points)
    {
        if (point.y() <= base + (upper - base) * 0.65)
        {
            continue;
        }
        top.push_back(point);
        if (point.z() < z0 + (z1 - z0) * 0.1)
        {
            left.push_back(point);
        }
        if (point.z() > z1 - (z1 - z0) * 0.1)
        {
            right.push_back(point);
        }
    }
    const auto first = measureStrip(left, 0, 2, x0, x1, 48);
    const auto second = measureStrip(right, 0, 2, x0, x1, 48);
    if (!first || !second)
    {
        result.warnings.emplace_back("两侧纵向顶边的支持点或厚度一致性不足，无法估算底板厚度；保留观测面，不补默认板厚。");
        return result;
    }
    const double inside0 = first->center + first->thickness / 2;
    const double inside1 = second->center - second->thickness / 2;
    if (inside1 <= inside0)
    {
        throw GeometryError("invalid_recognition", "两侧翼板内侧面没有有效净间距，无法构建底板。");
    }
    // 观测面仍是底板上表面。底板夹在两翼板之间；翼板向下贴齐，肋板仍落在上表面。
    auto &bottom = result.plates.front();
    bottom.thicknessMm = std::max(first->thickness, second->thickness);
    bottom.minimumMm.y = base - *bottom.thicknessMm;
    bottom.minimumMm.z = inside0;
    bottom.maximumMm.z = inside1;
    bottom.method = "inferred_side_plate_max";
    const double underside = bottom.minimumMm.y;
    result.plates.push_back(sidePlate("flange-bottom", *first, coordinate(left, 0, 0.001),
                                     coordinate(left, 0, 0.999), underside));
    result.plates.push_back(sidePlate("flange-top", *second, coordinate(right, 0, 0.001),
                                     coordinate(right, 0, 0.999), underside));
    const double margin = std::max(first->thickness, second->thickness) * 2;
    std::erase_if(top,
                  [&](const Point &point) { return point.z() <= inside0 + margin || point.z() >= inside1 - margin; });
    addRibs(result, std::move(top), base, inside0, inside1, x1 - x0, stop);
    result.warnings.emplace_back("底板厚度按两块侧板识别厚度的最大值估算，向观测面背侧加厚；不是底板实测厚度。");
    result.warnings.emplace_back("按 H 型钢连接关系将底板限定在两翼板内侧，并将翼板下沿贴合底板底面；肋板落在底板上表面。");
    result.warnings.emplace_back("侧板和肋板厚度为可见顶边横向宽度的分段稳健估计；各板独立识别，不取名义规格或经验初值。");
    result.warnings.emplace_back("当前按直板构建；弯曲、缺口、孔洞及不可见的另一侧板件不作自动补全。");
    checkpoint(stop, progress, 1.0, "板件位置与可识别板厚提取完成");
    return result;
}
} // namespace reverse
