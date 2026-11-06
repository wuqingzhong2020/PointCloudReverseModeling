/*
 * 文件描述：绘制两点测量线、RGB 轴向分解和距离卡片；所有标注均不参与选择或适合视图。
 */
#pragma once

#include <memory>
#include <optional>

#include <Standard_Handle.hxx>

#include "workbench/PointMeasurement.h"

class AIS_InteractiveContext;
class V3d_View;

namespace reverse::workbench
{
class OccMeasurementPresentation
{
public:
    OccMeasurementPresentation(const Handle(AIS_InteractiveContext) &context, const Handle(V3d_View) &view);
    ~OccMeasurementPresentation();
    void update(const PointMeasurement &measurement, std::optional<MeasurementPoint> hover,
                int pixelWidth, int pixelHeight, double pixelRatio);
    void clear();

private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
