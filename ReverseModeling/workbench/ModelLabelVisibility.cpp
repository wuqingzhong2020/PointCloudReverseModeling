/*
 * 文件描述：
 * 沿标注锚点到相机的视线检查模型三角网格，隐藏背面、掠视和被其他板件遮挡的标签。
 * 先用部件包围盒排除无关网格，再作双面射线三角形求交，容差排除锚点自身表面。
 */
#include <algorithm>
#include <cmath>
#include <utility>

#include "workbench/ModelLabelVisibility.h"

namespace reverse::workbench
{
namespace
{
Vec3 subtract(const Vec3 &left, const Vec3 &right)
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

double dot(const Vec3 &left, const Vec3 &right)
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

Vec3 cross(const Vec3 &left, const Vec3 &right)
{
    return {left.y * right.z - left.z * right.y,
            left.z * right.x - left.x * right.z,
            left.x * right.y - left.y * right.x};
}

bool hitsBox(const Vec3 &point, const Vec3 &direction, const Vec3 &minimum, const Vec3 &maximum,
             double nearDistance, double farDistance)
{
    const std::array origin{point.x, point.y, point.z};
    const std::array ray{direction.x, direction.y, direction.z};
    const std::array low{minimum.x, minimum.y, minimum.z};
    const std::array high{maximum.x, maximum.y, maximum.z};
    for (std::size_t axis = 0; axis < origin.size(); ++axis)
    {
        if (std::abs(ray[axis]) < 1e-12)
        {
            if (origin[axis] < low[axis] || origin[axis] > high[axis])
            {
                return false;
            }
            continue;
        }
        const double first = (low[axis] - origin[axis]) / ray[axis];
        const double second = (high[axis] - origin[axis]) / ray[axis];
        nearDistance = std::max(nearDistance, std::min(first, second));
        farDistance = std::min(farDistance, std::max(first, second));
        if (nearDistance > farDistance)
        {
            return false;
        }
    }
    return true;
}

bool hitsTriangle(const Vec3 &origin, const Vec3 &direction, const std::array<Vec3, 3> &triangle,
                  double tolerance, double farDistance)
{
    const Vec3 edge1 = subtract(triangle[1], triangle[0]);
    const Vec3 edge2 = subtract(triangle[2], triangle[0]);
    const Vec3 perpendicular = cross(direction, edge2);
    const double determinant = dot(edge1, perpendicular);
    if (std::abs(determinant) <= 1e-12 * std::sqrt(dot(edge1, edge1) * dot(edge2, edge2)))
    {
        return false;
    }
    const Vec3 offset = subtract(origin, triangle[0]);
    const double u = dot(offset, perpendicular) / determinant;
    const Vec3 q = cross(offset, edge1);
    const double v = dot(direction, q) / determinant;
    const double distance = dot(edge2, q) / determinant;
    return u >= -1e-9 && v >= -1e-9 && u + v <= 1.0 + 1e-9
        && distance > tolerance && distance < farDistance;
}
}

ModelLabelVisibility::ModelLabelVisibility(const std::vector<ModelDisplayPart> &parts)
{
    for (const auto &part : parts)
    {
        if (part.vertices.empty())
        {
            continue;
        }
        Surface surface;
        surface.minimum = surface.maximum = part.vertices.front();
        for (const auto &point : part.vertices)
        {
            surface.minimum.x = std::min(surface.minimum.x, point.x);
            surface.minimum.y = std::min(surface.minimum.y, point.y);
            surface.minimum.z = std::min(surface.minimum.z, point.z);
            surface.maximum.x = std::max(surface.maximum.x, point.x);
            surface.maximum.y = std::max(surface.maximum.y, point.y);
            surface.maximum.z = std::max(surface.maximum.z, point.z);
        }
        const Vec3 extent = subtract(surface.maximum, surface.minimum);
        m_tolerance = std::max(m_tolerance, std::sqrt(dot(extent, extent)) * 1e-8);
        surface.triangles.reserve(part.triangles.size());
        for (const auto &indices : part.triangles)
        {
            surface.triangles.push_back({part.vertices.at(indices[0]), part.vertices.at(indices[1]),
                                        part.vertices.at(indices[2])});
        }
        m_surfaces.push_back(std::move(surface));
    }
}

bool ModelLabelVisibility::isVisible(
    const ModelLabelAnchor &anchor, const Vec3 &towardEye, double eyeDistance) const
{
    const double length = std::sqrt(dot(towardEye, towardEye));
    if (!std::isfinite(length) || length < 1e-12 || eyeDistance <= m_tolerance)
    {
        return false;
    }
    const Vec3 direction{towardEye.x / length, towardEye.y / length, towardEye.z / length};
    if (dot(anchor.normal, direction) <= 0.02)
    {
        return false; // 背面及几乎沿板面观察的标签不显示。
    }
    for (const auto &surface : m_surfaces)
    {
        if (!hitsBox(anchor.position, direction, surface.minimum, surface.maximum,
                     m_tolerance, eyeDistance - m_tolerance))
        {
            continue;
        }
        for (const auto &triangle : surface.triangles)
        {
            if (hitsTriangle(anchor.position, direction, triangle, m_tolerance, eyeDistance - m_tolerance))
            {
                return false;
            }
        }
    }
    return true;
}
}
