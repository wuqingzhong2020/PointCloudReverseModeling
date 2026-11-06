/*
 * 文件描述：管理起点、终点和下一组取点；由实际坐标差计算三维距离和轴向分量，拒绝非有限值。
 */
#include <cmath>

#include "workbench/PointMeasurement.h"

namespace reverse::workbench
{
namespace
{
bool finite(const Vec3 &point)
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}
}

bool PointMeasurement::pick(const Vec3 &point, bool snapped)
{
    if (!finite(point))
    {
        return false;
    }
    if (!m_first || m_second)
    {
        m_first = point;
        m_second.reset();
        m_firstSnapped = snapped;
        m_secondSnapped = false;
    }
    else
    {
        if (!result(point))
        {
            return false;
        }
        m_second = point;
        m_secondSnapped = snapped;
    }
    return true;
}

void PointMeasurement::clear()
{
    m_first.reset();
    m_second.reset();
    m_firstSnapped = false;
    m_secondSnapped = false;
}

std::optional<PointMeasurementResult> PointMeasurement::result(std::optional<Vec3> preview) const
{
    const auto end = m_second ? m_second : preview;
    if (!m_first || !end || !finite(*end))
    {
        return {};
    }
    const Vec3 difference{std::abs(end->x - m_first->x), std::abs(end->y - m_first->y),
                          std::abs(end->z - m_first->z)};
    const double distance = std::hypot(difference.x, difference.y, difference.z);
    if (!std::isfinite(distance))
    {
        return {};
    }
    return PointMeasurementResult{distance, difference,
        {*m_first, {end->x, m_first->y, m_first->z}, {end->x, end->y, m_first->z}, *end}};
}
}
