/*
 * 文件描述：为部件三角网格提供整件选择实体，按真实三角面拾取，避免用包围盒误选背侧部件。
 */
#pragma once

#include <AIS_Triangulation.hxx>
#include <AIS_InteractiveContext.hxx>
#include <Select3D_SensitiveTriangulation.hxx>
#include <SelectMgr_EntityOwner.hxx>
#include <SelectMgr_Selection.hxx>
#include <TopLoc_Location.hxx>

namespace reverse::workbench
{
class OccPartMesh final : public AIS_Triangulation
{
public:
    explicit OccPartMesh(const Handle(Poly_Triangulation) &mesh) : AIS_Triangulation(mesh) {}

    void setSelectable(const Handle(AIS_InteractiveContext) &context, bool enabled)
    {
        if (enabled)
        {
            // 首次以 -1 模式显示时不进入选择管理器，必须先注册再激活。
            context->Load(this, 0);
            context->Activate(this, 0);
        }
        else
        {
            context->Deactivate(this);
        }
    }

protected:
    void ComputeSelection(const Handle(SelectMgr_Selection) &selection, const int mode) override
    {
        // AIS_Triangulation 自身没有选择实现；模式 0 表示整块板件，不是单个三角面。
        if (mode == 0)
        {
            const Handle(SelectMgr_EntityOwner) owner = new SelectMgr_EntityOwner(this);
            selection->Add(new Select3D_SensitiveTriangulation(owner, GetTriangulation(), TopLoc_Location(), true));
        }
    }
};
}
