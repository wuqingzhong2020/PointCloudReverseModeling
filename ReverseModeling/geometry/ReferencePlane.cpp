/*
 * 文件描述：校验三点并构造正交基准面；统一剖视偏移和反向的半空间方程，
 * 供命令、视图和单元测试使用。拒绝重合、近共线及非有限坐标，避免无效法线进入图形驱动。
 */
#include <algorithm>
#include <cmath>

#include "geometry/Geometry.h"
#include "geometry/ReferencePlane.h"

namespace reverse
{
ReferencePlane makeReferencePlane(const std::array<Vec3, 3> &points)
{
    for (const auto &p : points)
    {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            throw GeometryError("invalid_plane", "参数 --points 必须是三个有限的 XYZ 坐标（mm）。");
    }
    const Vec3 a{points[1].x - points[0].x, points[1].y - points[0].y, points[1].z - points[0].z};
    const Vec3 b{points[2].x - points[0].x, points[2].y - points[0].y, points[2].z - points[0].z};
    const double la = std::hypot(a.x, a.y, a.z), lb = std::hypot(b.x, b.y, b.z);
    if (!std::isfinite(la) || !std::isfinite(lb) || la < 1e-6 || lb < 1e-6)
        throw GeometryError("invalid_plane", "参数 --points 含重合点或无效间距，请重新选择三个不同点。");
    const Vec3 x{a.x / la, a.y / la, a.z / la}, y{b.x / lb, b.y / lb, b.z / lb};
    const Vec3 cross{x.y * y.z - x.z * y.y, x.z * y.x - x.x * y.z, x.x * y.y - x.y * y.x};
    const double length = std::hypot(cross.x, cross.y, cross.z);
    if (length < 1e-6 || length * std::min(la, lb) < 1e-6)
        throw GeometryError("invalid_plane", "参数 --points 的三个点共线或过于接近共线，无法确定基准面。");
    return {points, {cross.x / length, cross.y / length, cross.z / length}, x};
}

ReferencePlane localReferencePlane(const ReferencePlane &plane, const Vec3 &origin)
{
    auto result = plane;
    for (auto &p : result.pointsMm)
    {
        p.x -= origin.x;
        p.y -= origin.y;
        p.z -= origin.z;
    }
    return result;
}

std::array<double, 4> sectionEquation(const ReferencePlane &plane, const PlaneSection &section)
{
    if (!std::isfinite(section.offsetMm))
        throw GeometryError("invalid_section", "参数 --offset 必须是有限的毫米数值。");
    const auto &n = plane.normal;
    const auto &p = plane.pointsMm[0];
    const double sign = section.reversed ? -1.0 : 1.0;
    const std::array equation{sign * n.x, sign * n.y, sign * n.z,
        -sign * (n.x * p.x + n.y * p.y + n.z * p.z + section.offsetMm)};
    if (!std::all_of(equation.begin(), equation.end(), [](double value) { return std::isfinite(value); }))
        throw GeometryError("invalid_section", "基准面坐标与 --offset 的组合超出有效范围。");
    return equation;
}
}
