/*
 * 文件描述：模型或相机变化时建立关键点屏幕网格；鼠标移动仅查询附近单元。
 * 按屏幕距离、深度排序，并逐个检查可见性；不把网格细分点或遮挡背面当作捕获点。
 */
#include <algorithm>
#include <cmath>
#include <set>
#include <tuple>

#include "workbench/MeasurementSnap.h"

namespace reverse::workbench
{
void MeasurementSnap::rebuild(const std::vector<ModelDisplayPart> &parts)
{
    m_points.clear();
    m_cells.clear();
    m_view.reset();
    std::set<std::array<double, 3>> unique;
    for (const auto &part : parts)
    {
        for (const auto &point : part.keyPoints)
        {
            if (std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z) &&
                unique.insert({point.x, point.y, point.z}).second)
            {
                m_points.push_back(point);
            }
        }
    }
}

void MeasurementSnap::setView(const MeasurementSnapView &view)
{
    if (m_view && *m_view == view)
    {
        return;
    }
    m_view = view;
    m_cells.clear();
    if (view.width <= 0 || view.height <= 0)
    {
        return;
    }
    const auto &m = view.viewProjection;
    for (const auto &point : m_points)
    {
        const double w = m[12] * point.x + m[13] * point.y + m[14] * point.z + m[15];
        if (!std::isfinite(w) || w <= 1e-12)
        {
            continue;
        }
        const double x = (m[0] * point.x + m[1] * point.y + m[2] * point.z + m[3]) / w;
        const double y = (m[4] * point.x + m[5] * point.y + m[6] * point.z + m[7]) / w;
        const double z = (m[8] * point.x + m[9] * point.y + m[10] * point.z + m[11]) / w;
        if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || x < -1 || x > 1 ||
            y < -1 || y > 1 || z < (view.zeroToOneDepth ? 0 : -1) || z > 1)
        {
            continue;
        }
        const double pixelX = (x + 1) * 0.5 * view.width;
        const double pixelY = (1 - y) * 0.5 * view.height;
        const auto key = std::pair{static_cast<int>(std::floor(pixelX / CellSize)),
                                  static_cast<int>(std::floor(pixelY / CellSize))};
        m_cells[key].push_back({point, pixelX, pixelY, z});
    }
}

std::optional<Vec3> MeasurementSnap::find(double x, double y, double radius,
    const std::function<bool(const Vec3 &)> &visible) const
{
    if (!m_view || !visible || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(radius) ||
        radius <= 0 || radius > 1024 || x < 0 || y < 0 || x >= m_view->width || y >= m_view->height)
    {
        return {};
    }
    std::vector<std::pair<double, const ProjectedPoint *>> candidates;
    for (int col = static_cast<int>(std::floor((x - radius) / CellSize));
         col <= static_cast<int>(std::floor((x + radius) / CellSize)); ++col)
    {
        for (int row = static_cast<int>(std::floor((y - radius) / CellSize));
             row <= static_cast<int>(std::floor((y + radius) / CellSize)); ++row)
        {
            const auto cell = m_cells.find({col, row});
            if (cell == m_cells.end())
            {
                continue;
            }
            for (const auto &point : cell->second)
            {
                const double distance = (point.x - x) * (point.x - x) + (point.y - y) * (point.y - y);
                if (distance <= radius * radius)
                {
                    candidates.emplace_back(distance, &point);
                }
            }
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const auto &a, const auto &b)
    {
        return std::tie(a.first, a.second->depth) < std::tie(b.first, b.second->depth);
    });
    for (const auto &[distance, point] : candidates)
    {
        if (visible(point->position))
        {
            return point->position;
        }
    }
    return {};
}
}
