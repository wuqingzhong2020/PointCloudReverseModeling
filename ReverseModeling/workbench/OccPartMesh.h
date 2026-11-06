/*
 * 文件描述：按真实拓扑顶点、边、面或整个部件建立选择实体及局部高亮。
 */
#pragma once

#include <map>
#include <vector>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Triangulation.hxx>
#include <SelectMgr_EntityOwner.hxx>

#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
// 批量重建高亮，避免取消一个子对象时清掉同一部件其他已选对象的高亮。
void setSelectedMeshOwners(const Handle(AIS_InteractiveContext) & context,
                           const std::vector<Handle(SelectMgr_EntityOwner)> &owners);

class OccMeshOwner final : public SelectMgr_EntityOwner
{
  public:
    OccMeshOwner(const Handle(SelectMgr_SelectableObject) & object, ModelPickMode kind, std::size_t index,
                 int priority);
    ModelPickMode kind;
    std::size_t index;
};

class OccPartMesh final : public AIS_Triangulation
{
  public:
    explicit OccPartMesh(const Handle(Poly_Triangulation) & mesh, const ModelDisplayPart &part = {});
    void setSelectable(const Handle(AIS_InteractiveContext) & context, bool enabled,
                       ModelPickMode mode = ModelPickMode::Body);
    Handle(SelectMgr_EntityOwner) selectionOwner(ModelPickMode mode, std::size_t index) const;
    void HilightOwnerWithColor(const Handle(PrsMgr_PresentationManager) & manager, const Handle(Prs3d_Drawer) & style,
                               const Handle(SelectMgr_EntityOwner) & owner) override;
    void HilightSelected(const Handle(PrsMgr_PresentationManager) & manager,
                         const NCollection_Sequence<Handle(SelectMgr_EntityOwner)> &owners) override;
    void ClearSelected() override;

  protected:
    void Compute(const Handle(PrsMgr_PresentationManager) & manager, const Handle(Prs3d_Presentation) & presentation,
                 int mode) override;
    void ComputeSelection(const Handle(SelectMgr_Selection) & selection, int mode) override;

  private:
    std::vector<Vec3> m_points;
    std::vector<std::vector<Vec3>> m_edges;
    std::map<std::size_t, Handle(Poly_Triangulation)> m_faces;
    std::map<std::size_t, std::vector<std::array<Vec3, 2>>> m_faceBoundaries;
    bool m_selectable{};
    ModelPickMode m_mode{ModelPickMode::Body};
    void drawHighlight(const Handle(Prs3d_Presentation) & presentation, const Handle(SelectMgr_EntityOwner) & owner,
                       bool selected);
};
} // namespace reverse::workbench
