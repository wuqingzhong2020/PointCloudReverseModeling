/*
 * 文件描述：复用显示网格按原始 BRep 面分组，曲线只使用拓扑边，不把细分三角线当作边。
 */
#include <algorithm>

#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ArrayOfPolylines.hxx>
#include <Graphic3d_ArrayOfSegments.hxx>
#include <Graphic3d_ArrayOfTriangles.hxx>
#include <Graphic3d_AspectFillArea3d.hxx>
#include <Graphic3d_AspectLine3d.hxx>
#include <Graphic3d_AspectMarker3d.hxx>
#include <Graphic3d_Group.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_Presentation.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <Select3D_SensitiveCurve.hxx>
#include <Select3D_SensitivePoint.hxx>
#include <Select3D_SensitiveTriangulation.hxx>
#include <SelectMgr_Selection.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <TopLoc_Location.hxx>

#include "workbench/OccPartMesh.h"

namespace reverse::workbench
{
namespace
{
gp_Pnt point(const Vec3 &value)
{
    return {value.x, value.y, value.z};
}
Quantity_Color srgb(const std::array<double, 3> &value)
{
    return {value[0], value[1], value[2], Quantity_TOC_sRGB};
}
void drawEdges(const Handle(Prs3d_Presentation) & presentation, const std::vector<std::vector<Vec3>> &edges,
               const Quantity_Color &color, double width)
{
    for (const auto &edge : edges)
    {
        if (edge.size() < 2)
            continue;
        const Handle(Graphic3d_ArrayOfPolylines) array = new Graphic3d_ArrayOfPolylines(static_cast<int>(edge.size()));
        for (const auto &vertex : edge)
            array->AddVertex(point(vertex));
        const auto group = presentation->NewGroup();
        group->SetPrimitivesAspect(new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, width));
        group->AddPrimitiveArray(array);
    }
}
} // namespace

OccMeshOwner::OccMeshOwner(const Handle(SelectMgr_SelectableObject) & object, ModelPickMode kind, std::size_t index,
                           int priority)
    : SelectMgr_EntityOwner(object, priority), kind(kind), index(index)
{
}

void setSelectedMeshOwners(const Handle(AIS_InteractiveContext) & context,
                           const std::vector<Handle(SelectMgr_EntityOwner)> &owners)
{
    context->ClearSelected(false);
    for (const auto &owner : owners)
        if (!owner.IsNull() && !context->IsSelected(owner))
            context->AddOrRemoveSelected(owner, false);
    context->HilightSelected(false);
}

OccPartMesh::OccPartMesh(const Handle(Poly_Triangulation) & mesh, const ModelDisplayPart &part)
    : AIS_Triangulation(mesh), m_points(part.keyPoints), m_edges(part.edges)
{
    SetAutoHilight(false);
    std::map<std::size_t, std::vector<int>> groups;
    for (std::size_t index = 0; index < part.triangleFaceIndices.size(); ++index)
        groups[part.triangleFaceIndices[index]].push_back(static_cast<int>(index + 1));
    for (const auto &[id, triangles] : groups)
    {
        std::map<std::pair<int, int>, unsigned int> edgeCounts;
        const int count = static_cast<int>(triangles.size());
        Handle(Poly_Triangulation) face = new Poly_Triangulation(count * 3, count, false);
        for (int index = 0; index < count; ++index)
        {
            const auto &triangle = mesh->Triangle(triangles[index]);
            for (int corner = 1; corner <= 3; ++corner)
            {
                const int first = triangle.Value(corner), second = triangle.Value(corner % 3 + 1);
                ++edgeCounts[{std::min(first, second), std::max(first, second)}];
            }
            for (int corner = 1; corner <= 3; ++corner)
                face->SetNode(index * 3 + corner, mesh->Node(triangle.Value(corner)));
            face->SetTriangle(index + 1, Poly_Triangle(index * 3 + 1, index * 3 + 2, index * 3 + 3));
        }
        m_faces.emplace(id, face);
        // 只保留面的边界，排除面内部的三角剖分线。
        auto &boundary = m_faceBoundaries[id];
        for (const auto &[edge, adjacentTriangles] : edgeCounts)
            if (adjacentTriangles == 1)
            {
                const auto &a = mesh->Node(edge.first), &b = mesh->Node(edge.second);
                boundary.push_back({Vec3{a.X(), a.Y(), a.Z()}, Vec3{b.X(), b.Y(), b.Z()}});
            }
    }
}

void OccPartMesh::setWireframe(bool wireframe)
{
    if (m_wireframe == wireframe) return;
    m_wireframe = wireframe;
    SetToUpdate();
}

