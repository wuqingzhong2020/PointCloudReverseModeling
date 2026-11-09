/* 文件描述：由剩余支持点生成平板或包围盒，支持规则轮廓及保留凹部的栅格外轮廓。 */
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <set>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <GProp_GProps.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <GeomAPI_Interpolate.hxx>
#include <Geom_BSplineCurve.hxx>
#include <NCollection_HArray1.hxx>
#include <gp_Circ.hxx>

#include "geometry/Geometry.h"
#include "geometry/LocalReconstruction.h"

namespace reverse::detail
{
namespace
{
struct P
{
    double x{}, y{};
};
Vec3 sub(Vec3 a, Vec3 b)
{
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}
double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
Vec3 unit(Vec3 a)
{
    const double length = std::sqrt(dot(a, a));
    if (!std::isfinite(length) || length < 1e-8)
        throw GeometryError("invalid_up", "水平法向必须是非零有限向量。");
    return {a.x / length, a.y / length, a.z / length};
}
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "局部重建已取消，模型未改变。");
}
double turn(P a, P b, P c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}
std::vector<P> hull(std::vector<P> p)
{
    std::sort(p.begin(), p.end(), [](P a, P b) { return a.x < b.x || (a.x == b.x && a.y < b.y); });
    p.erase(std::unique(p.begin(), p.end(), [](P a, P b) { return a.x == b.x && a.y == b.y; }), p.end());
    if (p.size() < 3)
        throw GeometryError("insufficient_points", "剩余点不能确定二维轮廓。");
    std::vector<P> h;
    for (auto v : p)
    {
        while (h.size() > 1 && turn(h[h.size() - 2], h.back(), v) <= 0)
            h.pop_back();
        h.push_back(v);
    }
    const auto lower = h.size();
    for (auto it = p.rbegin() + 1; it != p.rend(); ++it)
    {
        while (h.size() > lower && turn(h[h.size() - 2], h.back(), *it) <= 0)
            h.pop_back();
        h.push_back(*it);
    }
    h.pop_back();
    return h;
}
// 稠密扫描面离散为有限栅格，追踪真实外边界。孔洞不填造为新内孔，分离区域拒绝合成一板。
std::vector<P> concave(const std::vector<P> &p, double tolerance, std::stop_token stop)
{
    double x0 = p[0].x, x1 = x0, y0 = p[0].y, y1 = y0;
    for (auto v : p)
    {
        x0 = std::min(x0, v.x);
        x1 = std::max(x1, v.x);
        y0 = std::min(y0, v.y);
        y1 = std::max(y1, v.y);
    }
    const double cell = std::max({tolerance, (x1 - x0) / 256, (y1 - y0) / 256});
    using I = std::pair<int, int>;
    std::set<I> occupied;
    for (auto v : p)
        occupied.emplace(static_cast<int>((v.x - x0) / cell), static_cast<int>((v.y - y0) / cell));
    // 补一格扫描采样间隙，再腐蚀回原边界，不扩大板件外沿。
    std::set<I> dilated, closed;
    for (auto [x, y] : occupied)
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                dilated.emplace(x + dx, y + dy);
    for (auto [x, y] : dilated)
    {
        bool inside = true;
        for (int dx = -1; dx <= 1; ++dx)
            for (int dy = -1; dy <= 1; ++dy)
                inside &= dilated.contains({x + dx, y + dy});
        if (inside)
            closed.emplace(x, y);
    }
    check(stop);
    std::multimap<I, I> edges;
    for (auto [x, y] : closed)
    {
        if (!closed.contains({x, y - 1}))
            edges.emplace(I{x, y}, I{x + 1, y});
        if (!closed.contains({x + 1, y}))
            edges.emplace(I{x + 1, y}, I{x + 1, y + 1});
        if (!closed.contains({x, y + 1}))
            edges.emplace(I{x + 1, y + 1}, I{x, y + 1});
        if (!closed.contains({x - 1, y}))
            edges.emplace(I{x, y + 1}, I{x, y});
    }
    std::vector<P> best;
    double bestArea = 0, otherArea = 0;
    while (!edges.empty())
    {
        check(stop);
        const I start = edges.begin()->first;
        I cursor = start;
        std::vector<P> loop;
        do
        {
            loop.push_back({x0 + cursor.first * cell, y0 + cursor.second * cell});
            auto it = edges.find(cursor);
            if (it == edges.end())
                throw GeometryError("open_contour", "点云轮廓不闭合；请补充点云或选择凸多边形。");
            cursor = it->second;
            edges.erase(it);
        } while (cursor != start);
        double area = 0;
        for (std::size_t i = 0; i < loop.size(); ++i)
            area += turn({0, 0}, loop[i], loop[(i + 1) % loop.size()]);
        if (area > bestArea)
        {
            otherArea += bestArea;
            bestArea = area;
            best = std::move(loop);
        }
        else if (area > 0)
            otherArea += area;
    }
    if (best.size() < 3 || otherArea > bestArea * 0.02)
        throw GeometryError("disconnected_cloud",
                            "剩余点云存在分离区域或过于稀疏；不能可靠生成单一凹轮廓，请调整轮廓容差或选择规则轮廓。");
    // 删除近共线顶点；精度由显式容差控制，避免将数千个栅格角直接送入 CAD。
    bool changed = true;
    while (changed && best.size() > 3)
    {
        changed = false;
        for (std::size_t i = 0; i < best.size(); ++i)
        {
            const auto a = best[(i + best.size() - 1) % best.size()], b = best[i], c = best[(i + 1) % best.size()];
            const double length = std::hypot(c.x - a.x, c.y - a.y);
            if (length > 0 && std::abs(turn(a, c, b)) / length < tolerance * 0.5)
            {
                best.erase(best.begin() + i);
                changed = true;
                break;
            }
        }
    }
    if (best.size() > 1024)
        throw GeometryError("complex_contour", "轮廓过于复杂，请增大轮廓容差后重试。");
    return best;
}
// 按角选择倒角或精确的 90° 圆弧；孔角以矩形顶点为圆心，普通圆角与两条直边相切。
TopoDS_Wire cornerRectangle(double x0, double x1, double y0, double y1, double bottom,
                           double radius, const std::string &contour, const std::array<bool, 4> &treated)
{
    if (radius * 2 >= std::min(x1 - x0, y1 - y0))
        throw GeometryError("invalid_corner", "倒角退距或角部半径必须小于板件较短边的一半。");
    const P corners[] = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
    std::array<P, 4> incoming, outgoing;
    for (int i = 0; i < 4; ++i)
    {
        const auto c = corners[i], prev = corners[(i + 3) % 4], next = corners[(i + 1) % 4];
        const double r = treated[i] ? radius : 0;
        const double before = std::hypot(prev.x - c.x, prev.y - c.y);
        const double after = std::hypot(next.x - c.x, next.y - c.y);
        incoming[i] = {c.x + r * (prev.x - c.x) / before, c.y + r * (prev.y - c.y) / before};
        outgoing[i] = {c.x + r * (next.x - c.x) / after, c.y + r * (next.y - c.y) / after};
    }
    const auto point = [bottom](P p) { return gp_Pnt(p.x, p.y, bottom); };
    BRepBuilderAPI_MakeWire wire;
    for (int i = 0; i < 4; ++i)
    {
        wire.Add(BRepBuilderAPI_MakeEdge(point(outgoing[(i + 3) % 4]), point(incoming[i])));
        if (!treated[i])
            continue;
        if (contour == "chamfer_rectangle")
            wire.Add(BRepBuilderAPI_MakeEdge(point(incoming[i]), point(outgoing[i])));
        else
        {
            const double ratio = contour == "quarter_hole_rectangle" ? 1 / std::sqrt(2.0) : 1 - 1 / std::sqrt(2.0);
            const auto c = corners[i];
            const P middle{c.x + (incoming[i].x + outgoing[i].x - 2 * c.x) * ratio,
                           c.y + (incoming[i].y + outgoing[i].y - 2 * c.y) * ratio};
            GC_MakeArcOfCircle arc(point(incoming[i]), point(middle), point(outgoing[i]));
            if (!arc.IsDone())
                throw GeometryError("invalid_corner", "角部圆弧生成失败，请调整半径。");
            wire.Add(BRepBuilderAPI_MakeEdge(arc.Value()));
        }
    }
    if (!wire.IsDone())
        throw GeometryError("invalid_contour", "角部轮廓无法闭合，原部件保留。");
    return wire.Wire();
}
} // namespace

