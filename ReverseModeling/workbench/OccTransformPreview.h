/* 文件描述：模型变换的临时 AIS 矩阵、原位置参照和受轴约束的拖动手柄，不修改工程数据。 */
#pragma once
#include <memory>
#include <optional>
#include <vector>

#include <Standard_Handle.hxx>

#include "geometry/ModelTransform.h"
#include "workbench/ModelDisplayData.h"

class AIS_InteractiveContext;
class AIS_InteractiveObject;
class V3d_View;
namespace reverse::workbench
{
class OccTransformPreview
{
  public:
    OccTransformPreview(const Handle(AIS_InteractiveContext) & context, const Handle(V3d_View) & view,
                        const std::vector<Handle(AIS_InteractiveObject)> &targets,
                        const std::vector<ModelDisplayPart> &parts, const std::vector<std::string> &ids, Vec3 origin,
                        const ModelTransform &request, double pixelRatio);
    ~OccTransformPreview();
    void apply(const ModelTransform &request);
    bool startDrag(int x, int y);
    std::optional<ModelTransform> drag(int x, int y);
    void finishDrag();
    bool dragging() const;

  private:
    struct State;
    std::unique_ptr<State> m_state;
};
} // namespace reverse::workbench