void OccPartMesh::Compute(const Handle(PrsMgr_PresentationManager) &, const Handle(Prs3d_Presentation) & presentation,
                          int mode)
{
    if (mode != 0)
        return;
    if (m_wireframe)
    {
        // 深色视口上使用实体的浅色；实体模式的深色描边只适合叠在填充面上。
        drawEdges(presentation, m_edges, srgb(ModelColors::surface), 1.0);
        return;
    }
    const auto mesh = GetTriangulation();
    if (mesh.IsNull() || mesh->NbTriangles() == 0)
        return;
    const Handle(Graphic3d_ArrayOfTriangles) array =
        new Graphic3d_ArrayOfTriangles(mesh->NbNodes(), mesh->NbTriangles() * 3, mesh->HasNormals());
    for (int i = 1; i <= mesh->NbNodes(); ++i)
    {
        if (mesh->HasNormals())
            array->AddVertex(mesh->Node(i), mesh->Normal(i));
        else
            array->AddVertex(mesh->Node(i));
    }
    for (int i = 1; i <= mesh->NbTriangles(); ++i)
    {
        const auto &triangle = mesh->Triangle(i);
        array->AddEdges(triangle.Value(1), triangle.Value(2), triangle.Value(3));
    }
    const auto group = presentation->NewGroup();
    group->SetPrimitivesAspect(Attributes()->ShadingAspect()->Aspect());
    group->AddPrimitiveArray(array);
    // 轮廓参与正常深度遮挡，移动/旋转时与同一呈现中的实体一起变换。
    drawEdges(presentation, m_edges, srgb(ModelColors::outline), 1.0);
}

void OccPartMesh::setSelectable(const Handle(AIS_InteractiveContext) & context, bool enabled, ModelPickMode mode)
{
    if (m_selectable == enabled && m_mode == mode)
        return;
    context->Deactivate(this);
    m_selectable = enabled;
    m_mode = mode;
    if (enabled)
    {
        // 整件选择体同时用于遮挡查询。非体模式由视口过滤，不能作为子对象的回退命中。
        context->Load(this, 0);
        context->Activate(this, 0);
        if (mode != ModelPickMode::Body)
            context->Activate(this, static_cast<int>(mode));
    }
}

void OccPartMesh::ComputeSelection(const Handle(SelectMgr_Selection) & selection, int mode)
{
    const auto kind = static_cast<ModelPickMode>(mode);
    if (kind == ModelPickMode::Body)
    {
        selection->Add(new Select3D_SensitiveTriangulation(new OccMeshOwner(this, kind, 0, 0), GetTriangulation(),
                                                           TopLoc_Location(), true));
    }
    else if (kind == ModelPickMode::Vertex)
    {
        for (std::size_t index = 0; index < m_points.size(); ++index)
            selection->Add(new Select3D_SensitivePoint(new OccMeshOwner(this, kind, index, 9), point(m_points[index])));
    }
    else if (kind == ModelPickMode::Edge)
    {
        for (std::size_t index = 0; index < m_edges.size(); ++index)
        {
            const auto &edge = m_edges[index];
            if (edge.size() < 2)
                continue;
            NCollection_Array1<gp_Pnt> points(1, static_cast<int>(edge.size()));
            for (int i = 1; i <= points.Length(); ++i)
                points.SetValue(i, point(edge[i - 1]));
            selection->Add(new Select3D_SensitiveCurve(new OccMeshOwner(this, kind, index, 7), points));
        }
    }
    else if (kind == ModelPickMode::Face)
    {
        for (const auto &[index, face] : m_faces)
            selection->Add(new Select3D_SensitiveTriangulation(new OccMeshOwner(this, kind, index, 5), face,
                                                               TopLoc_Location(), true));
    }
}

Handle(SelectMgr_EntityOwner) OccPartMesh::selectionOwner(ModelPickMode mode, std::size_t index) const
{
    const auto selection = Selection(static_cast<int>(mode));
    if (selection.IsNull())
        return {};
    for (const auto &entity : selection->Entities())
    {
        const auto owner = entity->BaseSensitive()->OwnerId();
        const auto *object = dynamic_cast<const OccMeshOwner *>(owner.get());
        if (object && object->kind == mode && object->index == index)
            return owner;
    }
    return {};
}

