/* 文件描述：由剩余支持点生成平板或包围盒，支持规则轮廓及保留凹部的栅格外轮廓。 */
#include <algorithm>
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
#include <GProp_GProps.hxx>
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
} // namespace

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
    else
    {
        std::vector<P> outline;
        if (box || request.contour == "rectangle")
            outline = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
        else if (request.contour == "chamfer_rectangle")
        {
            const double c = request.cornerMm;
            if (c * 2 >= std::min(width, height))
                throw GeometryError("invalid_corner", "倒角退距不能达到板件较短边的一半。");
            outline = {{x0 + c, y0}, {x1 - c, y0}, {x1, y0 + c}, {x1, y1 - c},
                       {x1 - c, y1}, {x0 + c, y1}, {x0, y1 - c}, {x0, y0 + c}};
        }
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
