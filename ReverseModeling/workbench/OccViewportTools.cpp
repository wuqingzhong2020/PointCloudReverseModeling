/* 文件描述：管理测量、孔/板件预览、参考面及变换辅助显示，辅助对象不进入真实模型。 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QPushButton>

#include <AIS_Shape.hxx>
#include <AIS_Triangulation.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRep_Builder.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>

#include "geometry/PlateHoles.h"
#include "geometry/ReferencePlane.h"
#include "workbench/ModelFocus.h"
#include "workbench/OccMeasurementPicking.h"
#include "workbench/OccPicking.h"
#include "workbench/OccViewportState.h"
#include "workbench/ReferencePlanePicking.h"

namespace reverse::workbench
{
namespace
{
class HolePreviewShape final : public AIS_Shape
{
  public:
    explicit HolePreviewShape(const TopoDS_Shape &shape) : AIS_Shape(shape) {}

  protected:
    void Compute(const Handle(PrsMgr_PresentationManager) & manager, const Handle(Prs3d_Presentation) & presentation,
                 int mode) override
    {
        // AIS_Shape 的无限对象会强制退化为线框；先生成实体，再排除它的显示包围盒。
        AIS_Shape::Compute(manager, presentation, mode);
        presentation->SetInfiniteState(true);
    }
};
} // namespace

void OccViewport::updateMeasurementPicking()
{
    if (state_->context.IsNull())
    {
        return;
    }
    const bool pointPick =
        state_->measurementEnabled || (state_->referencePickMode == ReferencePlanePickMode::ThreePoints ||
                                       state_->referencePickMode == ReferencePlanePickMode::OrderedPoints);
    const bool referencePick = state_->referencePickMode != ReferencePlanePickMode::None;
    const auto mode = pointPick ? ModelPickMode::Body : referencePick ? ModelPickMode::Face : state_->pickMode;
    state_->pickFilter->mode = mode;
    if (!state_->cloud.IsNull())
    {
        if (pointPick && state_->cloudVisible)
        {
            state_->context->Load(state_->cloud, AIS_PointCloud::SM_Points);
            state_->context->Activate(state_->cloud, AIS_PointCloud::SM_Points);
        }
        else
        {
            state_->context->Deactivate(state_->cloud);
        }
    }
    if (state_->model)
    {
        state_->model->setSelectable(
            !state_->plateCropEnabled && (state_->modelPickingEnabled || pointPick || referencePick), mode);
    }
    state_->context->SetPixelTolerance(state_->measurementEnabled ? qRound(6 * devicePixelRatioF())
                                                                  : qRound(5 * devicePixelRatioF()));
}

std::optional<MeasurementPoint> OccViewport::measurementPoint(const QPoint &pixel) const
{
    const auto &selector = state_->context->MainSelector();
    selector->Pick(pixel.x(), pixel.y(), state_->view);
    double nearest = std::numeric_limits<double>::max();
    std::optional<Vec3> result;
    for (int rank = 1; rank <= selector->NbPicked(); ++rank)
    {
        const auto owner = selector->Picked(rank);
        if (isDetectedInteractive(owner, state_->viewCube))
        {
            return {}; // 导航立方体覆盖区域不穿透取点。
        }
        const bool cloud = state_->cloudVisible && isDetectedInteractive(owner, state_->cloud);
        const bool model = state_->modelVisible && state_->model && state_->model->isSurfaceOwner(owner);
        if (!(cloud || model) || selector->PickedData(rank).Depth >= nearest)
        {
            continue;
        }
        const auto point = selector->PickedPoint(rank);
        if (cloud && state_->sectionPlane && state_->section.enabled)
        {
            const auto e = sectionEquation(*state_->sectionPlane, state_->section);
            if (e[0] * point.X() + e[1] * point.Y() + e[2] * point.Z() + e[3] < -1e-7)
                continue;
        }
        nearest = selector->PickedData(rank).Depth;
        result = Vec3{point.X(), point.Y(), point.Z()};
    }
    if (state_->modelVisible && state_->model)
    {
        const auto camera = state_->view->Camera();
        const auto matrix = camera->ProjectionMatrix() * camera->OrientationMatrix();
        MeasurementSnapView projection;
        for (std::size_t row = 0; row < 4; ++row)
        {
            for (std::size_t col = 0; col < 4; ++col)
            {
                projection.viewProjection[row * 4 + col] = matrix.GetValue(row, col);
            }
        }
        projection.width = qRound(width() * devicePixelRatioF());
        projection.height = qRound(height() * devicePixelRatioF());
        projection.zeroToOneDepth = camera->IsZeroToOneDepth();
        state_->measurementSnap.setView(projection);
        const auto captured = state_->measurementSnap.find(
            pixel.x(), pixel.y(), MeasurementSnap::RadiusPixels * devicePixelRatioF(),
            [this](const Vec3 &point)
            {
                return isMeasurementPointVisible(gp_Pnt(point.x, point.y, point.z), state_->context, state_->view,
                                                 [this](const auto &owner)
                                                 { return state_->model->isSurfaceOwner(owner); });
            });
        if (captured)
        {
            return MeasurementPoint{*captured, true};
        }
    }
    return result ? std::optional<MeasurementPoint>{{*result, false}} : std::nullopt;
}

void OccViewport::setHoleGuide(const std::vector<Vec3> &points)
{
    if (state_->context.IsNull())
        return;
    for (const auto &object : state_->holeGuide)
        state_->context->Remove(object, false);
    state_->holeGuide.clear();
    if (points.size() == 3)
    {
        const auto point = [](Vec3 p) { return gp_Pnt(p.x, p.y, p.z); };
        Handle(AIS_Shape) edge = new AIS_Shape(BRepBuilderAPI_MakeEdge(point(points[0]), point(points[2])).Edge());
        edge->SetColor(Quantity_Color(0.0, 0.48, 1.0, Quantity_TOC_sRGB));
        edge->SetWidth(2);
        edge->SetZLayer(Graphic3d_ZLayerId_Topmost);
        state_->context->Display(edge, 0, -1, false);
        state_->holeGuide.push_back(edge);
        const QStringList names{QStringLiteral("● 1"), QStringLiteral("● 中"), QStringLiteral("● 2")};
        for (int i = 0; i < 3; ++i)
        {
            Handle(AIS_TextLabel) label = new AIS_TextLabel();
            label->SetText(TCollection_ExtendedString(names[i].toUtf8().constData(), true));
            label->SetPosition(point(points[i]));
            label->SetHeight(16);
            label->SetFont("Microsoft YaHei");
            label->SetColor(Quantity_Color(0.0, 0.48, 1.0, Quantity_TOC_sRGB));
            label->SetZLayer(Graphic3d_ZLayerId_Topmost);
            state_->context->Display(label, 0, -1, false);
            state_->holeGuide.push_back(label);
        }
    }
    redraw();
}

void OccViewport::clearPlateEditReference()
{
    if (!state_->context.IsNull())
        for (const auto &object : state_->plateEditReference)
            state_->context->Remove(object, false);
    state_->plateEditReference.clear();
    redraw();
}

void OccViewport::setPlateEditReference(const std::vector<ModelDisplayPart> &original,
                                        const std::vector<std::string> &sources, const std::string &target,
                                        bool removedMaterial)
{
    clearPlateEditReference();
    if (state_->context.IsNull())
        return;
    for (const auto &part : original)
    {
        const bool isTarget = part.id == target;
        if (!isTarget && std::find(sources.begin(), sources.end(), part.id) == sources.end())
            continue;
        if (removedMaterial && !isTarget && !part.triangles.empty())
        {
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
            ghost->SetColor(Quantity_Color(.95, .2, .18, Quantity_TOC_sRGB));
            ghost->SetTransparency(.55);
            ghost->SetPolygonOffsets(1, 2, 2);
            ghost->SetInfiniteState(true);
            state_->context->Display(ghost, 0, -1, false);
            state_->plateEditReference.push_back(ghost);
        }
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
        wire->SetColor(isTarget ? Quantity_Color(1, .55, .08, Quantity_TOC_sRGB)
                                : Quantity_Color(.1, .65, .95, Quantity_TOC_sRGB));
        wire->SetWidth(isTarget ? 2 : 1);
        wire->SetInfiniteState(true);
        state_->context->Display(wire, 0, -1, false);
        state_->plateEditReference.push_back(wire);
    }
    redraw();
}

void OccViewport::clearHolePreview()
{
    state_->holePreviewBase.reset();
    state_->batchHolePreview.clear();
    rebuildHolePreview();
    update();
}

void OccViewport::setHolePreview(const PlateHoleBase &base, const PlateHole &request, const Vec3 &origin)
{
    state_->batchHolePreview.clear();
    state_->holePreviewBase = base;
    state_->holePreviewRequest = request;
    state_->holePreviewOrigin = origin;
    rebuildHolePreview();
    update();
}

void OccViewport::setBatchHolePreview(const std::vector<std::pair<PlateHoleBase, PlateHole>> &items, const Vec3 &origin)
{
    state_->holePreviewBase.reset();
    state_->batchHolePreview = items;
    state_->holePreviewOrigin = origin;
    rebuildHolePreview();
    update();
}
void OccViewport::setPlateCropEnabled(bool enabled, bool preserveRectangle)
{
    if (state_->plateCropEnabled == enabled && preserveRectangle)
        return;
    state_->plateCropEnabled = enabled;
    if (!preserveRectangle)
        clearSelectionRectangle();
    // 与参考实现一致：切割时固定源板，关闭模型拾取，只接收切割框。
    try
    {
        updateMeasurementPicking();
        if (!state_->context.IsNull())
            state_->context->ClearDetected(false);
        updateSelectionCursor(state_->lastMouse);
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}
void OccViewport::rebuildHolePreview()
{
    if (state_->context.IsNull())
        return;
    for (const auto &object : state_->holePreview)
        state_->context->Remove(object, false);
    state_->holePreview.clear();
    if (!state_->holePreviewBase && state_->batchHolePreview.empty())
        return;
    try
    {
        std::vector<TopoDS_Shape> tools;
        if (state_->holePreviewBase)
            tools = detail::plateHoleTools(*state_->holePreviewBase, state_->holePreviewRequest);
        for (const auto &[base, request] : state_->batchHolePreview)
        {
            const auto batch = detail::plateHoleTools(base, request);
            tools.insert(tools.end(), batch.begin(), batch.end());
        }
        const auto &origin = state_->holePreviewOrigin;
        gp_Trsf local;
        local.SetTranslation(gp_Vec(-origin.x, -origin.y, -origin.z));
        const Quantity_Color green(0.18, 0.90, 0.24, Quantity_TOC_sRGB);
        Graphic3d_MaterialAspect material(Graphic3d_NameOfMaterial_Plastified);
        material.SetSpecularColor(Quantity_NOC_BLACK);
        for (const auto &tool : tools)
        {
            Handle(AIS_Shape) preview = new HolePreviewShape(tool);
            preview->SetLocalTransformation(local);
            preview->SetMaterial(material);
            preview->SetColor(green);
            preview->SetTransparency(0.45);
            preview->SetOwnDeviationCoefficient(0.0005);
            preview->Attributes()->SetFaceBoundaryDraw(true);
            preview->Attributes()->SetFaceBoundaryAspect(
                new Prs3d_LineAspect(Quantity_Color(0.10, 0.60, 0.15, Quantity_TOC_sRGB), Aspect_TOL_SOLID, 1.0));
            // 切孔体需要透过目标板件可见；不参与拾取、包围盒、模型输出或历史。
            preview->SetZLayer(Graphic3d_ZLayerId_Topmost);
            state_->holePreview.push_back(preview);
            state_->context->Display(preview, 1, -1, false);
        }
    }
    catch (...)
    {
        clearHolePreview();
        throw;
    }
}

void OccViewport::setMeasurementEnabled(bool enabled)
{
    if (state_->measurementEnabled == enabled)
    {
        return;
    }
    if (enabled && state_->referencePickMode != ReferencePlanePickMode::None && onReferenceCancelled)
        onReferenceCancelled();
    state_->measurementEnabled = enabled;
    state_->measurement.clear();
    state_->measurementHover.reset();
    setCursor(enabled ? Qt::CrossCursor : Qt::ArrowCursor);
    try
    {
        clearSelectionRectangle();
        if (!state_->context.IsNull())
        {
            if (enabled)
            {
                setSelectedPart({});
                if (onModelObjectsSelected)
                {
                    onModelObjectsSelected({});
                }
            }
            state_->context->ClearDetected(false);
            updateMeasurementPicking();
        }
        if (state_->measurementPresentation)
        {
            state_->measurementPresentation->clear();
        }
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    if (onMeasurementModeChanged)
    {
        onMeasurementModeChanged(enabled);
    }
}
void OccViewport::setEndCapEdges(const std::vector<Vec3> &points, int activeEdge, bool enabled)
{
    state_->capAnchors = points;
    for (int i = 0; i < 2; ++i)
    {
        auto *&button = state_->capButtons[i];
        if (!button && !points.empty())
        {
            button = new QPushButton(QStringLiteral("◉ 边缘 %1").arg(i + 1), this);
            button->setObjectName(QStringLiteral("endCap.sceneEdge%1").arg(i + 1));
            button->setCheckable(true);
            button->setFixedSize(84, 28);
            button->setStyleSheet(QStringLiteral(
                "QPushButton { background:#f1f5f8; color:#475c69; border:1px solid #89a5b8; border-radius:4px; } "
                "QPushButton:checked { background:#164963; color:#ffffff; border:2px solid #00aeef; }"));
            connect(button, &QPushButton::clicked, this,
                    [this, i]
                    {
                        if (onEndCapEdgeSelected)
                            onEndCapEdgeSelected(i + 1);
                    });
        }
        if (button)
        {
            button->setChecked(activeEdge == i + 1);
            button->setEnabled(enabled);
            button->setVisible(points.size() == 2 && state_->modelVisible);
            button->raise();
        }
    }
    updateEndCapEdges();
}
void OccViewport::updateEndCapEdges()
{
    if (state_->view.IsNull() || state_->capAnchors.size() != 2)
        return;
    QRect previous;
    for (int i = 0; i < 2; ++i)
    {
        auto *button = state_->capButtons[i];
        const auto &p = state_->capAnchors[i];
        int x, y;
        state_->view->Convert(p.x, p.y, p.z, x, y);
        x = qRound(x / devicePixelRatioF());
        y = qRound(y / devicePixelRatioF());
        button->setVisible(state_->modelVisible && rect().adjusted(-20, -20, 20, 20).contains(x, y));
        QRect label(QPoint(std::clamp(x - 42, 0, width() - 84), std::clamp(y - 32, 0, height() - 28)), QSize(84, 28));
        if (i && label.intersects(previous))
            label.moveTop(std::clamp(previous.bottom() + 4, 0, height() - 28));
        button->setGeometry(label);
        previous = label;
    }
}

void OccViewport::updateFocusLocator()
{
    if (state_->context.IsNull())
        return;
    if (!state_->focusMarker.IsNull())
        state_->context->Erase(state_->focusMarker, false);
    if (!state_->focusLabel.IsNull())
        state_->context->Erase(state_->focusLabel, false);
    if (state_->focusPartId.empty() || !state_->modelVisible || state_->transform || state_->measurementEnabled ||
        state_->referencePickMode != ReferencePlanePickMode::None || state_->section.enabled)
        return;
    const auto selected = std::find_if(state_->parts.begin(), state_->parts.end(),
                                       [this](const auto &p) { return p.id == state_->focusPartId; });
    if (selected == state_->parts.end() || selected->vertices.empty())
        return;
    int x0 = std::numeric_limits<int>::max(), y0 = x0, x1 = std::numeric_limits<int>::min(), y1 = x1;
    Vec3 low = selected->vertices.front(), high = low;
    for (const auto &p : selected->vertices)
    {
        int x, y;
        state_->view->Convert(p.x, p.y, p.z, x, y);
        x0 = std::min(x0, x);
        x1 = std::max(x1, x);
        y0 = std::min(y0, y);
        y1 = std::max(y1, y);
        low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
        high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
    }
    const double ratio = devicePixelRatioF();
    const int w = qRound(width() * ratio), h = qRound(height() * ratio);
    // 只为整体视图中的小目标补充屏幕尺寸固定的定位标记，放大后显示正常选择高亮。
    if (double(x1) - x0 >= 40 * ratio || double(y1) - y0 >= 40 * ratio || x1 < 0 || y1 < 0 || x0 >= w || y0 >= h)
        return;
    if (state_->focusMarker.IsNull())
    {
        state_->focusMarker = new AIS_Point(new Geom_CartesianPoint(0, 0, 0));
        state_->focusMarker->SetInfiniteState(true);
        state_->focusMarker->SetZLayer(Graphic3d_ZLayerId_Topmost);
        state_->focusLabel = new AIS_TextLabel();
        state_->focusLabel->SetFont("Microsoft YaHei");
        state_->focusLabel->SetZoomable(false);
        state_->focusLabel->SetOwnAnchorPoint(true);
        state_->focusLabel->SetHJustification(Graphic3d_HTA_LEFT);
        state_->focusLabel->SetVJustification(Graphic3d_VTA_CENTER);
        state_->focusLabel->SetDisplayType(Aspect_TODT_SUBTITLE);
        state_->focusLabel->SetColorSubTitle(Quantity_Color(.031, .082, .129, Quantity_TOC_sRGB));
        state_->focusLabel->SetInfiniteState(true);
        state_->focusLabel->SetZLayer(Graphic3d_ZLayerId_TopOSD);
    }
    const Quantity_Color color(1.0, .65, .12, Quantity_TOC_sRGB);
    const gp_Pnt anchor((low.x + high.x) * .5, (low.y + high.y) * .5, (low.z + high.z) * .5);
    state_->focusMarker->SetComponent(new Geom_CartesianPoint(anchor));
    state_->focusMarker->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O, color, 5 * ratio));
    const QString id = QString::fromStdString(selected->id);
    const auto text = (id.startsWith(QStringLiteral("obstacle-")) ? QStringLiteral("障碍物 %1").arg(id.mid(9))
                                                                  : QStringLiteral("选中部件"))
                          .toUtf8();
    state_->focusLabel->SetText(TCollection_ExtendedString(text.constData(), true));
    state_->focusLabel->SetColor(color);
    state_->focusLabel->SetHeight(14 * ratio);
    int x, y;
    state_->view->Convert(anchor.X(), anchor.Y(), anchor.Z(), x, y);
    const int labelX =
        std::clamp(x + qRound(20 * ratio), qRound(8 * ratio), std::max(qRound(8 * ratio), w - qRound(130 * ratio)));
    const int labelY =
        std::clamp(y - qRound(20 * ratio), qRound(20 * ratio), std::max(qRound(20 * ratio), h - qRound(20 * ratio)));
    double ax, ay, az, bx, by, bz;
    state_->view->Convert(x, y, ax, ay, az);
    state_->view->Convert(labelX, labelY, bx, by, bz);
    state_->focusLabel->SetPosition(anchor.Translated(gp_Vec(bx - ax, by - ay, bz - az)));
    state_->context->Display(state_->focusMarker, 0, -1, false);
    state_->context->Redisplay(state_->focusMarker, false);
    state_->context->Display(state_->focusLabel, 0, -1, false);
    state_->context->Redisplay(state_->focusLabel, false);
}
void OccViewport::setReferencePlane(std::shared_ptr<const ReferencePlane> plane, PlaneSection section,
                                    const Vec3 &origin, std::optional<ReferencePlane> sectionPlane)
{
    state_->referencePlane = plane ? std::optional<ReferencePlane>(localReferencePlane(*plane, origin)) : std::nullopt;
    state_->sectionPlane =
        sectionPlane ? std::optional<ReferencePlane>(localReferencePlane(*sectionPlane, origin)) : std::nullopt;
    state_->section = section;
    try
    {
        updateCloudClip();
        updateReferencePresentation();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setSectionPreviewVisible(bool visible)
{
    state_->sectionPreviewVisible = visible;
    updateReferencePresentation();
    update();
}

void OccViewport::updateCloudClip()
{
    if (state_->cloud.IsNull())
        return;
    if (!state_->cloudClip.IsNull())
        state_->cloud->RemoveClipPlane(state_->cloudClip);
    state_->cloudClip.Nullify();
    if (state_->sectionPlane && state_->section.enabled)
    {
        const auto e = sectionEquation(*state_->sectionPlane, state_->section);
        state_->cloudClip = new Graphic3d_ClipPlane(NCollection_Vec4<double>(e[0], e[1], e[2], e[3]));
        state_->cloud->AddClipPlane(state_->cloudClip);
    }
}

void OccViewport::setReferencePlanePicking(ReferencePlanePickMode mode)
{
    if (mode != ReferencePlanePickMode::None)
        setMeasurementEnabled(false);
    state_->referencePickMode = mode;
    setSelectedPart({});
    state_->referencePoints.clear();
    state_->measurementHover.reset();
    state_->measurement.clear();
    if (state_->measurementPresentation)
        state_->measurementPresentation->clear();
    clearSelectionRectangle();
    setCursor(mode == ReferencePlanePickMode::None ? Qt::ArrowCursor : Qt::CrossCursor);
    updateMeasurementPicking();
    updateReferencePresentation();
    update();
}

void OccViewport::setPointModelingPicking(bool enabled, bool face)
{
    if (state_->pointModelingPicking == enabled &&
        (!enabled ||
         state_->referencePickMode == (face ? ReferencePlanePickMode::Face : ReferencePlanePickMode::OrderedPoints)))
        return;
    state_->pointModelingPicking = enabled;
    if (enabled && face)
        setModelPickMode(ModelPickMode::Face);
    setReferencePlanePicking(enabled ? (face ? ReferencePlanePickMode::Face : ReferencePlanePickMode::OrderedPoints)
                                     : ReferencePlanePickMode::None);
}
void OccViewport::setReferencePreview(std::vector<Vec3> points)
{
    if (points.size() == state_->referencePoints.size() &&
        std::equal(points.begin(), points.end(), state_->referencePoints.begin(),
                   [](const Vec3 &a, const Vec3 &b) { return a.x == b.x && a.y == b.y && a.z == b.z; }))
        return;
    state_->referencePoints = std::move(points);
    updateReferencePresentation();
    update();
}

void OccViewport::updateReferencePresentation()
{
    if (!state_->referencePresentation)
        return;
    const bool picking = state_->referencePickMode != ReferencePlanePickMode::None;
    // 选点建模的指定面直接高亮模型面，不显示用于“三点建基准面”的编号和辅助大平面。
    const bool modelingFace = state_->pointModelingPicking && state_->referencePickMode == ReferencePlanePickMode::Face;
    auto plane = picking ? std::optional<ReferencePlane>{} : state_->referencePlane;
    if (!picking && state_->section.enabled)
        plane = state_->sectionPreviewVisible ? state_->sectionPlane : std::nullopt;
    if (picking && !modelingFace && state_->referencePickMode != ReferencePlanePickMode::OrderedPoints &&
        state_->referencePoints.size() == 3)
        plane =
            makeReferencePlane({state_->referencePoints[0], state_->referencePoints[1], state_->referencePoints[2]});
    Vec3 low{}, high{};
    bool first = true;
    const auto include = [&](const Vec3 &p)
    {
        if (first)
        {
            low = high = p;
            first = false;
        }
        else
        {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    };
    for (const auto &p : state_->points)
        include(p.position);
    for (const auto &part : state_->parts)
        for (const auto &p : part.vertices)
            include(p);
    for (const auto &p : state_->referencePoints)
        include(p);
    state_->referencePresentation->update(
        picking && !modelingFace ? state_->referencePoints : std::vector<Vec3>{}, plane, low, high,
        !picking && state_->section.enabled ? state_->section.offsetMm : 0, devicePixelRatioF());
}

void OccViewport::pickReference(const QPoint &pixel)
{
    QString message;
    try
    {
        if ((state_->referencePickMode == ReferencePlanePickMode::ThreePoints ||
             state_->referencePickMode == ReferencePlanePickMode::OrderedPoints))
        {
            const auto picked = measurementPoint(pixel);
            if (!picked)
                return;
            auto points = state_->referencePoints;
            if (points.size() == 3 && state_->referencePickMode != ReferencePlanePickMode::OrderedPoints)
                points.clear();
            if (std::any_of(points.begin(), points.end(),
                            [&](const Vec3 &p)
                            {
                                return std::hypot(p.x - picked->position.x, p.y - picked->position.y,
                                                  p.z - picked->position.z) < 1e-6;
                            }))
                throw std::invalid_argument("该点已选择，请选择其他位置。");
            points.push_back(picked->position);
            if (points.size() == 3 && state_->referencePickMode != ReferencePlanePickMode::OrderedPoints)
                (void)makeReferencePlane({points[0], points[1], points[2]});
            setReferencePreview(std::move(points));
        }
        else if (state_->model)
        {
            detectObject(pixel);
            const auto owner = state_->context->DetectedOwner();
            const auto selected = state_->model->detectedSelection(owner);
            if (selected.partId.empty() || selected.kind != ModelPickMode::Face)
                throw std::invalid_argument(
                    "未选中模型平面。请左键点击灰色模型的平面区域；点云点或空白处不能作为参考面。");
            const auto part = std::find_if(state_->parts.begin(), state_->parts.end(),
                                           [&](const auto &p) { return p.id == selected.partId; });
            if (part == state_->parts.end())
                return;
            const auto plane = referencePlaneFromFace(*part, selected.index);
            setSelectedObjects({selected});
            setReferencePreview(std::vector<Vec3>(plane.pointsMm.begin(), plane.pointsMm.end()));
        }
    }
    catch (const std::exception &error)
    {
        if (state_->referencePickMode == ReferencePlanePickMode::Face)
        {
            setSelectedPart({});
            setReferencePreview({});
        }
        message = QString::fromUtf8(error.what());
    }
    if (state_->pointModelingPicking && onModelingPicked)
        onModelingPicked(state_->referencePoints, message);
    else if (onReferencePicked)
        onReferencePicked(state_->referencePoints, message);
    redraw();
}
bool OccViewport::beginTransformPreview(const std::vector<std::string> &ids, Vec3 origin, const ModelTransform &request)
{
    endTransformPreview();
    if (!state_->model || state_->view.IsNull())
        return false;
    const auto objects = state_->model->objectsForParts(ids);
    if (objects.empty())
        return false;
    setMeasurementEnabled(false);
    clearSelectionRectangle();
    state_->model->setTransformPreview(true);
    try
    {
        state_->transform = std::make_unique<OccTransformPreview>(state_->context, state_->view, objects, state_->parts,
                                                                  ids, origin, request, devicePixelRatioF());
        redraw();
        return true;
    }
    catch (const Standard_Failure &error)
    {
        state_->model->setTransformPreview(false);
        if (onError)
            onError(QString::fromUtf8(error.what()));
        return false;
    }
}
void OccViewport::updateTransformPreview(const ModelTransform &request)
{
    if (!state_->transform)
        return;
    state_->transform->apply(request);
    redraw();
}
void OccViewport::endTransformPreview()
{
    if (!state_->transform)
        return;
    state_->transform.reset();
    if (state_->model)
    {
        state_->model->setTransformPreview(false);
        state_->model->selectObjects(state_->selectedObjects);
    }
    update();
}

} // namespace reverse::workbench