void OccPartMesh::drawHighlight(const Handle(Prs3d_Presentation) & presentation,
                                const Handle(SelectMgr_EntityOwner) & owner, bool selected)
{
    const auto color = srgb(selected ? ModelColors::selectedOutline : ModelColors::hoverOutline);
    const auto *object = dynamic_cast<const OccMeshOwner *>(owner.get());
    const auto kind = object ? object->kind : ModelPickMode::Body;
    const auto index = object ? object->index : 0;
    const auto group = presentation->NewGroup();
    if (kind == ModelPickMode::Vertex && index < m_points.size())
    {
        const Handle(Graphic3d_ArrayOfPoints) array = new Graphic3d_ArrayOfPoints(1);
        array->AddVertex(point(m_points[index]));
        group->SetPrimitivesAspect(new Graphic3d_AspectMarker3d(Aspect_TOM_O_POINT, color, 4.5));
        group->AddPrimitiveArray(array);
    }
    else if (kind == ModelPickMode::Edge && index < m_edges.size())
    {
        const auto &edge = m_edges[index];
        const Handle(Graphic3d_ArrayOfPolylines) array = new Graphic3d_ArrayOfPolylines(static_cast<int>(edge.size()));
        for (const auto &vertex : edge)
            array->AddVertex(point(vertex));
        group->SetPrimitivesAspect(new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 3.0));
        group->AddPrimitiveArray(array);
    }
    else
    {
        // 线框下保持面/体的边界高亮，不重新填充被隐藏的表面。
        if (m_wireframe)
        {
            if (kind == ModelPickMode::Body)
                drawEdges(presentation, m_edges, color, 1.5);
            else if (const auto boundary = m_faceBoundaries.find(index); boundary != m_faceBoundaries.end())
            {
                std::vector<std::vector<Vec3>> edges;
                edges.reserve(boundary->second.size());
                for (const auto &edge : boundary->second) edges.push_back({edge[0], edge[1]});
                drawEdges(presentation, edges, color, 1.5);
            }
            return;
        }
        const auto found = m_faces.find(index);
        const auto mesh = kind == ModelPickMode::Body ? GetTriangulation()
                          : found != m_faces.end()    ? found->second
                                                      : Handle(Poly_Triangulation)();
        if (mesh.IsNull())
            return;
        const Handle(Graphic3d_ArrayOfTriangles) array = new Graphic3d_ArrayOfTriangles(mesh->NbTriangles() * 3);
        for (int i = 1; i <= mesh->NbTriangles(); ++i)
            for (int corner = 1; corner <= 3; ++corner)
                array->AddVertex(mesh->Node(mesh->Triangle(i).Value(corner)));
        const Handle(Graphic3d_AspectFillArea3d) aspect = new Graphic3d_AspectFillArea3d();
        aspect->SetInteriorStyle(Aspect_IS_SOLID);
        const auto fill = srgb(selected ? ModelColors::selectedSurface : ModelColors::hoverSurface);
        aspect->SetInteriorColor(fill);
        aspect->SetBackInteriorColor(fill);
        aspect->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
        aspect->SetPolygonOffsets(Aspect_POM_Fill, 0.5f, 0.5f);
        group->SetPrimitivesAspect(aspect);
        group->AddPrimitiveArray(array);
        if (kind == ModelPickMode::Body)
            drawEdges(presentation, m_edges, color, 1.5);
        else if (const auto boundary = m_faceBoundaries.find(index);
                 boundary != m_faceBoundaries.end() && !boundary->second.empty())
        {
            const Handle(Graphic3d_ArrayOfSegments) lines =
                new Graphic3d_ArrayOfSegments(static_cast<int>(boundary->second.size() * 2));
            for (const auto &edge : boundary->second)
            {
                lines->AddVertex(point(edge[0]));
                lines->AddVertex(point(edge[1]));
            }
            const auto outline = presentation->NewGroup();
            outline->SetPrimitivesAspect(new Graphic3d_AspectLine3d(color, Aspect_TOL_SOLID, 1.5));
            outline->AddPrimitiveArray(lines);
        }
    }
}

void OccPartMesh::HilightOwnerWithColor(const Handle(PrsMgr_PresentationManager) & manager,
                                        const Handle(Prs3d_Drawer) &, const Handle(SelectMgr_EntityOwner) & owner)
{
    const auto presentation = GetHilightPresentation(manager);
    presentation->Clear();
    presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    drawHighlight(presentation, owner, owner->IsSelected());
    if (manager->IsImmediateModeOn())
        manager->AddToImmediateList(presentation);
    else
        presentation->Display();
}

void OccPartMesh::HilightSelected(const Handle(PrsMgr_PresentationManager) & manager,
                                  const NCollection_Sequence<Handle(SelectMgr_EntityOwner)> &owners)
{
    const auto presentation = GetSelectPresentation(manager);
    presentation->Clear();
    presentation->SetZLayer(Graphic3d_ZLayerId_Top);
    for (const auto &owner : owners)
        drawHighlight(presentation, owner, true);
    presentation->Display();
}

void OccPartMesh::ClearSelected()
{
    const auto presentation = GetSelectPresentation({});
    if (!presentation.IsNull())
    {
        presentation->Clear();
        presentation->Erase();
    }
}
} // namespace reverse::workbench
