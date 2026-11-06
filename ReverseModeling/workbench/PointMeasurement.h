/*
 * 文件描述：两点测量的数据与取点状态；使用毫米坐标，不依赖 Qt 或图形上下文。
 */
#pragma once

#include <array>
#include <optional>

#include "core/Types.h"

namespace reverse::workbench
{
struct MeasurementPoint
{
    Vec3 position;
    bool snapped{};
};

struct PointMeasurementResult
{
    double distanceMm{};
    Vec3 componentsMm; // 工程坐标轴上的绝对距离，和相机方向无关。
    std::array<Vec3, 4> componentPath; // 起点 -> 沿 X -> 沿 Y -> 沿 Z 到终点。
};

class PointMeasurement
{
public:
    // 第三次有效取点开始下一组；无效坐标不改变已有结果。
    [[nodiscard]] bool pick(const Vec3 &point, bool snapped = false);
    void clear();
    const std::optional<Vec3> &first() const { return m_first; }
    const std::optional<Vec3> &second() const { return m_second; }
    bool firstSnapped() const { return m_firstSnapped; }
    bool secondSnapped() const { return m_secondSnapped; }
    [[nodiscard]] std::optional<PointMeasurementResult> result(std::optional<Vec3> preview = {}) const;

private:
    std::optional<Vec3> m_first;
    std::optional<Vec3> m_second;
    bool m_firstSnapped{};
    bool m_secondSnapped{};
};
}
