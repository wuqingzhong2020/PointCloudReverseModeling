/*
 * 文件描述：
 * 从单侧正交板件点云识别底部观测面、两条纵向顶边及实际横向肋板站位。
 * 沿条带分段估计横向宽度作为板厚，并保存支持点数及截面离散度。
 * 底板按两侧板厚最大值向观测面背侧加厚，夹在翼板内侧；翼板下沿对齐底板底面。
 * 保留各翼板独立观测端部，供建模阶段有界收齐底板突出；侧板证据不足时仅保留底部观测面。
 * 由腹板残差自动分离高低筋板；保留筋板的实际支持范围和可见直线/圆弧顶边。
 * 不由俯视盲区推断根部孔洞，不把观测断带自动跨接为焊接连接。
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <utility>

#include <Eigen/Dense>

#include "geometry/Geometry.h"
#include "geometry/ScanProfile.h"
#include "geometry/ObservedWebHoles.h"

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
    // 全云 PCA 可能被台面/地面拖偏；俯视 H 型钢的纵向顶边在横向投影中形成长条峰。
    // 只修正梁长方向附近的小角度，不依赖不可见侧面或固定板厚。
    const auto aligned = project(cloud, frame, stop);
    const double floor = coordinate(aligned, 1, 0.5);
    const double ceiling = coordinate(aligned, 1, 0.99);
    std::vector<Point> top;
    const std::size_t stride = std::max<std::size_t>(1, aligned.size() / 100000);
    for (std::size_t i = 0; i < aligned.size(); i += stride)
        if (aligned[i].y() > floor + (ceiling - floor) * 0.5) top.push_back(aligned[i]);
    if (ceiling - floor > 5 && top.size() > 100)
    {
        const double width = coordinate(top, 2, 0.999) - coordinate(top, 2, 0.001);
        const double halfRange = std::max(std::abs(coordinate(top, 2, 0)), std::abs(coordinate(top, 2, 1))) +
                                 0.15 * std::max(std::abs(coordinate(top, 0, 0)), std::abs(coordinate(top, 0, 1))) + 10;
        const double cell = std::max(1.0, width / 1024);
        const auto concentration = [&](double angle)
        {
            std::vector<std::uint32_t> counts(static_cast<std::size_t>(2 * halfRange / cell) + 2);
            for (const auto &p : top)
            {
                const double z = -std::sin(angle) * p.x() + std::cos(angle) * p.z();
                const auto index = static_cast<std::size_t>(std::max(0.0, (z + halfRange) / cell));
                if (index < counts.size()) ++counts[index];
            }
            double score = 0;
            for (auto n : counts) score += static_cast<double>(n) * n;
            return score;
        };
        double bestAngle = 0, bestScore = concentration(0);
        for (int i = -50; i <= 50; ++i)
        {
            checkpoint(stop, {}, 0, {});
            const double angle = i * 0.002, score = concentration(angle);
            if (score > bestScore) { bestScore = score; bestAngle = angle; }
        }
        const double coarse = bestAngle;
        for (int i = -20; i <= 20; ++i)
        {
            const double angle = coarse + i * 0.0001, score = concentration(angle);
            if (score > bestScore) { bestScore = score; bestAngle = angle; }
        }
        const Point x = eigen(frame.xAxis), z = eigen(frame.zAxis);
        frame.xAxis = vector(std::cos(bestAngle) * x + std::sin(bestAngle) * z);
        frame.zAxis = vector(-std::sin(bestAngle) * x + std::cos(bestAngle) * z);
    }
    return frame;
}

// 在局部腹板块内去除斜率，再估计残差。整体翘曲不能被当成相机噪声放大轮廓容差。
double localSurfaceNoise(const std::vector<Point> &points, double base, double band,
                         double x0, double x1, double z0, double z1, std::stop_token stop)
{
    struct Tile
    {
        Eigen::Matrix3d matrix = Eigen::Matrix3d::Zero();
        Point right = Point::Zero(), fit = Point::Zero();
        std::size_t count{};
        bool usable{};
    };
    std::array<Tile, 64 * 16> tiles;
    const auto support = std::count_if(points.begin(), points.end(), [&](const auto &p) { return std::abs(p.y() - base) <= band; });
    const double tileBudget = std::max(1.0, static_cast<double>(support) / 32);
    const int xCount = std::clamp(static_cast<int>(std::sqrt(tileBudget * (x1 - x0) / (z1 - z0))), 1, 64);
    const int zCount = std::clamp(static_cast<int>(tileBudget / xCount), 1, 16);
    const auto location = [&](const Point &p)
    {
        const double x = (p.x() - x0) / (x1 - x0) * xCount;
        const double z = (p.z() - z0) / (z1 - z0) * zCount;
        const int ix = std::clamp(static_cast<int>(x), 0, xCount - 1), iz = std::clamp(static_cast<int>(z), 0, zCount - 1);
        return std::pair{ix * 16 + iz, Point{x - ix, z - iz, 1}};
    };
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (i % 16384 == 0) checkpoint(stop, {}, 0, {});
        const auto &p = points[i];
        if (std::abs(p.y() - base) > band) continue;
        const auto [index, row] = location(p);
        auto &tile = tiles[index];
        tile.matrix += row * row.transpose();
        tile.right += row * (p.y() - base);
        ++tile.count;
    }
    for (auto &tile : tiles)
        if (tile.count >= 12 && tile.matrix.fullPivLu().isInvertible())
        {
            tile.fit = tile.matrix.ldlt().solve(tile.right);
            tile.usable = tile.fit.allFinite();
        }
    std::vector<double> residuals;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (i % 16384 == 0) checkpoint(stop, {}, 0, {});
        const auto &p = points[i];
        if (std::abs(p.y() - base) > band) continue;
        const auto [index, row] = location(p);
        if (tiles[index].usable) residuals.push_back(std::abs(p.y() - base - tiles[index].fit.dot(row)));
    }
    if (residuals.size() < 100)
        for (const auto &p : points)
            if (std::abs(p.y() - base) <= band) residuals.push_back(std::abs(p.y() - base));
    return 1.4826 * quantile(std::move(residuals), 0.5);
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

// 顶边按横向厚度中部取样，避免侧面/毛刺将单侧扫描的上沿拉低。
void applyVisibleProfile(RecognizedPlate &plate, const std::vector<Point> &points, int along,
                         double tolerance, std::stop_token stop)
{
    const double low[] = {plate.minimumMm.x, plate.minimumMm.y, plate.minimumMm.z};
    const double high[] = {plate.maximumMm.x, plate.maximumMm.y, plate.maximumMm.z};
    const double length = high[along] - low[along];
    const double sampleStep = std::sqrt(length * *plate.thicknessMm / std::max<std::size_t>(1, points.size()));
    const int sections = std::clamp(static_cast<int>(length / std::max({tolerance * 2, sampleStep * 2, 0.5})), 8, 128);
    std::vector<std::vector<Point>> slices(sections);
    const double middle = (low[plate.normalAxis] + high[plate.normalAxis]) / 2;
    for (const auto &point : points)
    {
        if (std::abs(point[plate.normalAxis] - middle) > *plate.thicknessMm * 0.4) continue;
        const int section = std::clamp(static_cast<int>((point[along] - low[along]) / length * sections),
                                       0, sections - 1);
        slices[section].push_back(point);
    }
    std::vector<Vec3> samples;
    int missing = 0;
    for (const auto &slice : slices)
    {
        if (slice.size() < 3) { ++missing; continue; }
        Point p;
        p[along] = coordinate(slice, along, 0.5);
        p[1] = coordinate(slice, 1, 0.5);
        p[plate.normalAxis] = low[plate.normalAxis];
        samples.push_back(vector(p));
    }
    // 缺失段不能用圆弧先验补齐；仅在连续支持且非平顶时采用轮廓。
    if (samples.size() < 5 || missing > sections / 10) return;
    double minHeight = samples.front().y, maxHeight = minHeight;
    for (const auto &p : samples) { minHeight = std::min(minHeight, p.y); maxHeight = std::max(maxHeight, p.y); }
    if (maxHeight - minHeight <= 2 * tolerance) return;
    // 两端只延伸到本板实际观测界限，不延伸到另一块板。
    if (along == 0) { samples.front().x = low[along]; samples.back().x = high[along]; }
    else { samples.front().z = low[along]; samples.back().z = high[along]; }
    auto profile = detail::fitScanProfile(samples, tolerance, stop);
    Vec3 bottomFirst = samples.front(), bottomLast = samples.back();
    bottomFirst.y = bottomLast.y = low[1];
    plate.outline.push_back({bottomFirst, samples.front(), {}});
    plate.outline.insert(plate.outline.end(), profile.begin(), profile.end());
    plate.outline.push_back({samples.back(), bottomLast, {}});
    plate.outline.push_back({bottomLast, bottomFirst, {}});
    plate.maximumMm.y = maxHeight;
}

void addRibs(PlateRecognition &result, std::vector<Point> points, double base, double z0, double z1, double length,
             std::stop_token stop)
{
    std::sort(points.begin(), points.end(), [](const Point &a, const Point &b) { return a.x() < b.x(); });
    const double gap = std::max(2.0, length * 0.0005);
    std::size_t start = 0, station = 0;
    while (start < points.size())
    {
        checkpoint(stop, {}, 0, {});
        std::size_t end = start + 1;
        while (end < points.size() && points[end].x() - points[end - 1].x() <= gap) ++end;
        std::vector<Point> cluster(points.begin() + start, points.begin() + end);
        // 同一站位可以是分离的短筋板；按实际支持区域分别检测，不默认横跨整个净宽。
        std::sort(cluster.begin(), cluster.end(), [](const Point &a, const Point &b) { return a.z() < b.z(); });
        const double width = coordinate(cluster, 0, 0.98) - coordinate(cluster, 0, 0.02);
        const double spacing = std::sqrt(std::max(0.01, width * (z1 - z0) / cluster.size()));
        const double alongGap = std::max({2.0, spacing * 3, result.surfaceNoiseMm * 6});
        std::size_t begin = 0;
        while (begin < cluster.size())
        {
            std::size_t finish = begin + 1;
            while (finish < cluster.size() && cluster[finish].z() - cluster[finish - 1].z() <= alongGap) ++finish;
            const std::vector<Point> part(cluster.begin() + begin, cluster.begin() + finish);
            double startZ = coordinate(part, 2, 0.001), endZ = coordinate(part, 2, 0.999);
            std::vector<Point> interior;
            for (const auto &p : part)
                if (p.z() > startZ + (endZ - startZ) * 0.1 && p.z() < endZ - (endZ - startZ) * 0.1)
                    interior.push_back(p);
            if (const auto strip = measureStrip(interior, 2, 0, startZ + (endZ - startZ) * 0.1,
                                                 endZ - (endZ - startZ) * 0.1, 8))
            {
                // 只跨越一个采样尺度内的边缘缺样；更大的间隙保留并提示。
                const double edgeTolerance = std::max(result.contourToleranceMm, spacing * 1.5);
                if (startZ - z0 <= edgeTolerance) startZ = z0;
                if (z1 - endZ <= edgeTolerance) endZ = z1;
                RecognizedPlate plate{"rib-" + std::to_string(++station) + "-right",
                    {strip->center - strip->thickness / 2, base, startZ},
                    {strip->center + strip->thickness / 2, strip->height, endZ},
                    0, strip->thickness, strip->spread, strip->support, "observed_edge_width"};
                applyVisibleProfile(plate, part, 2, result.contourToleranceMm, stop);
                if (startZ > z0 + edgeTolerance || endZ < z1 - edgeTolerance)
                    result.warnings.emplace_back(plate.id + "：顶边未连续延伸到两侧翼板，已保留可见范围；端部间隙或遮挡需核对。");
                result.plates.push_back(std::move(plate));
            }
            begin = finish;
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
    std::vector<double> surfaceHeights;
    for (const auto &point : points)
        if (std::abs(point.y() - base) <= baseBand) surfaceHeights.push_back(point.y());
    result.surfaceNoiseMm = localSurfaceNoise(points, base, baseBand, x0, x1, z0, z1, stop);
    result.scanSpacingMm = std::sqrt((x1 - x0) * (z1 - z0) / std::max<std::size_t>(1, baseSupport));
    result.contourToleranceMm = std::max({0.15, result.surfaceNoiseMm * 3, result.scanSpacingMm * 0.25});
    const double minimumRise = std::max({1.0, result.surfaceNoiseMm * 6,
        quantile(std::move(surfaceHeights), 0.995) - base + 3 * result.surfaceNoiseMm});
    result.plates.push_back({"web",
                             {x0, base, z0},
                             {x1, base, z1},
                             1,
                             {},
                             0,
                             static_cast<std::uint64_t>(baseSupport),
                             "observed_surface_thickness_unresolved"});
    checkpoint(stop, progress, 0.4, "识别可见顶边条带及各板件厚度");
    if (upper - base < minimumRise)
    {
        result.warnings.emplace_back("点云没有足够高度分离的顶边，未推断侧板或肋板；底板保留观测面，板厚未识别。");
        return result;
    }
    std::vector<Point> left, right, top;
    for (const auto &point : points)
    {
        if (point.y() <= base + minimumRise)
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
    // 翼板各自选取上沿，允许两侧高度不同；筋板保留全部高于腹板噪声带的点。
    const auto filterFlange = [&](std::vector<Point> &strip)
    {
        if (strip.empty()) return;
        const double topHeight = coordinate(strip, 1, 0.9);
        std::erase_if(strip, [&](const auto &p) { return p.y() < topHeight - std::max(minimumRise, (topHeight - base) * 0.35); });
    };
    filterFlange(left);
    filterFlange(right);
    auto first = measureStrip(left, 0, 2, x0, x1, 48);
    auto second = measureStrip(right, 0, 2, x0, x1, 48);
    if (!first || !second)
    {
        // 外部背景不一定在最低高度；从高于腹板的长条峰寻找两个稳定翼板，不直接删除最低平面。
        const double cell = std::max(1.0, (z1 - z0) / 1024);
        std::vector<std::size_t> histogram(static_cast<std::size_t>((z1 - z0) / cell) + 2);
        for (const auto &p : top)
        {
            if (p.z() < z0 || p.z() > z1) continue;
            ++histogram[static_cast<std::size_t>((p.z() - z0) / cell)];
        }
        std::vector<std::size_t> candidates(histogram.size());
        std::iota(candidates.begin(), candidates.end(), 0);
        std::sort(candidates.begin(), candidates.end(), [&](auto a, auto b) { return histogram[a] > histogram[b]; });
        const double radius = std::clamp((upper - base) * 0.2, 8.0, 50.0);
        std::vector<double> examined;
        std::vector<std::pair<Strip, std::vector<Point>>> accepted;
        for (auto index : candidates)
        {
            checkpoint(stop, {}, 0, {});
            if (histogram[index] < 20 || examined.size() >= 32) break;
            const double center = z0 + (index + 0.5) * cell;
            if (std::any_of(examined.begin(), examined.end(), [&](auto v) { return std::abs(v - center) < radius; })) continue;
            examined.push_back(center);
            std::vector<Point> stripPoints;
            for (const auto &p : top)
                if (std::abs(p.z() - center) < radius) stripPoints.push_back(p);
            filterFlange(stripPoints);
            if (const auto strip = measureStrip(stripPoints, 0, 2, x0, x1, 48))
            {
                if (std::any_of(accepted.begin(), accepted.end(), [&](const auto &v) {
                    return std::abs(v.first.center - strip->center) < 4 * std::max(v.first.thickness, strip->thickness); })) continue;
                accepted.emplace_back(*strip, std::move(stripPoints));
                if (accepted.size() == 2) break;
            }
        }
        if (accepted.size() == 2)
        {
            if (accepted[0].first.center > accepted[1].first.center) std::swap(accepted[0], accepted[1]);
            first = accepted[0].first; left = std::move(accepted[0].second);
            second = accepted[1].first; right = std::move(accepted[1].second);
            result.warnings.emplace_back("已从连续纵向顶边定位 H 型钢翼板，外部背景未作为工件边界。");
        }
    }
    if (!first || !second)
    {
        result.warnings.emplace_back("两侧纵向顶边的支持点或厚度一致性不足，无法确定底板厚度；保留观测面，不补默认板厚。");
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
    std::vector<Point> webSupport;
    for (const auto &p : points)
        if (p.z() >= inside0 && p.z() <= inside1 && std::abs(p.y() - base) <= baseBand) webSupport.push_back(p);
    if (webSupport.size() >= 100)
    {
        bottom.minimumMm.x = coordinate(webSupport, 0, 0.001);
        bottom.maximumMm.x = coordinate(webSupport, 0, 0.999);
        bottom.supportPoints = webSupport.size();
    }
    const double underside = bottom.minimumMm.y;
    result.plates.push_back(sidePlate("flange-bottom", *first, coordinate(left, 0, 0.001),
                                     coordinate(left, 0, 0.999), underside));
    result.plates.push_back(sidePlate("flange-top", *second, coordinate(right, 0, 0.001),
                                     coordinate(right, 0, 0.999), underside));
    applyVisibleProfile(result.plates[1], left, 0, result.contourToleranceMm, stop);
    applyVisibleProfile(result.plates[2], right, 0, result.contourToleranceMm, stop);
    const double margin = std::max(0.1, result.surfaceNoiseMm * 3);
    std::erase_if(top,
                  [&](const Point &point) { return point.z() <= inside0 + margin || point.z() >= inside1 - margin; });
    addRibs(result, std::move(top), base, inside0, inside1, x1 - x0, stop);
    result.warnings.emplace_back("底板厚度取两块侧板识别厚度的最大值，向观测面背侧加厚。");
    result.warnings.emplace_back("按 H 型钢连接关系将底板限定在两翼板内侧，并将翼板下沿贴合底板底面；肋板落在底板上表面。");
    result.warnings.emplace_back("侧板和肋板厚度为可见顶边横向宽度的分段稳健估计；各板独立识别，不取名义规格或经验初值。");
    checkpoint(stop, progress, 0.85, "识别俯视可见的腹板圆孔和长圆孔");
    detail::recognizeWebHoles(cloud, result, stop);
    result.warnings.emplace_back("俯视线扫：可见顶边按直线/圆弧拟合；腹板封闭孔区拟合圆孔/长圆孔，筋板下方遮挡缺口不补造。");
    result.warnings.emplace_back("筋板根部按落在腹板上表面构建，这是装配假设；被遮挡的过焊孔、根部缺口及背面连接未确认，不自动补孔。");
    checkpoint(stop, progress, 1.0, "板件位置与可识别板厚提取完成");
    return result;
}
} // namespace reverse
