/* 文件描述：显示不可拾取、且不参与适合视图计算的基准面预览及选点标记。 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include <Standard_Handle.hxx>

#include "core/ReferencePlane.h"

class AIS_InteractiveContext;
namespace reverse::workbench
{
class OccReferencePlanePresentation
{
public:
    explicit OccReferencePlanePresentation(const Handle(AIS_InteractiveContext) &context);
    ~OccReferencePlanePresentation();
    void update(const std::vector<Vec3> &points, const std::optional<ReferencePlane> &plane,
                const Vec3 &boundsMin, const Vec3 &boundsMax, double offsetMm, double pixelRatio);
    void clear();
private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