TopoDS_Shape rebuildOccupancyBox(const TopoDS_Shape &shape, const BeamFrame &frame, std::stop_token stop)
{
    check(stop);
    gp_Trsf placement;
    placement.SetValues(frame.xAxis.x, frame.yAxis.x, frame.zAxis.x, frame.originMm.x,
                        frame.xAxis.y, frame.yAxis.y, frame.zAxis.y, frame.originMm.y,
                        frame.xAxis.z, frame.yAxis.z, frame.zAxis.z, frame.originMm.z);
    // 几何变换使用独立副本，不能改变预览前或撤销历史中的拓扑。
    const auto local = BRepBuilderAPI_Transform(shape, placement.Inverted(), true).Shape();
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(local, bounds, false, false);
    if (bounds.IsVoid() || bounds.IsOpen())
        throw GeometryError("invalid_box", "所选模型没有有限的占用范围，无法生成包围盒。");
    double x0, y0, z0, x1, y1, z1;
    bounds.Get(x0, y0, z0, x1, y1, z1);
    if (x1 - x0 <= 1e-6 || y1 - y0 <= 1e-6 || z1 - z0 <= 1e-6)
        throw GeometryError("flat_box", "所选模型没有三维占用范围，无法生成实体包围盒。");
    check(stop);
    const auto box = BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), x1 - x0, y1 - y0, z1 - z0).Shape();
    const auto result = BRepBuilderAPI_Transform(box, placement, true).Shape();
    if (result.IsNull() || !BRepCheck_Analyzer(result).IsValid())
        throw GeometryError("invalid_box", "占用空间包围盒无效，原模型未改变。");
    check(stop);
    return result;
}

