/* 文件描述：沿项目 X/Y/Z 轴移动，旋转只启用当前选定轴；拖动和数值输入共用刚体预览。 */
#include <algorithm>
#include <cmath>
#include <numbers>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Manipulator.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Triangulation.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <TopoDS_Compound.hxx>
#include <V3d_View.hxx>

#include "workbench/OccPicking.h"
#include "workbench/OccTransformPreview.h"

namespace reverse::workbench
{
struct OccTransformPreview::State
{
    Handle(AIS_InteractiveContext) context;
    Handle(V3d_View) view;
    Handle(AIS_Manipulator) handle;
    std::vector<Handle(AIS_InteractiveObject)> targets, ghosts;
    Vec3 origin;
    ModelTransform request, start;
    bool dragging{};
};
OccTransformPreview::OccTransformPreview(const Handle(AIS_InteractiveContext) & context, const Handle(V3d_View) & view,
                                         const std::vector<Handle(AIS_InteractiveObject)> &targets,
                                         const std::vector<ModelDisplayPart> &parts,
                                         const std::vector<std::string> &ids, Vec3 origin,
                                         const ModelTransform &request, double pixelRatio)
    : m_state(std::make_unique<State>())
{
    auto &s = *m_state;
    s.context = context;
    s.view = view;
    s.targets = targets;
    s.origin = origin;
    s.request = request;
    context->ClearSelected(false);
    for (const auto &part : parts)
    {
        if (std::find(ids.begin(), ids.end(), part.id) == ids.end())
            continue;
        Handle(Poly_Triangulation) mesh = new Poly_Triangulation(static_cast<int>(part.vertices.size()),
                                                                 static_cast<int>(part.triangles.size()), false);
        for (std::size_t i = 0; i < part.vertices.size(); ++i)
        {
            const auto &p = part.vertices[i];
            mesh->SetNode(static_cast<int>(i + 1), gp_Pnt(p.x, p.y, p.z));
        }
        for (std::size_t i = 0; i < part.triangles.size(); ++i)
        {
            const auto &t = part.triangles[i];
            mesh->SetTriangle(static_cast<int>(i + 1), Poly_Triangle(t[0] + 1, t[1] + 1, t[2] + 1));
        }
        mesh->ComputeNormals();
        Handle(AIS_Triangulation) ghost = new AIS_Triangulation(mesh);
        Handle(Prs3d_ShadingAspect) shading = new Prs3d_ShadingAspect();
        shading->SetColor(Quantity_Color(0.3, 0.7, 0.8, Quantity_TOC_sRGB));
        shading->SetTransparency(0.82);
        ghost->Attributes()->SetShadingAspect(shading);
        ghost->SetTransparency(0.82);
        ghost->SetInfiniteState(true);
        context->Display(ghost, 0, -1, false);
        s.ghosts.push_back(ghost);
        TopoDS_Compound edges;
        BRep_Builder builder;
        builder.MakeCompound(edges);
        for (const auto &points : part.edges)
        {
            if (points.size() < 2)
                continue;
            BRepBuilderAPI_MakePolygon polygon;
            for (const auto &p : points)
                polygon.Add(gp_Pnt(p.x, p.y, p.z));
            if (polygon.IsDone())
                builder.Add(edges, polygon.Wire());
        }
        Handle(AIS_Shape) wire = new AIS_Shape(edges);
        wire->SetColor(Quantity_Color(0.1, 0.7, 0.95, Quantity_TOC_sRGB));
        wire->SetWidth(1.5);
        wire->SetInfiniteState(true);
        context->Display(wire, 0, -1, false);
        s.ghosts.push_back(wire);
    }
    s.handle = new AIS_Manipulator();
    s.handle->SetPart(AIS_MM_Scaling, false);
    s.handle->SetPart(AIS_MM_TranslationPlane, false);
    s.handle->SetModeActivationOnDetection(true);
    s.handle->SetZoomPersistence(true);
    s.handle->SetSize(static_cast<float>(85 * pixelRatio));
    s.handle->SetInfiniteState(true);
    s.handle->SetZLayer(Graphic3d_ZLayerId_Topmost);
    s.handle->Attach(targets.front(),
                     AIS_Manipulator::OptionsForAttach().SetAdjustPosition(false).SetEnableModes(false));
    s.handle->SetTransformBehavior(AIS_Manipulator::BehaviorOnTransform().SetFollowRotation(false));
    apply(request);
}
OccTransformPreview::~OccTransformPreview()
{
    auto &s = *m_state;
    if (!s.handle.IsNull())
    {
        s.handle->StopTransform(true);
        s.handle->Detach();
    }
    for (const auto &target : s.targets)
        s.context->SetLocation(target, TopLoc_Location());
    for (const auto &ghost : s.ghosts)
        s.context->Remove(ghost, false);
}
void OccTransformPreview::apply(const ModelTransform &request)
{
    auto &s = *m_state;
    const auto delta = modelTransformFrame(request);
    auto t = transformPoint(delta, s.origin);
    t = {t.x - s.origin.x, t.y - s.origin.y, t.z - s.origin.z};
    gp_Trsf matrix;
    matrix.SetValues(delta.xAxis.x, delta.yAxis.x, delta.zAxis.x, t.x, delta.xAxis.y, delta.yAxis.y, delta.zAxis.y, t.y,
                     delta.xAxis.z, delta.yAxis.z, delta.zAxis.z, t.z);
    for (const auto &target : s.targets)
        s.context->SetLocation(target, TopLoc_Location(matrix));
    const bool reconfigure = s.request.kind != request.kind || s.request.axis != request.axis;
    s.request = request;
    if (s.dragging)
        return;
    if (reconfigure)
        s.handle->DeactivateCurrentMode();
    const bool moving = request.kind == ModelTransformKind::Move;
    s.context->Deactivate(s.handle);
    s.handle->SetPart(AIS_MM_Translation, moving);
    for (int i = 0; i < 3; ++i)
        s.handle->SetPart(i, AIS_MM_Rotation, !moving && i == static_cast<int>(request.axis));
    auto center = request.centerMm;
    if (moving)
        center = transformPoint(delta, center);
    s.handle->SetPosition(gp_Ax2(gp_Pnt(center.x - s.origin.x, center.y - s.origin.y, center.z - s.origin.z),
                                 gp_Dir(0, 0, 1), gp_Dir(1, 0, 0)));
    s.context->Redisplay(s.handle, false);
    s.handle->EnableMode(moving ? AIS_MM_Translation : AIS_MM_Rotation);
}
bool OccTransformPreview::startDrag(int x, int y)
{
    auto &s = *m_state;
    if (!isDetectedInteractive(s.context->DetectedOwner(), s.handle) || !s.handle->HasActiveMode())
        return false;
    s.start = s.request;
    s.handle->StartTransform(x, y, s.view);
    return s.dragging = s.handle->HasActiveTransformation();
}
std::optional<ModelTransform> OccTransformPreview::drag(int x, int y)
{
    auto &s = *m_state;
    if (!s.dragging)
        return {};
    gp_Trsf delta;
    if (!s.handle->ObjectTransformation(x, y, s.view, delta))
        return {};
    auto result = s.start;
    if (result.kind == ModelTransformKind::Move)
    {
        const auto t = delta.TranslationPart();
        result.offsetMm = {result.offsetMm.x + t.X(), result.offsetMm.y + t.Y(), result.offsetMm.z + t.Z()};
    }
    else
    {
        gp_XYZ axis;
        double angle{};
        if (delta.GetRotation(axis, angle))
        {
            const double sign = axis.Coord(static_cast<int>(result.axis) + 1) < 0 ? -1 : 1;
            result.angleDegrees =
                std::clamp(result.angleDegrees + sign * angle * 180 / std::numbers::pi, -180.0, 180.0);
        }
    }
    apply(result);
    return result;
}
void OccTransformPreview::finishDrag()
{
    auto &s = *m_state;
    if (!s.dragging)
        return;
    s.handle->StopTransform(true);
    s.dragging = false;
    apply(s.request);
}
bool OccTransformPreview::dragging() const
{
    return m_state->dragging;
}
} // namespace reverse::workbench
