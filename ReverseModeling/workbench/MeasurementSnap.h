/*
 * 文件描述：缓存关键点的屏幕投影，用像素距离和可见性选择测量吸附点，不依赖 Qt/OCCT。
 */
#pragma once

#include <array>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
struct MeasurementSnapView
{
    std::array<double, 16> viewProjection{}; // 行优先，输入为显示局部坐标。
    int width{};
    int height{};
    bool zeroToOneDepth{};
    bool operator==(const MeasurementSnapView &) const = default;
};

class MeasurementSnap
{
public:
    static constexpr double RadiusPixels = 16; // Qt 逻辑像素，调用处乘 DPI 比率。
    void rebuild(const std::vector<ModelDisplayPart> &parts);
    void setView(const MeasurementSnapView &view);
    [[nodiscard]] std::optional<Vec3> find(double x, double y, double radius,
        const std::function<bool(const Vec3 &)> &visible) const;

private:
    struct ProjectedPoint
    {
        Vec3 position;
        double x{};
        double y{};
        double depth{};
    };
    static constexpr double CellSize = 32;
    std::vector<Vec3> m_points;
    std::optional<MeasurementSnapView> m_view;
    std::map<std::pair<int, int>, std::vector<ProjectedPoint>> m_cells;
};
}
