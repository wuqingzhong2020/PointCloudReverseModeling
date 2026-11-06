/*
 * 文件描述：用 OCCT 选择器的射线查询检查关键点遮挡，排除挡在顶点前方的模型面。
 */
#pragma once

#include <algorithm>
#include <functional>

#include <AIS_InteractiveContext.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <V3d_View.hxx>
#include <gp_Ax1.hxx>
#include <gp_Vec.hxx>

namespace reverse::workbench
{
// 会重用选择器；调用者必须先保存普通拾取结果。
inline bool isMeasurementPointVisible(const gp_Pnt &point, const Handle(AIS_InteractiveContext) &context,
    const Handle(V3d_View) &view, const std::function<bool(const Handle(SelectMgr_EntityOwner) &)> &isSurface)
{
    const auto camera = view->Camera();
    gp_Pnt origin = camera->Eye();
    gp_Dir direction = camera->Direction();
    double distance = gp_Vec(origin, point).Dot(gp_Vec(direction));
    if (camera->IsOrthographic())
    {
        origin = point.Translated(-gp_Vec(direction) * distance);
    }
    else
    {
        distance = origin.Distance(point);
        if (distance <= 1e-9)
        {
            return false;
        }
        direction = gp_Dir(gp_Vec(origin, point));
    }
    if (distance <= 0)
    {
        return false;
    }
    const auto selector = context->MainSelector();
    selector->Pick(gp_Ax1(origin, direction), view);
    const double tolerance = std::max(1e-6, distance * 1e-8);
    for (int rank = 1; rank <= selector->NbPicked(); ++rank)
    {
        if (isSurface(selector->Picked(rank)))
        {
            const double depth = gp_Vec(origin, selector->PickedPoint(rank)).Dot(gp_Vec(direction));
            if (depth >= 0 && depth < distance - tolerance)
            {
                return false;
            }
        }
    }
    return true;
}
}
