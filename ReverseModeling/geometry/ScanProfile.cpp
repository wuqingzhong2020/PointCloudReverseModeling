/*
 * 文件描述：
 * 对有序、共面的可见顶边进行有界误差直线/圆弧拟合，优先采用更简单的直线。
 * 圆弧必须有至少五个支持点、同向转角和可分辨弓高；不按四分之一圆/半圆先验凭空补孔。
 * 使用精确 OCCT 圆弧及直线生成闭合轮廓和等厚拉伸体；连接、平面和尺寸检查失败时拒绝建模。
 */
#include <algorithm>
#include <cmath>
#include <numbers>

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include "geometry/Geometry.h"
#include "geometry/ScanProfile.h"

namespace reverse::detail
{
namespace
{
gp_Pnt point(Vec3 p) { return {p.x, p.y, p.z}; }
Vec3 vector(gp_Pnt p) { return {p.X(), p.Y(), p.Z()}; }
bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

bool lineFits(std::span<const Vec3> points, double tolerance)
{
    const gp_Pnt first = point(points.front());
    const gp_Vec chord(first, point(points.back()));
    const double length = chord.Magnitude();
    if (length < 1e-8) return false;
    for (auto p : points)
    {
        const gp_Vec delta(first, point(p));
        const double t = delta.Dot(chord) / (length * length);
        if (t < 0 || t > 1 || delta.Crossed(chord).Magnitude() / length > tolerance) return false;
    }
    return true;
}

bool arcFits(std::span<const Vec3> points, double tolerance, Vec3 &middle)
{
    if (points.size() < 5) return false;
    const gp_Pnt a = point(points.front()), b = point(points[points.size() / 2]), c = point(points.back());
    const gp_Vec ab(a, b), ac(a, c);
    const gp_Vec cross = ab.Crossed(ac);
    const double squared = cross.SquareMagnitude();
    if (squared < 1e-12 || cross.Magnitude() / ac.Magnitude() < 2 * tolerance) return false;
    const gp_Vec offset = (ac.Crossed(cross) * ab.SquareMagnitude() +
                           cross.Crossed(ab) * ac.SquareMagnitude()) / (2 * squared);
    const gp_Pnt center = a.Translated(offset);
    const double radius = center.Distance(a);
    if (!std::isfinite(radius) || radius < 2 * tolerance) return false;
    const gp_Vec normal = cross.Normalized();
    double sweep = 0;
    gp_Vec previous(center, a);
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const gp_Vec current(center, point(points[i]));
        if (std::abs(current.Magnitude() - radius) > tolerance ||
            std::abs(current.Dot(normal)) > tolerance) return false;
        const double angle = std::atan2(normal.Dot(previous.Crossed(current)), previous.Dot(current));
        if (angle < -1e-6 || angle > std::numbers::pi / 2) return false;
        sweep += angle;
        previous = current;
    }
    if (sweep < 0.15 || sweep > std::numbers::pi + 0.05) return false;
    middle = vector(b);
    return true;
}
}

std::vector<PlateBoundarySegment> fitScanProfile(std::span<const Vec3> points, double tolerance,
                                                std::stop_token stop)
{
    if (!std::isfinite(tolerance) || tolerance <= 0 || points.size() < 2)
        throw GeometryError("invalid_profile", "可见轮廓需要有效容差和至少两个点。");
    for (auto p : points)
        if (!finite(p)) throw GeometryError("invalid_profile", "可见轮廓包含无效坐标。");
    std::vector<PlateBoundarySegment> result;
    std::size_t start = 0;
    while (start + 1 < points.size())
    {
        if (stop.stop_requested()) throw GeometryError("cancelled", "可见轮廓拟合已取消。");
        std::size_t best = start + 1;
        PlateBoundarySegment segment{points[start], points[best], {}};
        // 每个候选均检查全部支持点；失败不立即终止，避免短圆弧尚不足五点时被切成碎线。
        for (std::size_t end = start + 2; end < points.size(); ++end)
        {
            if (stop.stop_requested()) throw GeometryError("cancelled", "可见轮廓拟合已取消。");
            auto span = points.subspan(start, end - start + 1);
            Vec3 middle;
            if (lineFits(span, tolerance))
            {
                best = end;
                segment = {points[start], points[end], {}};
            }
            else if (arcFits(span, tolerance, middle))
            {
                best = end;
                segment = {points[start], points[end], middle};
            }
        }
        result.push_back(segment);
        start = best;
    }
    return result;
}

TopoDS_Shape buildScanProfile(const RecognizedPlate &plate)
{
    if (!plate.thicknessMm || plate.outline.size() < 3 || plate.normalAxis == 1)
        throw GeometryError("invalid_profile", "可见顶边轮廓需要有厚度的竖直板件。");
    const double low[] = {plate.minimumMm.x, plate.minimumMm.y, plate.minimumMm.z};
    const double high[] = {plate.maximumMm.x, plate.maximumMm.y, plate.maximumMm.z};
    const auto valid = [&](Vec3 p)
    {
        if (!finite(p)) return false;
        const double q[] = {p.x, p.y, p.z};
        for (int axis = 0; axis < 3; ++axis)
            if (q[axis] < low[axis] - 1e-6 || q[axis] > high[axis] + 1e-6) return false;
        return std::abs(q[plate.normalAxis] - low[plate.normalAxis]) <= 1e-6;
    };
    BRepBuilderAPI_MakeWire wire;
    for (std::size_t i = 0; i < plate.outline.size(); ++i)
    {
        const auto &s = plate.outline[i];
        if (!valid(s.firstMm) || !valid(s.lastMm) || (s.arcMiddleMm && !valid(*s.arcMiddleMm)) ||
            point(s.lastMm).Distance(point(plate.outline[(i + 1) % plate.outline.size()].firstMm)) > 1e-6)
            throw GeometryError("invalid_profile", "可见轮廓坐标或连接无效，未生成替代矩形。");
        if (s.arcMiddleMm)
        {
            GC_MakeArcOfCircle arc(point(s.firstMm), point(*s.arcMiddleMm), point(s.lastMm));
            if (!arc.IsDone()) throw GeometryError("invalid_profile", "圆弧轮廓退化。");
            wire.Add(BRepBuilderAPI_MakeEdge(arc.Value()));
        }
        else wire.Add(BRepBuilderAPI_MakeEdge(point(s.firstMm), point(s.lastMm)));
    }
    if (!wire.IsDone()) throw GeometryError("invalid_profile", "可见轮廓未能闭合。");
    BRepBuilderAPI_MakeFace face(wire.Wire(), true);
    if (!face.IsDone()) throw GeometryError("invalid_profile", "可见轮廓不能构成平面。");
    gp_Vec extrusion(0, 0, 0);
    extrusion.SetCoord(plate.normalAxis + 1, *plate.thicknessMm);
    return BRepPrimAPI_MakePrism(face.Face(), extrusion).Shape();
}
}
