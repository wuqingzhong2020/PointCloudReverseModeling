/*
 * 文件描述：
 * T 排专用点云识别：稳健分离水平支持层，以窄底板和其上方立板组成工件。
 * 在各板件自身坐标系中估计板厚与可见顶边，保留斜板角度和异形缺口。
 * 地面/台架、顶边规则化、边界投影及宽度限制均参与计算；不补固定默认板厚。
 */
#include <algorithm>
#include <cmath>
#include <map>
#include <queue>
#include <random>

#include <Eigen/Dense>

#include "geometry/ScanProfile.h"
#include "geometry/TBarRecognition.h"

namespace reverse
{
namespace
{
using P = Eigen::Vector3d;
struct Layer
{
    P normal;
    double offset{};
    std::vector<P> points;
};
struct Base
{
    BeamFrame frame;
    double x0{}, x1{}, z0{}, z1{};
    std::size_t support{};
    double noiseMm{};
};
P e(Vec3 p)
{
    return {p.x, p.y, p.z};
}
Vec3 v(const P &p)
{
    return {p.x(), p.y(), p.z()};
}
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "T 排重建已取消。");
}
double quantile(std::vector<double> values, double t)
{
    if (values.empty())
        throw GeometryError("insufficient_evidence", "T 排支持点不足。");
    const auto n = static_cast<std::size_t>(t * (values.size() - 1));
    std::nth_element(values.begin(), values.begin() + n, values.end());
    return values[n];
}
double coord(const std::vector<P> &points, int axis, double t)
{
    std::vector<double> a;
    a.reserve(points.size());
    for (const auto &p : points)
        a.push_back(p[axis]);
    return quantile(std::move(a), t);
}
P local(const P &p, const BeamFrame &f)
{
    const P d = p - e(f.originMm);
    return {d.dot(e(f.xAxis)), d.dot(e(f.yAxis)), d.dot(e(f.zAxis))};
}
P center(const std::vector<P> &points)
{
    P c = P::Zero();
    for (const auto &p : points)
        c += p;
    return c / double(points.size());
}
Eigen::Matrix3d covariance(const std::vector<P> &points, const P &c)
{
    Eigen::Matrix3d m = Eigen::Matrix3d::Zero();
    for (const auto &p : points)
    {
        const P d = p - c;
        m += d * d.transpose();
    }
    return m / double(points.size());
}
std::vector<Layer> horizontalLayers(std::vector<P> points, double tolerance, std::stop_token stop)
{
    std::mt19937 rng(0x54424152);
    std::shuffle(points.begin(), points.end(), rng);
    if (points.size() > 45000)
        points.resize(45000);
    std::vector<Layer> layers;
    for (int pass = 0; pass < 10 && points.size() >= 80; ++pass)
    {
        check(stop);
        // 为水平扫描底板先给出高度直方图种子，避免低占比底板在随机采样中遗漏。
        std::map<long long, std::vector<double>> heights;
        for (const auto &p : points)
            heights[static_cast<long long>(std::floor(p.z() / tolerance))].push_back(p.z());
        const auto peak = std::max_element(heights.begin(), heights.end(), [](const auto &a, const auto &b) {
            return a.second.size() < b.second.size();
        });
        P normal = P::UnitZ();
        double offset = quantile(peak->second, .5);
        std::size_t best = 0;
        for (const auto &p : points)
            if (std::abs(p.z() - offset) <= tolerance)
                ++best;
        std::uniform_int_distribution<std::size_t> choose(0, points.size() - 1);
        for (int trial = 0; trial < 220; ++trial)
        {
            if (trial % 16 == 0)
                check(stop);
            const P a = points[choose(rng)], b = points[choose(rng)], c = points[choose(rng)];
            P n = (b - a).cross(c - a);
            if (n.norm() < 1e-6)
                continue;
            n.normalize();
            if (n.z() < 0)
                n = -n;
            if (n.z() < 0.85)
                continue;
            const double d = n.dot(a);
            std::size_t count = 0;
            for (const auto &p : points)
                if (std::abs(n.dot(p) - d) <= tolerance)
                    ++count;
            if (count > best)
            {
                best = count;
                normal = n;
                offset = d;
            }
        }
        if (best < 80)
            break;
        Layer layer{normal, offset, {}};
        for (int iteration = 0; iteration < 3; ++iteration)
        {
            layer.points.clear();
            for (const auto &p : points)
                if (std::abs(layer.normal.dot(p) - layer.offset) <= tolerance)
                    layer.points.push_back(p);
            const P c = center(layer.points);
            Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> fit(covariance(layer.points, c));
            P n = fit.eigenvectors().col(0);
            if (n.z() < 0)
                n = -n;
            if (n.z() >= 0.85)
            {
                layer.normal = n;
                layer.offset = n.dot(c);
            }
        }
        // 必须按最终平面重新收集支持；否则初始斜种子漏掉的底板区域会被删除却不进入分割。
        layer.points.clear();
        for (const auto &p : points)
            if (std::abs(layer.normal.dot(p) - layer.offset) <= tolerance)
                layer.points.push_back(p);
        std::erase_if(points, [&](const P &p) { return std::abs(layer.normal.dot(p) - layer.offset) <= tolerance; });
        layers.push_back(std::move(layer));
    }
    return layers;
}
std::vector<Base> bases(const Layer &layer, const P &forward, double maxWidth, std::stop_token stop)
{
    P x = forward - layer.normal * forward.dot(layer.normal);
    if (x.norm() < 1e-6)
        x = P::UnitX() - layer.normal * layer.normal.x();
    x.normalize();
    const P z = x.cross(layer.normal).normalized();
    const P origin = center(layer.points);
    const BeamFrame initial{v(origin), v(x), v(layer.normal), v(z)};
    std::vector<P> projected;
    projected.reserve(layer.points.size());
    for (const auto &p : layer.points)
        projected.push_back(local(p, initial));
    // 网格连通仅用于拆分相互分离的工件；20 mm 可跨过立板在底板上的遮挡条带。
    constexpr double cell = 20;
    using Key = std::pair<int, int>;
    std::map<Key, std::vector<std::size_t>> grid;
    for (std::size_t i = 0; i < projected.size(); ++i)
        grid[{int(std::floor(projected[i].x() / cell)), int(std::floor(projected[i].z() / cell))}].push_back(i);
    std::vector<Base> result;
    while (!grid.empty())
    {
        check(stop);
        std::queue<Key> todo;
        todo.push(grid.begin()->first);
        std::vector<P> group;
        while (!todo.empty())
        {
            const auto key = todo.front();
            todo.pop();
            const auto found = grid.find(key);
            if (found == grid.end())
                continue;
            for (auto i : found->second)
                group.push_back(projected[i]);
            grid.erase(found);
            for (int dx = -1; dx <= 1; ++dx)
                for (int dz = -1; dz <= 1; ++dz)
                    if (dx || dz)
                    {
                        Key next{key.first + dx, key.second + dz};
                        if (grid.contains(next))
                            todo.push(next);
                    }
        }
        if (group.size() < 60)
            continue;
        const P c = center(group);
        Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> fit(covariance(group, c));
        P axis = fit.eigenvectors().col(2);
        axis.y() = 0;
        axis.normalize();
        if (axis.x() < 0)
            axis = -axis;
        P along = x * axis.x() + z * axis.z();
        const P across = along.cross(layer.normal).normalized();
        const P o = origin + x * c.x() + z * c.z();
        BeamFrame f{v(o), v(along), v(layer.normal), v(across)};
        std::vector<P> q;
        for (const auto &p : group)
            q.push_back(local(origin + x * p.x() + layer.normal * p.y() + z * p.z(), f));
        const double x0 = coord(q, 0, 0.001), x1 = coord(q, 0, 0.999), z0 = coord(q, 2, 0.001), z1 = coord(q, 2, 0.999);
        const double width = z1 - z0, length = x1 - x0;
        if (width < 5 || width > maxWidth + 1 || length < 40 || length < 1.2 * width)
            continue;
        std::vector<double> residuals;
        for (const auto &p : group)
            residuals.push_back(std::abs(p.y()));
        result.push_back({f, x0, x1, z0, z1, group.size(), 1.4826 * quantile(std::move(residuals), .5)});
    }
    return result;
}
std::optional<double> baseThickness(const Base &base, const std::vector<P> &points, const std::vector<Layer> &layers,
                                    bool ground, double tolerance)
{
    const P up = e(base.frame.yAxis), origin = e(base.frame.originMm);
    if (ground)
    {
        std::optional<double> groundDepth;
        std::size_t groundSupport = 0;
        for (const auto &layer : layers)
        {
            if (layer.normal.dot(up) < 0.995)
                continue;
            const double gap = (layer.normal.dot(origin) - layer.offset) / layer.normal.dot(up);
            if (gap > 2 * tolerance && gap <= 60 && layer.points.size() > groundSupport)
            {
                groundDepth = gap;
                groundSupport = layer.points.size();
            }
        }
        if (groundDepth)
            return groundDepth;
    }
    std::vector<double> side;
    for (const auto &p : points)
    {
        const P q = local(p, base.frame);
        const bool edge = std::min(std::abs(q.x() - base.x0), std::abs(q.x() - base.x1)) < 2 * tolerance ||
                          std::min(std::abs(q.z() - base.z0), std::abs(q.z() - base.z1)) < 2 * tolerance;
        if (edge && q.x() >= base.x0 - 2 * tolerance && q.x() <= base.x1 + 2 * tolerance &&
            q.z() >= base.z0 - 2 * tolerance && q.z() <= base.z1 + 2 * tolerance && q.y() < -tolerance && q.y() > -60)
            side.push_back(-q.y());
    }
    if (side.size() >= 30)
    {
        const double depth = quantile(side, 0.99);
        if (depth > 3 * tolerance)
            return depth;
    }
    return {};
}
std::optional<RecognizedPlate> webPlate(const Base &base, const std::vector<P> &points,
                                        const PlateBuildOptions &options, double tolerance, BeamFrame &frame,
                                        std::stop_token stop)
{
    std::vector<P> upper;
    for (const auto &p : points)
    {
        const P q = local(p, base.frame);
        if (q.x() >= base.x0 - 20 && q.x() <= base.x1 + 20 &&
            q.z() >= base.z0 - (options.perpendicularHypothesis ? 10 : options.maximumBaseWidthMm) &&
            q.z() <= base.z1 + (options.perpendicularHypothesis ? 10 : options.maximumBaseWidthMm) &&
            q.y() > 8 * tolerance)
            upper.push_back(p);
    }
    if (upper.size() < 80)
        return {};
    const P c = center(upper), up = e(base.frame.yAxis);
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> fit(covariance(upper, c));
    P normal = fit.eigenvectors().col(0);
    if (std::abs(normal.dot(up)) > 0.65)
    {
        // 俯视线扫可能只有窄顶边；直 T 可由条带宽度定厚，斜 T 不能凭顶边猜倾角。
        if (!options.perpendicularHypothesis)
            return {};
        normal = e(base.frame.zAxis);
    }
    if (options.perpendicularHypothesis)
    {
        normal -= up * normal.dot(up);
        normal.normalize();
    }
    P along = up.cross(normal).normalized();
    if (along.dot(e(base.frame.xAxis)) < 0)
    {
        normal = -normal;
        along = -along;
    }
    const P webUp = normal.cross(along).normalized();
    frame = {base.frame.originMm, v(along), v(webUp), v(normal)};
    std::vector<P> q;
    q.reserve(upper.size());
    for (const auto &p : upper)
        q.push_back(local(p, frame));
    const double x0 = coord(q, 0, .001), x1 = coord(q, 0, .999);
    double z0 = coord(q, 2, .01), z1 = coord(q, 2, .99);
    // 两侧面有成簇支持时取各面的中位位置；用噪声外包络定厚会稳定地偏厚。
    // 只有顶面条带时没有两个集中峰，仍按可见条带宽度估计。
    const double observedWidth = z1 - z0;
    if (coord(q, 2, .25) - z0 < .15 * observedWidth && z1 - coord(q, 2, .75) < .15 * observedWidth)
    {
        std::vector<double> lower, upperFace;
        const double middle = (z0 + z1) / 2;
        for (const auto &p : q)
            (p.z() < middle ? lower : upperFace).push_back(p.z());
        if (lower.size() >= 30 && upperFace.size() >= 30)
        {
            z0 = quantile(std::move(lower), .5);
            z1 = quantile(std::move(upperFace), .5);
        }
    }
    const double thickness = z1 - z0;
    if (x1 - x0 < 40 || thickness <= 3 * tolerance || thickness > std::min(80.0, (base.z1 - base.z0) * .65))
        return {};
    const double bin = std::max(2.5, (x1 - x0) / 500.0);
    std::map<int, std::vector<double>> columns;
    for (const auto &p : q)
        columns[int(std::floor((p.x() - x0) / bin))].push_back(p.y());
    std::vector<Vec3> top;
    int previous = columns.begin()->first;
    for (const auto &[index, values] : columns)
    {
        check(stop);
        if ((index - previous - 1) * bin > std::max(bin * 2, options.maximumSlotWidthMm))
            throw GeometryError("tbar_scan_gap", "T 排顶边存在超过插槽宽度上限的无支持断带，请分开工件或补扫。");
        previous = index;
        if (values.size() < 2)
            continue;
        top.push_back({std::clamp(x0 + (index + .5) * bin, x0, x1), quantile(values, .995), z0});
    }
    if (top.size() < 8)
        return {};
    top.front().x = x0;
    top.back().x = x1;
    if (options.regularUpperSurface)
    {
        // 稳健拟合规则顶边，避免少量槽底或飞点改变直线高度。
        std::vector<double> heights;
        for (auto p : top)
            heights.push_back(p.y);
        const double middle = quantile(heights, .5);
        double sx = 0, sy = 0, sxx = 0, sxy = 0, n = 0;
        for (auto p : top)
            if (std::abs(p.y - middle) < std::max(5.0, .15 * middle))
            {
                sx += p.x;
                sy += p.y;
                sxx += p.x * p.x;
                sxy += p.x * p.y;
                ++n;
            }
        if (n < 3 || n * sxx - sx * sx < 1e-8)
            return {};
        const double a = (n * sxy - sx * sy) / (n * sxx - sx * sx), b = (sy - a * sx) / n;
        top = {{x0, a * x0 + b, z0}, {x1, a * x1 + b, z0}};
    }
    const double pad = std::abs(normal.dot(up)) * std::max(std::abs(z0), std::abs(z1)) / webUp.dot(up) + tolerance;
    double root0 = x0, root1 = x1;
    if (options.projectionOntoBorder)
    {
        const P o = e(base.frame.originMm), x = e(base.frame.xAxis);
        root0 = local(o + x * base.x0, frame).x();
        root1 = local(o + x * base.x1, frame).x();
    }
    std::vector<Vec3> outline{{root0, -pad, z0}};
    outline.insert(outline.end(), top.begin(), top.end());
    outline.push_back({root1, -pad, z0});
    RecognizedPlate plate;
    plate.normalAxis = 2;
    plate.thicknessMm = thickness;
    plate.supportPoints = upper.size();
    plate.method =
        options.perpendicularHypothesis ? "tbar_observed_width_perpendicular" : "tbar_observed_width_inclined";
    plate.minimumMm = {std::min(root0, x0), -pad, z0};
    double ymax = 0;
    for (auto p : top)
        ymax = std::max(ymax, p.y);
    if (ymax < 10)
        return {};
    plate.maximumMm = {std::max(root1, x1), ymax, z1};
    // 顶边的折点/槽口在异形模式保留；拟合误差受扫描尺度限制。
    plate.outline.push_back({outline.front(), top.front(), {}});
    auto fitted = detail::fitScanProfile(top, tolerance, stop);
    plate.outline.insert(plate.outline.end(), fitted.begin(), fitted.end());
    plate.outline.push_back({top.back(), outline.back(), {}});
    plate.outline.push_back({outline.back(), outline.front(), {}});
    return plate;
}
} // namespace
void validateTBarOptions(const PlateBuildOptions &o)
{
    if (o.type != "hbeam" && o.type != "tbar" && o.type != "other")
        throw GeometryError("invalid_parameters", "重建类型必须是 hbeam、tbar 或 other。");
    if (!std::isfinite(o.maximumSlotWidthMm) || o.maximumSlotWidthMm < 0 || o.maximumSlotWidthMm > 1000 ||
        !std::isfinite(o.maximumBaseWidthMm) || o.maximumBaseWidthMm < 20 || o.maximumBaseWidthMm > 1000)
        throw GeometryError("invalid_parameters", "插槽宽度须为 0–1000 mm，底板宽度上限须为 20–1000 mm。");
    if (o.type != "hbeam" && o.mirrorSymmetry)
        throw GeometryError("invalid_parameters", "T 排和其他模型不使用 H 型钢镜像补建选项。");
}
PlateRecognition recognizeTBar(const CloudData &cloud, const BeamEstimate &estimate, const PlateBuildOptions &options,
                               std::stop_token stop, const ProgressCallback &progress)
{
    validateTBarOptions(options);
    check(stop);
    if (cloud.points.size() < 200)
        throw GeometryError("insufficient_evidence", "T 排识别至少需要 200 个有效点。");
    std::vector<P> points;
    points.reserve(cloud.points.size());
    for (const auto &p : cloud.points)
        points.push_back(e(p.position) + e(cloud.originMm));
    constexpr double tolerance = .75;
    if (progress)
        progress(0.05, "T 排：分离底板与地面/台架支持层");
    const auto layers = horizontalLayers(points, tolerance, stop);
    std::vector<Base> candidates;
    for (const auto &layer : layers)
    {
        auto found = bases(layer, e(estimate.frame.xAxis), options.maximumBaseWidthMm, stop);
        candidates.insert(candidates.end(), found.begin(), found.end());
    }
    std::sort(candidates.begin(), candidates.end(), [](const Base &a, const Base &b) { return a.support > b.support; });
    PlateRecognition result;
    result.type = "tbar";
    result.frame = estimate.frame;
    result.contourToleranceMm = tolerance;
    std::vector<Base> accepted;
    for (const auto &base : candidates)
    {
        check(stop);
        // 优先采用有最多平面支持的底板。台架横梁/残余扫描条带即使局部像 T，
        // 与已识别底板投影相交时也不能再次作为工件底板。
        if (std::any_of(accepted.begin(), accepted.end(), [&](const Base &previous) {
                double x0 = 1e30, x1 = -1e30, z0 = 1e30, z1 = -1e30;
                for (double x : {previous.x0, previous.x1})
                    for (double z : {previous.z0, previous.z1})
                    {
                        const P p =
                            e(previous.frame.originMm) + x * e(previous.frame.xAxis) + z * e(previous.frame.zAxis);
                        const P q = local(p, base.frame);
                        x0 = std::min(x0, q.x());
                        x1 = std::max(x1, q.x());
                        z0 = std::min(z0, q.z());
                        z1 = std::max(z1, q.z());
                    }
                return std::min(x1, base.x1) - std::max(x0, base.x0) > tolerance &&
                       std::min(z1, base.z1) - std::max(z0, base.z0) > tolerance;
            }))
            continue;
        BeamFrame webFrame;
        auto web = webPlate(base, points, options, tolerance, webFrame, stop);
        if (!web)
            continue;
        const auto prefix = "tbar-" + std::to_string(result.plates.size() / 2 + 1);
        RecognizedPlate bottom;
        bottom.id = prefix + "-base";
        bottom.normalAxis = 1;
        bottom.supportPoints = base.support;
        bottom.thicknessMm = baseThickness(base, points, layers, options.onGround, tolerance);
        bottom.minimumMm = {base.x0, -bottom.thicknessMm.value_or(0), base.z0};
        bottom.maximumMm = {base.x1, 0, base.z1};
        bottom.method = bottom.thicknessMm ? "tbar_observed_base_depth" : "unresolved_tbar_base";
        if (!bottom.thicknessMm)
            result.warnings.push_back(bottom.id + "：底板底面/侧面证据不足，保留观测面，不补默认厚度。");
        web->id = prefix + "-web";
        result.plateFrames.emplace_back(bottom.id, base.frame);
        result.plateFrames.emplace_back(web->id, webFrame);
        result.plates.push_back(bottom);
        result.plates.push_back(*web);
        result.surfaceNoiseMm = std::max(result.surfaceNoiseMm, base.noiseMm);
        result.scanSpacingMm =
            std::max(result.scanSpacingMm, std::sqrt((base.x1 - base.x0) * (base.z1 - base.z0) / base.support));
        accepted.push_back(base);
    }
    if (result.plates.empty())
        throw GeometryError("no_tbar", "未找到有窄底板及可定厚立板支持的 T 排；请核对类型、底板宽度上限或扫描范围。");
    if (progress)
        progress(1, "T 排板件与可见轮廓识别完成");
    return result;
}
} // namespace reverse