LocalReconstructionShape rebuildLocalShape(const std::vector<Vec3> &points, const LocalPlateSource &source,
                                           std::optional<double> thickness, const LocalReconstruction &request,
                                           std::stop_token stop)
{
    check(stop);
    if (points.size() < 12)
        throw GeometryError("insufficient_points", "当前点云中该部件剩余支持点不足 12 个；原部件保留。");
    const auto &f = source.frame;
    const Vec3 axes[] = {f.xAxis, f.yAxis, f.zAxis};
    const int normal = source.plate.normalAxis;
    Vec3 n = axes[normal], u = axes[(normal + 1) % 3], v = axes[(normal + 2) % 3];
    if (request.type == "horizontal")
    {
        n = unit(request.up);
        const Vec3 axis = std::abs(n.x) < 0.8 ? Vec3{1, 0, 0} : Vec3{0, 1, 0};
        u = unit(cross(axis, n));
        v = cross(n, u);
    }
    const auto low = source.plate.minimumMm, high = source.plate.maximumMm;
    const Vec3 localCenter{(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
    const Vec3 origin{f.originMm.x + f.xAxis.x * localCenter.x + f.yAxis.x * localCenter.y + f.zAxis.x * localCenter.z,
                      f.originMm.y + f.xAxis.y * localCenter.x + f.yAxis.y * localCenter.y + f.zAxis.y * localCenter.z,
                      f.originMm.z + f.xAxis.z * localCenter.x + f.yAxis.z * localCenter.y + f.zAxis.z * localCenter.z};
    std::vector<P> projected;
    double x0 = 1e100, x1 = -1e100, y0 = x0, y1 = x1, z0 = x0, z1 = x1;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        if (i % 4096 == 0)
            check(stop);
        auto q = sub(points[i], origin);
        const double x = dot(q, u), y = dot(q, v), z = dot(q, n);
        projected.push_back({x, y});
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
        z0 = std::min(z0, z);
        z1 = std::max(z1, z);
    }
    const bool cornerContour = request.contour == "chamfer_rectangle" ||
                               request.contour == "quarter_hole_rectangle" || request.contour == "rounded_rectangle";
    std::array<bool, 4> treated{true, true, true, true};
    // 世界 Z 为俯视扫描的上下方向。板面距竖直不超过 15° 时，沿原板框架中较竖直的面内轴补底。
    // 只补齐到原板底边，保留点云清理后的顶边和左右边界。
    if (request.type == "plate" && cornerContour && std::abs(n.z) <= std::sin(15.0 * std::acos(-1.0) / 180.0))
    {
        const bool heightIsU = std::abs(u.z) > std::abs(v.z);
        const bool positiveUp = (heightIsU ? u.z : v.z) > 0;
        if (request.mirrorBottom)
        {
            const int heightAxis = (normal + (heightIsU ? 1 : 2)) % 3;
            const double lows[] = {low.x, low.y, low.z}, highs[] = {high.x, high.y, high.z};
            const double originalHalfHeight = (highs[heightAxis] - lows[heightAxis]) / 2;
            auto &lower = heightIsU ? x0 : y0;
            auto &upper = heightIsU ? x1 : y1;
            if (positiveUp)
                lower = std::min(lower, -originalHalfHeight);
            else
                upper = std::max(upper, originalHalfHeight);
        }
        else if (heightIsU)
            treated = positiveUp ? std::array<bool, 4>{false, true, true, false}
                                 : std::array<bool, 4>{true, false, false, true};
        else
            treated = positiveUp ? std::array<bool, 4>{false, false, true, true}
                                 : std::array<bool, 4>{true, true, false, false};
    }
    const double width = x1 - x0, height = y1 - y0;
    if (width < 1e-3 || height < 1e-3)
        throw GeometryError("degenerate_projection", "点云投影退化为线或点；请选择保持原板面方向，或调整基准面。");
    if (request.type == "horizontal" && z1 - z0 > std::max(thickness.value_or(0.0) * 2, request.toleranceMm * 5))
        throw GeometryError("not_horizontal", "所选部件的点云不接近指定水平面；请选择“保持原板面”或调整基准面。");
    const bool box = request.type == "box";
    double depth = box ? z1 - z0 : thickness.value_or(0);
    if (box && depth < 1e-3)
        throw GeometryError("flat_box", "剩余点共面，不能形成有效包围盒；请选择平板轮廓。");
    double bottom = box ? z0 : -depth / 2;
    if (request.type == "horizontal")
        bottom += (z0 + z1) / 2;
    TopoDS_Wire wire;
    if (!box && request.contour == "circle")
    {
        const P center{(x0 + x1) / 2, (y0 + y1) / 2};
        double radius = 0;
        for (auto q : projected)
            radius = std::max(radius, std::hypot(q.x - center.x, q.y - center.y));
        wire = BRepBuilderAPI_MakeWire(
            BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(gp_Pnt(center.x, center.y, bottom), gp_Dir(0, 0, 1)), radius)));
    }
    else if (!box && cornerContour)
    {
        wire = cornerRectangle(x0, x1, y0, y1, bottom, request.cornerMm, request.contour, treated);
    }
    else
    {
        std::vector<P> outline;
        if (box || request.contour == "rectangle")
            outline = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        else if (request.contour == "convex")
            outline = hull(projected);
        else
            outline = concave(projected, request.toleranceMm, stop);
        if (!box && request.contour == "smooth")
        {
            auto poles = new NCollection_HArray1<gp_Pnt>(1, static_cast<int>(outline.size()));
            for (std::size_t i = 0; i < outline.size(); ++i)
                poles->SetValue(static_cast<int>(i + 1), gp_Pnt(outline[i].x, outline[i].y, bottom));
            GeomAPI_Interpolate interpolation(poles, true, 1e-6);
            interpolation.Perform();
            if (!interpolation.IsDone())
                throw GeometryError("curve_failed", "连续曲线拟合失败，请增大容差或选择任意多边形。");
            wire = BRepBuilderAPI_MakeWire(BRepBuilderAPI_MakeEdge(interpolation.Curve()));
        }
        else
        {
            BRepBuilderAPI_MakePolygon polygon;
            for (auto q : outline)
                polygon.Add(gp_Pnt(q.x, q.y, bottom));
            polygon.Close();
            wire = polygon.Wire();
        }
    }
    check(stop);
    BRepBuilderAPI_MakeFace face(wire);
    if (!face.IsDone())
        throw GeometryError("invalid_contour", "轮廓无法闭合成面，原部件保留。");
    TopoDS_Shape result = face.Shape();
    if (depth > 0)
        result = BRepPrimAPI_MakePrism(result, gp_Vec(0, 0, depth)).Shape();
    gp_Trsf transform;
    transform.SetValues(u.x, v.x, n.x, origin.x, u.y, v.y, n.y, origin.y, u.z, v.z, n.z, origin.z);
    result = BRepBuilderAPI_Transform(result, transform, true).Shape();
    if (result.IsNull() || !BRepCheck_Analyzer(result).IsValid())
        throw GeometryError("invalid_local_shape", "局部轮廓自交或几何无效；请调整容差或轮廓规则，原模型未改变。");
    check(stop);
    LocalReconstructionShape rebuilt{result, {}};
    if (depth > 0)
    {
        GProp_GProps volume;
        BRepGProp::VolumeProperties(result, volume);
        const auto center = volume.CentreOfMass();
        rebuilt.thickness = PlateThicknessInfo{depth, {center.X(), center.Y(), center.Z()}, n};
    }
    return rebuilt;
}
} // namespace reverse::detail
