/*
 * 文件描述：从裁剪到单个焊件的俯视线扫点云分离主底板与高于底板的顶边。
 * 按平面采样邻接和高度连续性分组；直条恢复独立平板，弯曲/相接条带保留内外环。
 * 连接轮廓使用有界误差直线/圆弧，向底板垂直投影是明确的装配假设。
 * 非连接处高度使用观测包络；不补不可见底板厚度，不跨接大断带，不把宽物体当立板。
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <random>
#include <set>

#include <Eigen/Dense>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <TopoDS.hxx>

#include "geometry/ModelTransform.h"
#include "geometry/ObservedWebHoles.h"
#include "geometry/OtherModelRecognition.h"
#include "geometry/ScanProfile.h"

namespace reverse
{
namespace
{
using P = Eigen::Vector3d;
using Q = Eigen::Vector2d;
using Cell = std::pair<int, int>;
constexpr std::array<Cell, 4> neighbors{{{1, 0}, {0, 1}, {-1, 0}, {0, -1}}};

P eigen(Vec3 p)
{
    return {p.x, p.y, p.z};
}
Vec3 vector(const P &p)
{
    return {p.x(), p.y(), p.z()};
}
void check(std::stop_token stop)
{
    if (stop.stop_requested())
    {
        throw GeometryError("cancelled", "其他模型重建已取消。");
    }
}
double quantile(std::vector<double> values, double fraction)
{
    if (values.empty())
    {
        throw GeometryError("insufficient_evidence", "其他模型缺少有效扫描支持。");
    }
    const auto i = static_cast<std::size_t>(fraction * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + i, values.end());
    return values[i];
}
double cross(Q a, Q b)
{
    return a.x() * b.y() - a.y() * b.x();
}

// 最小面积包围矩形仅用于底板非焊接外沿及扫描栅格方向；不缩到底板上的侧板之间。
Q rectangleAxis(std::vector<Q> points)
{
    std::sort(points.begin(), points.end(), [](const Q &a, const Q &b) {
        return a.x() < b.x() || (a.x() == b.x() && a.y() < b.y());
    });
    std::vector<Q> hull;
    for (const auto &p : points)
    {
        while (hull.size() >= 2 && cross(hull.back() - hull[hull.size() - 2], p - hull.back()) <= 0)
        {
            hull.pop_back();
        }
        hull.push_back(p);
    }
    const auto lower = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it)
    {
        while (hull.size() > lower && cross(hull.back() - hull[hull.size() - 2], *it - hull.back()) <= 0)
        {
            hull.pop_back();
        }
        hull.push_back(*it);
    }
    if (hull.size() < 4)
    {
        throw GeometryError("no_base_plane", "底板支持点没有有效面积。");
    }
    hull.pop_back();
    double best = std::numeric_limits<double>::infinity();
    Q direction{1, 0};
    for (std::size_t i = 0; i < hull.size(); ++i)
    {
        const Q x = (hull[(i + 1) % hull.size()] - hull[i]).normalized();
        const Q z{-x.y(), x.x()};
        Q low = Q::Constant(std::numeric_limits<double>::infinity()), high = -low;
        for (const auto &p : hull)
        {
            const Q q{x.dot(p), z.dot(p)};
            low = low.cwiseMin(q);
            high = high.cwiseMax(q);
        }
        const double area = (high.x() - low.x()) * (high.y() - low.y());
        if (area < best)
        {
            best = area;
            direction = x;
        }
    }
    if (direction.x() < 0)
    {
        direction = -direction;
    }
    return direction;
}

BeamFrame baseFrame(const CloudData &cloud, double &noise, std::stop_token stop)
{
    std::vector<P> sample;
    const auto stride = std::max<std::size_t>(1, cloud.points.size() / 20000);
    for (std::size_t i = 0; i < cloud.points.size(); i += stride)
    {
        const P p = eigen(cloud.points[i].position);
        if (!p.allFinite())
        {
            throw GeometryError("invalid_cloud", "点云包含非有限坐标。");
        }
        sample.push_back(p);
    }
    std::mt19937 random(0x57454c44);
    std::uniform_int_distribution<std::size_t> pick(0, sample.size() - 1);
    P normal = P::UnitZ();
    double offset = 0;
    std::size_t best = 0;
    constexpr double band = 0.8;
    for (int trial = 0; trial < 240; ++trial)
    {
        check(stop);
        const P a = sample[pick(random)], b = sample[pick(random)], c = sample[pick(random)];
        P n = (b - a).cross(c - a);
        if (n.norm() < 1e-8)
        {
            continue;
        }
        n.normalize();
        if (n.z() < 0)
        {
            n = -n;
        }
        if (n.z() < 0.85)
        {
            continue;
        }
        const double d = n.dot(a);
        const auto count = std::count_if(sample.begin(), sample.end(), [&](const P &p) {
            return std::abs(n.dot(p) - d) <= band;
        });
        if (static_cast<std::size_t>(count) > best)
        {
            best = count;
            normal = n;
            offset = d;
        }
    }
    if (best < 100 || best < sample.size() / 5)
    {
        throw GeometryError("no_base_plane", "未找到主底板，请裁剪到单个焊件并保留足够的底板观测面。");
    }
    std::vector<P> base;
    P center = P::Zero();
    for (const auto &p : sample)
    {
        if (std::abs(normal.dot(p) - offset) <= band)
        {
            base.push_back(p);
            center += p;
        }
    }
    center /= static_cast<double>(base.size());
    Eigen::Matrix3d covariance = Eigen::Matrix3d::Zero();
    for (const auto &p : base)
    {
        const P d = p - center;
        covariance += d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(covariance);
    if (solver.info() != Eigen::Success || solver.eigenvalues()[1] <= 1e-6)
    {
        throw GeometryError("no_base_plane", "底板观测面退化。");
    }
    normal = solver.eigenvectors().col(0);
    if (normal.z() < 0)
    {
        normal = -normal;
    }
    const P x = (P::UnitX() - normal.x() * normal).normalized(), z = x.cross(normal);
    std::vector<Q> projected;
    std::vector<double> residuals;
    for (const auto &p : base)
    {
        const P d = p - center;
        projected.emplace_back(d.dot(x), d.dot(z));
        residuals.push_back(std::abs(d.dot(normal)));
    }
    noise = 1.4826 * quantile(residuals, 0.5);
    const Q direction = rectangleAxis(std::move(projected));
    const P along = x * direction.x() + z * direction.y();
    return {vector(center + eigen(cloud.originMm)), vector(along), vector(normal), vector(along.cross(normal))};
}

struct TopCell
{
    double height{};
    std::vector<P> points;
};

std::vector<std::vector<Vec3>> boundaryLoops(const std::set<Cell> &cells, double step, double x0, double z0,
                                          std::stop_token stop)
{
    std::multimap<Cell, Cell> edges;
    for (const auto &[x, z] : cells)
    {
        if (!cells.contains({x, z - 1}))
        {
            edges.emplace(Cell{x, z}, Cell{x + 1, z});
        }
        if (!cells.contains({x + 1, z}))
        {
            edges.emplace(Cell{x + 1, z}, Cell{x + 1, z + 1});
        }
        if (!cells.contains({x, z + 1}))
        {
            edges.emplace(Cell{x + 1, z + 1}, Cell{x, z + 1});
        }
        if (!cells.contains({x - 1, z}))
        {
            edges.emplace(Cell{x, z + 1}, Cell{x, z});
        }
    }
    std::vector<std::pair<double, std::vector<Vec3>>> loops;
    while (!edges.empty())
    {
        check(stop);
        const Cell start = edges.begin()->first;
        Cell cursor = start;
        std::vector<Vec3> loop;
        do
        {
            loop.push_back({x0 + cursor.first * step, 0, z0 + cursor.second * step});
            if (edges.count(cursor) != 1)
            {
                throw GeometryError("ambiguous_footprint", "顶边轮廓存在自接触或采样断口。");
            }
            const auto edge = edges.find(cursor);
            cursor = edge->second;
            edges.erase(edge);
        } while (cursor != start);
        double area = 0;
        for (std::size_t i = 0; i < loop.size(); ++i)
        {
            const auto &a = loop[i], &b = loop[(i + 1) % loop.size()];
            area += a.x * b.z - a.z * b.x;
        }
        // 小于四个栅格的内部空区无法与扫描漏点区分，不当作可见通孔。
        if (area > 0 || std::abs(area) > 8 * step * step)
        {
            loops.emplace_back(area, std::move(loop));
        }
    }
    std::sort(loops.begin(), loops.end(), [](const auto &a, const auto &b) { return a.first > b.first; });
    if (loops.empty() || loops.front().first <= 0 || (loops.size() > 1 && loops[1].first > 0))
    {
        throw GeometryError("ambiguous_footprint", "顶边没有单一闭合外环。");
    }
    std::vector<std::vector<Vec3>> result;
    for (auto &loop : loops)
    {
        result.push_back(std::move(loop.second));
    }
    return result;
}

std::optional<std::vector<PlateBoundarySegment>> roundedLoop(const std::vector<Vec3> &raw, double tolerance)
{
    double x0 = raw.front().x, x1 = x0, z0 = raw.front().z, z1 = z0, area = 0;
    for (std::size_t i = 0; i < raw.size(); ++i)
    {
        const auto &p = raw[i], &q = raw[(i + 1) % raw.size()];
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
        area += p.x * q.z - p.z * q.x;
    }
    const double cx = (x0 + x1) / 2, cz = (z0 + z1) / 2;
    const double hx = (x1 - x0) / 2, hz = (z1 - z0) / 2;
    const auto residual = [&](Vec3 p, double radius) {
        const double x = std::abs(p.x - cx) - hx + radius, z = std::abs(p.z - cz) - hz + radius;
        return std::hypot(std::max(x, 0.0), std::max(z, 0.0)) + std::min(std::max(x, z), 0.0) - radius;
    };
    double radius = 0, error = std::numeric_limits<double>::infinity();
    const double limit = std::min(hx, hz) * 0.9;
    for (int i = 0; i <= 160; ++i)
    {
        const double candidate = limit * i / 160;
        double sum = 0;
        for (const auto &p : raw)
        {
            const double d = residual(p, candidate);
            sum += d * d;
        }
        if (sum < error)
        {
            error = sum;
            radius = candidate;
        }
    }
    if (radius < 4 * tolerance)
    {
        return {};
    }
    std::array<int, 4> cornerSupport{};
    for (const auto &p : raw)
    {
        // 栅格拐点允许半格锯齿；每个角都须有弧段中部的连续实测边界，不能凭矩形先验补角。
        if (std::abs(residual(p, radius)) > 1.8 * tolerance)
        {
            return {};
        }
        const double x = std::abs(p.x - cx) - hx + radius;
        const double z = std::abs(p.z - cz) - hz + radius;
        if (x > radius * 0.2 && z > radius * 0.2)
        {
            ++cornerSupport[(p.x > cx ? 1 : 0) + (p.z > cz ? 2 : 0)];
        }
    }
    if (std::any_of(cornerSupport.begin(), cornerSupport.end(), [](int n) { return n < 5; }))
    {
        return {};
    }
    const double d = radius / std::sqrt(2.0);
    const std::array<Vec3, 8> p{{{x0 + radius, 0, z0}, {x1 - radius, 0, z0},
        {x1, 0, z0 + radius}, {x1, 0, z1 - radius}, {x1 - radius, 0, z1},
        {x0 + radius, 0, z1}, {x0, 0, z1 - radius}, {x0, 0, z0 + radius}}};
    std::vector<PlateBoundarySegment> result{{p[0], p[1], {}},
        {p[1], p[2], Vec3{x1 - radius + d, 0, z0 + radius - d}}, {p[2], p[3], {}},
        {p[3], p[4], Vec3{x1 - radius + d, 0, z1 - radius + d}}, {p[4], p[5], {}},
        {p[5], p[6], Vec3{x0 + radius - d, 0, z1 - radius + d}}, {p[6], p[7], {}},
        {p[7], p[0], Vec3{x0 + radius - d, 0, z0 + radius - d}}};
    if (area < 0)
    {
        std::reverse(result.begin(), result.end());
        for (auto &edge : result)
        {
            std::swap(edge.firstMm, edge.lastMm);
        }
    }
    return result;
}

std::vector<PlateBoundarySegment> fitLoop(const std::vector<Vec3> &raw, double tolerance, std::stop_token stop)
{
    check(stop);
    if (const auto rounded = roundedLoop(raw, tolerance))
    {
        return *rounded;
    }
    // 有界抽样后分成两条开链，避免将首尾重合的闭环交给直线拟合器。
    std::vector<Vec3> points;
    const auto stride = std::max<std::size_t>(1, raw.size() / 512);
    for (std::size_t i = 0; i < raw.size(); i += stride)
    {
        points.push_back(raw[i]);
    }
    const auto split = std::max_element(points.begin() + 1, points.end(), [&](Vec3 a, Vec3 b) {
        return (eigen(a) - eigen(points.front())).squaredNorm() <
               (eigen(b) - eigen(points.front())).squaredNorm();
    }) - points.begin();
    auto first = detail::fitScanProfile(std::span(points).first(split + 1), tolerance, stop);
    points.push_back(points.front());
    auto second = detail::fitScanProfile(std::span(points).subspan(split), tolerance, stop);
    first.insert(first.end(), second.begin(), second.end());
    return first;
}

std::optional<RecognizedPlate> straightPlate(const std::vector<P> &points, double step,
                                            BeamFrame &frame, std::stop_token stop)
{
    Q center = Q::Zero();
    for (const auto &p : points)
    {
        center += Q{p.x(), p.z()};
    }
    center /= static_cast<double>(points.size());
    Eigen::Matrix2d covariance = Eigen::Matrix2d::Zero();
    for (const auto &p : points)
    {
        const Q d = Q{p.x(), p.z()} - center;
        covariance += d * d.transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> solver(covariance);
    if (solver.info() != Eigen::Success || solver.eigenvalues()[1] < 25 * solver.eigenvalues()[0])
    {
        return {};
    }
    Q along = solver.eigenvectors().col(1);
    if (along.x() < 0)
    {
        along = -along;
    }
    const Q across{-along.y(), along.x()};
    std::vector<double> lengths, widths;
    for (const auto &p : points)
    {
        lengths.push_back(along.dot(Q{p.x(), p.z()}));
        widths.push_back(across.dot(Q{p.x(), p.z()}));
    }
    const double x0 = quantile(lengths, 0), x1 = quantile(lengths, 1);
    const double z0 = quantile(widths, 0.005), z1 = quantile(widths, 0.995);
    if (z1 - z0 < step || x1 - x0 < 6 * (z1 - z0))
    {
        return {};
    }
    std::map<int, std::vector<double>> transverse;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        transverse[static_cast<int>((lengths[i] - x0) / (2 * step))].push_back(widths[i]);
    }
    for (const auto &[station, samples] : transverse)
    {
        if (samples.size() >= 3 && std::abs(quantile(samples, 0.5) - (z0 + z1) / 2) > 2 * step)
        {
            return {}; // 长而弯曲的条带仍走内外轮廓流程，不把弓高误当板厚。
        }
    }
    const P x = eigen(frame.xAxis), z = eigen(frame.zAxis);
    frame.xAxis = vector(x * along.x() + z * along.y());
    frame.zAxis = vector(x * across.x() + z * across.y());
    std::map<int, std::vector<double>> heights;
    const double bin = std::max(step * 2, (x1 - x0) / 256);
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        heights[static_cast<int>((lengths[i] - x0) / bin)].push_back(points[i].y());
    }
    std::vector<Vec3> top;
    double maximum = 0;
    for (const auto &[i, samples] : heights)
    {
        const double height = quantile(samples, 0.95);
        maximum = std::max(maximum, height);
        top.push_back({std::min(x1, x0 + (i + 0.5) * bin), height, z0});
    }
    if (top.size() < 4)
    {
        return {};
    }
    top.front().x = x0;
    top.back().x = x1;
    RecognizedPlate plate;
    plate.normalAxis = 2;
    plate.minimumMm = {x0, 0, z0};
    plate.maximumMm = {x1, maximum, z1};
    plate.thicknessMm = z1 - z0;
    plate.supportPoints = points.size();
    plate.method = "other_top_width_vertical_projection";
    plate.outline = detail::fitScanProfile(top, step, stop);
    plate.outline.push_back({top.back(), {x1, 0, z0}, {}});
    plate.outline.push_back({{x1, 0, z0}, {x0, 0, z0}, {}});
    plate.outline.push_back({{x0, 0, z0}, top.front(), {}});
    return plate;
}
}

PlateRecognition recognizeOtherModel(const CloudData &cloud, std::stop_token stop, const ProgressCallback &progress)
{
    check(stop);
    if (cloud.points.size() < 200)
    {
        throw GeometryError("insufficient_evidence", "其他模型至少需要 200 个点及可见主底板。");
    }
    PlateRecognition result;
    result.type = "other";
    if (progress)
    {
        progress(0.05, "其他模型：拟合主底板与俯视扫描方向");
    }
    result.frame = baseFrame(cloud, result.surfaceNoiseMm, stop);
    const double band = std::max(0.8, 5 * result.surfaceNoiseMm);
    std::vector<P> points, basePoints;
    P low = P::Constant(std::numeric_limits<double>::infinity()), high = -low;
    for (const auto &sample : cloud.points)
    {
        if (points.size() % 8192 == 0)
        {
            check(stop);
        }
        const P world = eigen(sample.position) + eigen(cloud.originMm);
        if (!world.allFinite())
        {
            throw GeometryError("invalid_cloud", "点云包含非有限坐标。");
        }
        const P p = eigen(inverseTransformPoint(result.frame, vector(world)));
        points.push_back(p);
        if (std::abs(p.y()) <= band)
        {
            basePoints.push_back(p);
            low = low.cwiseMin(p);
            high = high.cwiseMax(p);
        }
    }
    const double area = (high.x() - low.x()) * (high.z() - low.z());
    if (!std::isfinite(area) || area <= 1 || basePoints.size() < 100)
    {
        throw GeometryError("no_base_plane", "主底板观测面积不足。");
    }
    result.scanSpacingMm = std::sqrt(area / basePoints.size());
    const double step = std::max({0.5, 1.5 * result.scanSpacingMm, std::sqrt(area / 1500000)});
    result.contourToleranceMm = step;
    RecognizedPlate base;
    base.id = "other-base";
    base.normalAxis = 1;
    base.minimumMm = {low.x(), -1, low.z()};
    base.maximumMm = {high.x(), 0, high.z()};
    base.supportPoints = basePoints.size();
    base.thicknessMm = 1.0;
    base.method = "other_base_default_thickness";
    result.plates.push_back(base);
    // 复用闭合缺点区证据检查；没有底面观测也可在观测面上保留圆孔/长圆孔。
    auto holeRecognition = result;
    holeRecognition.plates.front().id = "web";
    holeRecognition.plates.front().thicknessMm.reset();
    holeRecognition.plates.front().minimumMm.y = 0;
    detail::recognizeWebHoles(cloud, holeRecognition, stop);
    result.plates.front().holes = holeRecognition.plates.front().holes;

    std::map<Cell, TopCell> grid;
    for (const auto &p : points)
    {
        if (p.y() <= std::max(3 * band, 2 * step) || p.x() < low.x() || p.x() > high.x() ||
            p.z() < low.z() || p.z() > high.z())
        {
            continue;
        }
        const Cell key{static_cast<int>((p.x() - low.x()) / step), static_cast<int>((p.z() - low.z()) / step)};
        auto &cell = grid[key];
        cell.height = std::max(cell.height, p.y());
        cell.points.push_back(p);
    }
    std::set<Cell> remaining;
    for (const auto &[key, cell] : grid)
    {
        remaining.insert(key);
    }
    std::size_t serial = 0;
    while (!remaining.empty())
    {
        check(stop);
        std::vector<Cell> queue{*remaining.begin()};
        remaining.erase(queue.front());
        std::set<Cell> component;
        std::vector<P> support;
        double height = 0;
        for (std::size_t i = 0; i < queue.size(); ++i)
        {
            if (i % 4096 == 0)
            {
                check(stop);
            }
            const auto key = queue[i];
            const auto &cell = grid.at(key);
            component.insert(key);
            height = std::max(height, cell.height);
            support.insert(support.end(), cell.points.begin(), cell.points.end());
            for (const auto &[dx, dz] : neighbors)
            {
                const Cell next{key.first + dx, key.second + dz};
                if (remaining.contains(next) && std::abs(grid.at(next).height - cell.height) <= 2.5 * step)
                {
                    remaining.erase(next);
                    queue.push_back(next);
                }
            }
        }
        if (component.size() < 12 || support.size() < 30)
        {
            continue;
        }
        std::size_t perimeter = 0;
        for (const auto &[x, z] : component)
        {
            for (const auto &[dx, dz] : neighbors)
            {
                perimeter += !component.contains({x + dx, z + dz});
            }
        }
        // 宽顶面物体没有可解释为立板的窄条带证据，交由独立障碍物包络保留。
        const double stripWidth = 2 * component.size() * step / perimeter;
        if (stripWidth > height * 0.45 || stripWidth < step * 1.1)
        {
            continue;
        }
        BeamFrame frame = result.frame;
        auto straight = straightPlate(support, step, frame, stop);
        if (straight)
        {
            straight->id = "other-wall-" + std::to_string(++serial);
            result.plateFrames.emplace_back(straight->id, frame);
            result.plates.push_back(std::move(*straight));
            continue;
        }
        try
        {
            const auto loops = boundaryLoops(component, step, low.x(), low.z(), stop);
            RecognizedPlate wall;
            wall.id = "other-wall-" + std::to_string(serial + 1);
            wall.normalAxis = 1;
            wall.minimumMm = {high.x(), 0, high.z()};
            wall.maximumMm = {low.x(), height, low.z()};
            wall.supportPoints = support.size();
            wall.method = "other_footprint_vertical_projection";
            for (const auto &loop : loops)
            {
                wall.uprightFootprint.push_back(fitLoop(loop, step * 0.8, stop));
                for (const auto &p : loop)
                {
                    wall.minimumMm.x = std::min(wall.minimumMm.x, p.x);
                    wall.minimumMm.z = std::min(wall.minimumMm.z, p.z);
                    wall.maximumMm.x = std::max(wall.maximumMm.x, p.x);
                    wall.maximumMm.z = std::max(wall.maximumMm.z, p.z);
                }
            }
            std::vector<Vec3> topPoints;
            topPoints.reserve(support.size());
            for (const auto &point : support) topPoints.push_back(vector(point));
            (void)detail::recognizeEnclosureThickness(wall, topPoints, step);
            (void)detail::buildUprightFootprint(wall);
            result.plates.push_back(std::move(wall));
            ++serial;
        }
        catch (const GeometryError &error)
        {
            if (error.code() == "cancelled")
            {
                throw;
            }
            result.warnings.push_back(std::string("顶边组未作为连接板建模，交由障碍物流程保留：") + error.what());
        }
    }
    if (result.plates.size() == 1)
    {
        throw GeometryError("no_other_walls", "未找到可靠立板顶边，请保留顶边宽度采样并裁剪到单个底板焊件。");
    }
    result.warnings.emplace_back("其他模型：底板厚度未观测，默认 1 mm，可手动修改；底板上表面保持观测位置，外沿采用扫描包围矩形。");
    result.warnings.emplace_back("立板根部按顶边垂直投影到底板，是装配假设；遮挡根部缺口和背面焊缝未恢复。");
    result.warnings.emplace_back("相接且同高的弯板/立板保留为连接组，不推断组内拼接缝；非连接上沿采用观测高度包络。");
    if (progress)
    {
        progress(1, "其他模型：底板、立板及直线/圆弧连接轮廓识别完成");
    }
    return result;
}

namespace detail
{
TopoDS_Shape buildUprightFootprint(const RecognizedPlate &plate)
{
    if (plate.uprightFootprint.empty() || !std::isfinite(plate.maximumMm.y) ||
        plate.maximumMm.y <= plate.minimumMm.y)
    {
        throw GeometryError("invalid_footprint", "竖板连接组缺少有效轮廓或高度。");
    }
    try
    {
        std::vector<TopoDS_Wire> wires;
        const auto point = [](Vec3 p) { return gp_Pnt(p.x, p.y, p.z); };
        for (const auto &loop : plate.uprightFootprint)
        {
            if (loop.size() < 2)
            {
                throw GeometryError("invalid_footprint", "连接轮廓不闭合。");
            }
            BRepBuilderAPI_MakeWire wire;
            for (std::size_t i = 0; i < loop.size(); ++i)
            {
                const auto &edge = loop[i];
                for (auto p : {edge.firstMm, edge.lastMm, edge.arcMiddleMm.value_or(edge.firstMm)})
                {
                    if (!eigen(p).allFinite() || std::abs(p.y - plate.minimumMm.y) > 1e-6)
                    {
                        throw GeometryError("invalid_footprint", "连接轮廓不在底板面上。");
                    }
                }
                if (point(edge.lastMm).Distance(point(loop[(i + 1) % loop.size()].firstMm)) > 1e-6)
                {
                    throw GeometryError("invalid_footprint", "连接轮廓存在断口。");
                }
                if (edge.arcMiddleMm)
                {
                    GC_MakeArcOfCircle arc(point(edge.firstMm), point(*edge.arcMiddleMm), point(edge.lastMm));
                    if (!arc.IsDone())
                    {
                        throw GeometryError("invalid_footprint", "连接轮廓圆弧退化。");
                    }
                    wire.Add(BRepBuilderAPI_MakeEdge(arc.Value()));
                }
                else
                {
                    wire.Add(BRepBuilderAPI_MakeEdge(point(edge.firstMm), point(edge.lastMm)));
                }
            }
            if (!wire.IsDone())
            {
                throw GeometryError("invalid_footprint", "连接轮廓无法闭合。");
            }
            wires.push_back(wire.Wire());
        }
        BRepBuilderAPI_MakeFace face(wires.front(), true);
        for (std::size_t i = 1; i < wires.size(); ++i)
        {
            face.Add(wires[i]);
        }
        if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
        {
            throw GeometryError("invalid_footprint", "连接轮廓自交或内外环无效。");
        }
        auto shape = BRepPrimAPI_MakePrism(face.Face(), gp_Vec(0, plate.maximumMm.y - plate.minimumMm.y, 0)).Shape();
        if (!BRepCheck_Analyzer(shape).IsValid())
        {
            throw GeometryError("invalid_footprint", "连接组未形成有效实体。");
        }
        return shape;
    }
    catch (const Standard_Failure &error)
    {
        throw GeometryError("invalid_footprint", error.what());
    }
}
}
}
