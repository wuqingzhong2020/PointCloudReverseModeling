/*
 * 文件描述：
 * 为各板件创建独立的灰色哑光 AIS 网格及带底色的板厚标签，配合真实拓扑轮廓区分板件。
 * 标签在 OCCT 场景中绘制，按屏幕矩形避让并用引线关联部件，不覆盖原生视口的 QWidget 输入层。
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QFont>
#include <QFontMetrics>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QString>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Line.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_Triangulation.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_Camera.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <Quantity_Color.hxx>
#include <V3d_View.hxx>
#include <gp_Vec.hxx>

#include "workbench/OccModelPresentation.h"
#include "workbench/ModelLabelVisibility.h"
#include "workbench/OccPartMesh.h"
#include "workbench/OccPicking.h"
#include "workbench/ThicknessLabel.h"

namespace reverse::workbench
{
namespace
{
struct PartPresentation
{
    std::string id;
    Handle(OccPartMesh) mesh;
    Handle(AIS_TextLabel) label;
    Handle(AIS_Line) leader;
    gp_Pnt anchor;
    std::array<ModelLabelAnchor, 2> faces;
    gp_Pnt labelPosition;
    QString text;
    std::optional<double> thicknessMm;
    double fontHeight{};
    bool selected{};
};

Handle(OccPartMesh) makeMesh(const ModelDisplayPart &part)
{
    const auto limit = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (part.vertices.size() > limit || part.triangles.size() > limit / 3)
    {
        throw std::runtime_error("部件显示网格超过 OCCT 数组容量。");
    }
    Handle(Poly_Triangulation) mesh = new Poly_Triangulation(
        static_cast<int>(part.vertices.size()), static_cast<int>(part.triangles.size()), false);
    for (std::size_t index = 0; index < part.vertices.size(); ++index)
    {
        const auto &point = part.vertices[index];
        mesh->SetNode(static_cast<int>(index + 1), gp_Pnt(point.x, point.y, point.z));
    }
    for (std::size_t index = 0; index < part.triangles.size(); ++index)
    {
        const auto &triangle = part.triangles[index];
        mesh->SetTriangle(static_cast<int>(index + 1), Poly_Triangle(
            static_cast<int>(triangle[0] + 1), static_cast<int>(triangle[1] + 1), static_cast<int>(triangle[2] + 1)));
    }
    mesh->ComputeNormals();
    Handle(OccPartMesh) result = new OccPartMesh(mesh, part);
    // 模式 0 使用灰色哑光表面，关闭镜面高光，轮廓由 OccPartMesh 的真实拓扑边绘制。
    result->SetDisplayMode(0);
    Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
    material.SetSpecularColor(Quantity_NOC_BLACK);
    // AIS_Triangulation 的 Compute 直接读取 ShadingAspect，继承的 SetColor 不更新它。
    Handle(Prs3d_ShadingAspect) shading = new Prs3d_ShadingAspect();
    shading->SetMaterial(material);
    shading->SetColor(Quantity_Color(part.colorSrgb[0], part.colorSrgb[1], part.colorSrgb[2], Quantity_TOC_sRGB));
    shading->Aspect()->SetPolygonOffsets(Aspect_POM_Fill, 1.0f, 1.0f);
    result->Attributes()->SetShadingAspect(shading);
    result->SetTransparency(0.0);
    return result;
}

void makeLabel(PartPresentation &presentation, const std::optional<double> &thickness)
{
    presentation.text = thicknessLabelText(thickness);
    presentation.thicknessMm = thickness;
    presentation.label = new AIS_TextLabel();
    const auto utf8 = presentation.text.toUtf8();
    presentation.label->SetText(TCollection_ExtendedString(utf8.constData(), true));
    presentation.label->SetFont("Microsoft YaHei");
    presentation.label->SetFontAspect(Font_FontAspect_Bold);
    presentation.label->SetZoomable(false);
    presentation.label->SetHJustification(Graphic3d_HTA_CENTER);
    presentation.label->SetVJustification(Graphic3d_VTA_CENTER);
    presentation.label->SetOwnAnchorPoint(true);
    presentation.label->SetDisplayType(Aspect_TODT_SUBTITLE);
    presentation.label->SetColor(Quantity_Color(0.78, 0.25, 0.15, Quantity_TOC_sRGB));
    presentation.label->SetColorSubTitle(Quantity_Color(1.0, 0.97, 0.92, Quantity_TOC_sRGB));
    presentation.label->SetZLayer(Graphic3d_ZLayerId_Topmost);
    presentation.label->SetInfiniteState(true); // 标注不参与 FitAll 的几何包围盒。
    presentation.leader = new AIS_Line(new Geom_CartesianPoint(presentation.anchor),
                                      new Geom_CartesianPoint(presentation.anchor.Translated(gp_Vec(1, 0, 0))));
    presentation.leader->SetColor(Quantity_Color(0.72, 0.79, 0.84, Quantity_TOC_sRGB));
    presentation.leader->SetWidth(1.0);
    presentation.leader->SetZLayer(Graphic3d_ZLayerId_Topmost);
    presentation.leader->SetInfiniteState(true);
}

QRect placeLabel(const QPoint &anchor, const QSize &size, const QRect &viewport,
                 const QRegion &occupied, double pixelRatio)
{
    for (int ring = 0; ring < 6; ++ring)
    {
        const int step = qRound(28 * pixelRatio * ring);
        const std::array offsets{QPoint(0, -step), QPoint(0, step), QPoint(-step, -step), QPoint(step, step),
                                 QPoint(-step, step), QPoint(step, -step), QPoint(-step, 0), QPoint(step, 0)};
        for (const auto &offset : offsets)
        {
            QRect candidate(QPoint(), size);
            candidate.moveCenter(anchor + offset);
            if (viewport.contains(candidate) && !occupied.intersects(candidate))
            {
                return candidate;
            }
        }
    }
    return {}; // 密集标注暂时避让，放大视图后重新显示。
}

gp_Pnt displacedAnchor(const Handle(V3d_View) &view, const gp_Pnt &anchor,
                       const QPoint &anchorPixel, const QPoint &labelPixel)
{
    double x0, y0, z0, x1, y1, z1;
    view->Convert(anchorPixel.x(), anchorPixel.y(), x0, y0, z0);
    view->Convert(labelPixel.x(), labelPixel.y(), x1, y1, z1);
    return anchor.Translated(gp_Vec(x1 - x0, y1 - y0, z1 - z0));
}
}

struct OccModelPresentation::State
{
    Handle(AIS_InteractiveContext) context;
    Handle(V3d_View) view;
    std::vector<PartPresentation> parts;
    std::vector<ThicknessLabelTarget> labelTargets;
    std::unique_ptr<ModelLabelVisibility> visibility;
    bool modelVisible{true};
    bool wireframe{};
    bool labelsVisible{true};
    bool selectable{};
    bool transformPreview{};
    ModelPickMode mode{ModelPickMode::Body};

    bool updateAnchor(PartPresentation &part) const
    {
        const auto &camera = view->Camera();
        const auto &eye = camera->Eye();
        const auto &direction = camera->Direction();
        for (const auto &face : part.faces)
        {
            const Vec3 toEye = camera->IsOrthographic() ? Vec3{-direction.X(), -direction.Y(), -direction.Z()}
                : Vec3{eye.X() - face.position.x, eye.Y() - face.position.y, eye.Z() - face.position.z};
            const double distance = camera->IsOrthographic() ? std::numeric_limits<double>::infinity()
                : std::hypot(toEye.x, toEye.y, toEye.z);
            if (visibility->isVisible(face, toEye, distance))
            {
                part.anchor = gp_Pnt(face.position.x, face.position.y, face.position.z);
                return true;
            }
        }
        return false;
    }

    void clear()
    {
        for (const auto &part : parts)
        {
            context->Remove(part.mesh, false);
            if (!part.label.IsNull())
            {
                context->Remove(part.label, false);
                context->Remove(part.leader, false);
            }
        }
        parts.clear();
        labelTargets.clear();
    }

    void show(const Handle(AIS_InteractiveObject) &object, bool visible)
    {
        if (visible && !context->IsDisplayed(object))
        {
            context->Display(object, 0, -1, false);
        }
        else if (!visible && context->IsDisplayed(object))
        {
            context->Erase(object, false);
        }
    }
};

OccModelPresentation::OccModelPresentation(
    const Handle(AIS_InteractiveContext) &context, const Handle(V3d_View) &view)
    : m_state(std::make_unique<State>())
{
    m_state->context = context;
    m_state->view = view;
}

OccModelPresentation::~OccModelPresentation() = default;

void OccModelPresentation::rebuild(const std::vector<ModelDisplayPart> &parts)
{
    m_state->clear();
    m_state->visibility = std::make_unique<ModelLabelVisibility>(parts);
    for (const auto &part : parts)
    {
        PartPresentation presentation;
        presentation.id = part.id;
        presentation.mesh = makeMesh(part);
        presentation.mesh->setWireframe(m_state->wireframe);
        presentation.faces = part.labelFaces;
        const auto &anchor = part.labelFaces.front().position;
        presentation.anchor = gp_Pnt(anchor.x, anchor.y, anchor.z);
        if (part.thicknessMm || part.thicknessUnresolved)
        {
            makeLabel(presentation, part.thicknessMm);
        }
        m_state->parts.push_back(std::move(presentation));
    }
    setVisible(m_state->modelVisible, m_state->labelsVisible);
}

void OccModelPresentation::setTransformPreview(bool active)
{
    m_state->labelTargets.clear();
    m_state->transformPreview = active;
    setVisible(m_state->modelVisible,m_state->labelsVisible);
}
std::vector<Handle(AIS_InteractiveObject)> OccModelPresentation::objectsForParts(const std::vector<std::string> &ids) const
{
    std::vector<Handle(AIS_InteractiveObject)> objects;
    for (const auto &part : m_state->parts)
        if (std::find(ids.begin(),ids.end(),part.id) != ids.end()) objects.push_back(part.mesh);
    return objects;
}
void OccModelPresentation::setWireframe(bool wireframe)
{
    if (m_state->wireframe == wireframe) return;
    m_state->wireframe = wireframe;
    m_state->context->ClearDetected(false);
    for (const auto &part : m_state->parts)
    {
        part.mesh->setWireframe(wireframe);
        m_state->context->Redisplay(part.mesh, false);
    }
    m_state->context->HilightSelected(false);
}

void OccModelPresentation::setVisible(bool modelVisible, bool labelsVisible)
{
    m_state->labelTargets.clear();
    m_state->modelVisible = modelVisible;
    m_state->labelsVisible = labelsVisible;
    for (const auto &part : m_state->parts)
    {
        m_state->show(part.mesh, modelVisible);
        part.mesh->setSelectable(m_state->context, modelVisible && m_state->selectable && !m_state->transformPreview, m_state->mode);
        if (!part.label.IsNull() && (!modelVisible || !labelsVisible || m_state->transformPreview))
        {
            m_state->show(part.label, false);
            m_state->show(part.leader, false);
        }
    }
}

void OccModelPresentation::setSelectable(bool enabled, ModelPickMode mode)
{
    m_state->selectable = enabled;
    m_state->mode = mode;
    setVisible(m_state->modelVisible, m_state->labelsVisible);
    m_state->context->ClearDetected(false);
}

void OccModelPresentation::selectOwner(const Handle(SelectMgr_EntityOwner) &owner)
{
    const auto selection = detectedSelection(owner);
    selectObjects(selection.partId.empty() ? std::vector<ModelSelection>{} : std::vector<ModelSelection>{selection});
}

ModelSelection OccModelPresentation::detectedSelection(const Handle(SelectMgr_EntityOwner) &owner) const
{
    const auto *object = dynamic_cast<const OccMeshOwner *>(owner.get());
    return {detectedPartId(owner), object ? object->kind : ModelPickMode::Body, object ? object->index : 0};
}

bool OccModelPresentation::isSurfaceOwner(const Handle(SelectMgr_EntityOwner) &owner) const
{
    const auto selection = detectedSelection(owner);
    return !selection.partId.empty() && selection.kind == ModelPickMode::Body;
}

void OccModelPresentation::selectPart(const std::string &id)
{
    selectParts(id.empty() ? std::vector<std::string>{} : std::vector<std::string>{id});
}

void OccModelPresentation::selectParts(const std::vector<std::string> &ids)
{
    std::vector<ModelSelection> selections;
    for (const auto &id : ids) selections.push_back({id, ModelPickMode::Body, 0});
    selectObjects(selections);
}

void OccModelPresentation::selectObjects(const std::vector<ModelSelection> &selections)
{
    for (auto &part : m_state->parts)
    {
        const bool selected = std::any_of(selections.begin(), selections.end(),
            [&](const auto &selection) { return selection.partId == part.id; });
        if (part.selected == selected) continue;
        part.selected = selected;
        if (!part.label.IsNull())
        {
            part.label->SetColor(selected ? Quantity_Color(Quantity_NOC_WHITE)
                : Quantity_Color(0.78, 0.25, 0.15, Quantity_TOC_sRGB));
            part.label->SetColorSubTitle(selected ? Quantity_Color(1.0, 0.49, 0.0, Quantity_TOC_sRGB)
                : Quantity_Color(1.0, 0.97, 0.92, Quantity_TOC_sRGB));
            m_state->context->Redisplay(part.label, false);
        }
    }
    std::vector<Handle(SelectMgr_EntityOwner)> owners;
    if (!m_state->modelVisible || selections.empty())
    {
        setSelectedMeshOwners(m_state->context, owners);
        return;
    }
    for (const auto &selection : selections)
    {
        for (const auto &part : m_state->parts)
        {
            if (part.id != selection.partId) continue;
            m_state->context->Load(part.mesh, static_cast<int>(selection.kind));
            const auto owner = part.mesh->selectionOwner(selection.kind, selection.index);
            if (!owner.IsNull()) owners.push_back(owner);
            break;
        }
    }
    setSelectedMeshOwners(m_state->context, owners);
}

std::string OccModelPresentation::detectedPartId(const Handle(SelectMgr_EntityOwner) &owner) const
{
    if (m_state->modelVisible && m_state->selectable)
    {
        for (const auto &part : m_state->parts)
        {
            if (isDetectedInteractive(owner, part.mesh)) return part.id;
        }
    }
    return {};
}

std::optional<ThicknessLabelTarget> OccModelPresentation::labelAt(const QPointF &position, double pixelRatio) const
{
    return hitThicknessLabel(m_state->labelTargets, position, pixelRatio);
}

void OccModelPresentation::updateLabels(int pixelWidth, int pixelHeight, double pixelRatio)
{
    m_state->labelTargets.clear();
    if (m_state->transformPreview || !m_state->modelVisible || !m_state->labelsVisible || pixelWidth < 1 || pixelHeight < 1)
    {
        return;
    }
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(qRound(14 * pixelRatio));
    font.setBold(true);
    const QFontMetrics metrics(font);
    const int margin = qRound(8 * pixelRatio);
    const QRect viewport = QRect(0, 0, pixelWidth, pixelHeight).adjusted(margin, margin, -margin, -margin);
    QRegion occupied;
    for (auto &part : m_state->parts)
    {
        if (part.label.IsNull())
        {
            continue;
        }
        if (!m_state->updateAnchor(part))
        {
            m_state->show(part.label, false);
            m_state->show(part.leader, false);
            continue;
        }
        int x, y;
        m_state->view->Convert(part.anchor.X(), part.anchor.Y(), part.anchor.Z(), x, y);
        const QPoint anchorPixel(x, y);
        const QSize size(metrics.horizontalAdvance(part.text) + margin * 2, metrics.height() + margin);
        const QRect box = viewport.contains(anchorPixel)
            ? placeLabel(anchorPixel, size, viewport, occupied, pixelRatio) : QRect();
        if (box.isEmpty())
        {
            m_state->show(part.label, false);
            m_state->show(part.leader, false);
            continue;
        }
        if (part.thicknessMm)
        {
            m_state->labelTargets.push_back({part.id, *part.thicknessMm, box});
        }
        occupied += box.adjusted(-margin / 2, -margin / 2, margin / 2, margin / 2);
        const gp_Pnt position = displacedAnchor(m_state->view, part.anchor, anchorPixel, box.center());
        const double fontHeight = 14 * pixelRatio;
        if (part.labelPosition.SquareDistance(position) > 1e-14 || part.fontHeight != fontHeight)
        {
            part.label->SetPosition(position);
            part.label->SetHeight(fontHeight);
            m_state->context->Redisplay(part.label, false);
            if (position.SquareDistance(part.anchor) > 1e-14)
            {
                part.leader->SetPoints(new Geom_CartesianPoint(part.anchor), new Geom_CartesianPoint(position));
                m_state->context->Redisplay(part.leader, false);
            }
            part.labelPosition = position;
            part.fontHeight = fontHeight;
        }
        m_state->show(part.leader, (box.center() - anchorPixel).manhattanLength() > margin);
        m_state->show(part.label, true);
    }
}
}
