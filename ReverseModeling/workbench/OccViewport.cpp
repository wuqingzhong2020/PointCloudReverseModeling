/*
 * 文件描述：
 * 使用原生 QWidget 窗口承载 OCCT/AIS，显示点云采样和模型三角化结果，
 * 实现缩放、平移、旋转、可点击视图立方体和底板正视；不执行建模算法。
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <AIS_AnimationCamera.hxx>
#include <AIS_InteractiveContext.hxx>
#include <AIS_PointCloud.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_Shape.hxx>
#include <AIS_Triangulation.hxx>
#include <Poly_Triangulation.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <TopoDS_Compound.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <TopoDS_Edge.hxx>
#include <AIS_RubberBand.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Graphic3d_RenderingParams.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Graphic3d_TypeOfLimit.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_LineAspect.hxx>
#include <Prs3d_Presentation.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_TypeOfColor.hxx>
#include <SelectMgr_ViewerSelector.hxx>
#include <SelectMgr_Filter.hxx>
#include <Standard_Failure.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gp_Dir.hxx>

#if defined(_WIN32)
#include <WNT_Window.hxx>
#elif defined(__linux__)
#include <Xw_Window.hxx>
#endif

#include "workbench/OccModelPresentation.h"
#include "workbench/OccTransformPreview.h"
#include "workbench/OccMeasurementPresentation.h"
#include "workbench/OccMeasurementPicking.h"
#include "workbench/MeasurementSnap.h"
#include "workbench/OccPicking.h"
#include "workbench/ThicknessLabelEditor.h"
#include "workbench/OccPartMesh.h"
#include "workbench/OccViewport.h"
#include "workbench/OccReferencePlanePresentation.h"
#include "workbench/ReferencePlanePicking.h"
#include "workbench/SelectionRectangle.h"
#include "geometry/ReferencePlane.h"
#include "geometry/PlateHoles.h"

namespace reverse::workbench {

namespace
{
class HolePreviewShape final : public AIS_Shape
{
public:
    explicit HolePreviewShape(const TopoDS_Shape &shape) : AIS_Shape(shape) {}

protected:
    void Compute(const Handle(PrsMgr_PresentationManager) &manager,
                 const Handle(Prs3d_Presentation) &presentation, int mode) override
    {
        // AIS_Shape 的无限对象会强制退化为线框；先生成实体，再排除它的显示包围盒。
        AIS_Shape::Compute(manager, presentation, mode);
        presentation->SetInfiniteState(true);
    }
};

class ModelSelectionFilter final : public SelectMgr_Filter
{
public:
    ModelPickMode mode{ModelPickMode::Body};
    bool IsOk(const Handle(SelectMgr_EntityOwner) &owner) const override
    {
        const auto *object = dynamic_cast<const OccMeshOwner *>(owner.get());
        return !object || object->kind == mode;
    }
};
}

struct OccViewport::State {
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_PointCloud) cloud;
    Handle(AIS_ViewCube) viewCube;
    Handle(AIS_RubberBand) selectionBand;
    std::vector<Handle(AIS_InteractiveObject)> holeGuide;
    std::vector<Handle(AIS_InteractiveObject)> holePreview;
    std::vector<Handle(AIS_InteractiveObject)> plateEditReference;
    std::vector<std::pair<PlateHoleBase,PlateHole>> batchHolePreview;
    bool plateCropEnabled{};
    std::optional<PlateHoleBase> holePreviewBase;
    PlateHole holePreviewRequest;
    Vec3 holePreviewOrigin;
    std::shared_ptr<const CloudSelection> selection;
    CloudSelectionMode selectionMode{CloudSelectionMode::Replace};
    bool cloudEditingEnabled{};
    bool modelPickingEnabled{};
    bool thicknessEditingEnabled{};
    bool pressedThicknessLabel{};
    ThicknessLabelEditor *thicknessEditor{};
    ModelPickMode pickMode{ModelPickMode::Body};
    Handle(ModelSelectionFilter) pickFilter = new ModelSelectionFilter();
    ReferencePlanePickMode referencePickMode{ReferencePlanePickMode::None};
    std::vector<Vec3> referencePoints;
    std::optional<ReferencePlane> referencePlane;
    std::optional<ReferencePlane> sectionPlane;
    bool sectionPreviewVisible{};
    PlaneSection section;
    Handle(Graphic3d_ClipPlane) cloudClip;
    std::unique_ptr<OccReferencePlanePresentation> referencePresentation;
    bool measurementEnabled{};
    PointMeasurement measurement;
    std::optional<MeasurementPoint> measurementHover;
    MeasurementSnap measurementSnap;
    std::unique_ptr<OccMeasurementPresentation> measurementPresentation;
    std::vector<ModelSelection> selectedObjects;
    SelectionRectangle rectangle;
    bool selectionDragged{};
    bool rightDragged{};
    QPoint rightPress;
    QTimer *navigationTimer{};
    double cubePixelRatio{};
    std::unique_ptr<OccModelPresentation> model;
    std::unique_ptr<OccTransformPreview> transform;
    std::vector<CloudDisplayPoint> points;
    std::vector<ModelDisplayPart> parts;
    QPoint lastMouse;
    QPoint leftPress;
    bool cloudVisible = true;
    bool cloudHeightColors = true;
    bool modelVisible = true;
    bool modelWireframe{};
    bool thicknessVisible = true;
    bool initializing = false;
    bool failed = false;
    QLabel* failureLabel = nullptr;
};

OccViewport::OccViewport(QWidget* parent) : QWidget(parent), state_(std::make_unique<State>()) {
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral("双击板厚标签修改；左键按点/线/面/体拾取模型；Shift 追加/取消；点模式拖动框选点云，拖边/角缩放、拖框内移动；右键旋转，中键平移，Esc 清空"));
    state_->thicknessEditor = new ThicknessLabelEditor(this);
    state_->thicknessEditor->onSubmitted = [this](const std::string &id, double thickness)
    {
        if (state_->thicknessEditingEnabled && onThicknessLabelSubmitted)
        {
            onThicknessLabelSubmitted(id, thickness);
        }
    };
    state_->navigationTimer = new QTimer(this);
    state_->navigationTimer->setInterval(16);
    connect(state_->navigationTimer, &QTimer::timeout, this, &OccViewport::advanceNavigationAnimation);
}

OccViewport::~OccViewport() { release(); }

void OccViewport::release() noexcept {
    try {
        stopNavigationAnimation();
        state_->holeGuide.clear();
        state_->holePreview.clear();
        state_->transform.reset();
        state_->referencePresentation.reset();
        state_->measurementPresentation.reset();
        if (!state_->context.IsNull()) state_->context->RemoveAll(false);
        state_->viewCube.Nullify();
        state_->selectionBand.Nullify();
        state_->cloud.Nullify();
        state_->model.reset();
        state_->context.Nullify();
        if (!state_->view.IsNull()) state_->view->Remove();
    } catch (...) {
        // 析构期间不能传播图形驱动异常。
    }
    state_->view.Nullify();
    state_->viewer.Nullify();
    state_->driver.Nullify();
    state_->display.Nullify();
}

void OccViewport::fail(const QString& message) {
    release();
    state_->failed = true;
    setAttribute(Qt::WA_PaintOnScreen, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    if (!state_->failureLabel) {
        state_->failureLabel = new QLabel(this);
        state_->failureLabel->setAlignment(Qt::AlignCenter);
        state_->failureLabel->setWordWrap(true);
        state_->failureLabel->setMargin(24);
        state_->failureLabel->setStyleSheet(QStringLiteral("background:#18212d;color:#f2d5c4;"));
    }
    state_->failureLabel->setGeometry(rect());
    state_->failureLabel->setText(QStringLiteral("三维视图初始化或绘制失败\n%1\n文件处理服务仍可使用。").arg(message));
    state_->failureLabel->show();
    if (onError) onError(message);
}

void OccViewport::initialize() {
    if (!state_->view.IsNull() || state_->failed || state_->initializing || !isVisible()) return;
    state_->initializing = true;
    try {
#if defined(__linux__)
        if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
            throw std::runtime_error("Linux native OCCT viewport requires Qt xcb; use QT_QPA_PLATFORM=xcb.");
        }
#endif
        state_->display = new Aspect_DisplayConnection();
        state_->driver = new OpenGl_GraphicDriver(state_->display);
        state_->viewer = new V3d_Viewer(state_->driver);
        state_->viewer->SetDefaultLights();
        state_->viewer->SetLightOn();
        state_->view = state_->viewer->CreateView();
#if defined(_WIN32)
        Handle(WNT_Window) window = new WNT_Window(reinterpret_cast<Aspect_Handle>(winId()));
#elif defined(__linux__)
        Handle(Xw_Window) window = new Xw_Window(state_->display, static_cast<Aspect_Drawable>(winId()));
#else
        throw std::runtime_error("Native OCCT viewport is implemented for Windows and Linux xcb only.");
#endif
#if defined(_WIN32) || defined(__linux__)
        state_->view->SetWindow(window);
        if (!window->IsMapped()) window->Map();
#endif
        // 原生 OCCT 视口不经过 Qt 绘制，必须在实际 OpenGL 上下文创建后启用 MSAA。
        // 同时平滑实体轮廓、拓扑边和选择高亮；只影响显示，不增加模型细分。
        const int maxSamples = state_->driver->InquireLimit(Graphic3d_TypeOfLimit_MaxMsaa);
        auto &rendering = state_->view->ChangeRenderingParams();
        rendering.NbMsaaSamples = 0;
        for (const int samples : {8, 4, 2})
        {
            if (samples <= maxSamples)
            {
                rendering.NbMsaaSamples = samples;
                break;
            }
        }
        state_->view->SetBackgroundColor(Quantity_Color(0.031, 0.082, 0.129, Quantity_TOC_sRGB));
        state_->view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        state_->view->SetProj(V3d_Zpos);
        state_->view->SetUp(0, 1, 0);
        state_->context = new AIS_InteractiveContext(state_->viewer);
        state_->context->AddFilter(state_->pickFilter);
        state_->context->SelectionStyle()->SetColor(Quantity_Color(0.10, 0.66, 0.95, Quantity_TOC_sRGB));
        state_->context->SelectionStyle()->SetDisplayMode(0);
        state_->context->HighlightStyle()->SetColor(Quantity_Color(0.30, 0.82, 0.90, Quantity_TOC_sRGB));
        state_->context->HighlightStyle()->SetDisplayMode(0);
        createViewCube();
        state_->model = std::make_unique<OccModelPresentation>(state_->context, state_->view);
        state_->model->setWireframe(state_->modelWireframe);
        state_->measurementPresentation = std::make_unique<OccMeasurementPresentation>(state_->context, state_->view);
        state_->referencePresentation = std::make_unique<OccReferencePlanePresentation>(state_->context);
        rebuildCloud();
        rebuildMesh();
        rebuildHolePreview();
        updateReferencePresentation();
        updateMeasurementPicking();
        state_->view->MustBeResized();
        fitAll();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
    state_->initializing = false;
}

QPaintEngine* OccViewport::paintEngine() const {
    return state_->failed ? QWidget::paintEngine() : nullptr;
}

bool OccViewport::event(QEvent* event) {
    if (event->type() == QEvent::WinIdChange && !state_->initializing && !state_->view.IsNull()) {
        release();
        QTimer::singleShot(0, this, [this] { initialize(); });
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange) {
        // 等 Qt 更新原生窗口的物理像素尺寸后，在绘制入口同步 OCCT 视口。
        QTimer::singleShot(0, this, [this] { update(); });
    }
#endif
    return QWidget::event(event);
}

void OccViewport::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    // 首次 showEvent 可能早于 splitter/父窗口最终布局与原生 HWND 的尺寸提交。
    // 延迟到本轮事件完成后绑定视图，避免使用构造时 400×300 的最小尺寸。
    QTimer::singleShot(0, this, [this] {
        initialize();
        update();
    });
}

void OccViewport::paintEvent(QPaintEvent*) {
    if (state_->view.IsNull()) return;
    try {
        // 原生 OCCT 绘制不经过 Qt backing store；每次绘制都读取 HWND 当前客户区
        // 同步 OpenGL viewport 和相机纵横比，不能只依赖可能提前到达的 resizeEvent。
        state_->view->MustBeResized();
        updateViewCubeScale();
        redraw();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::resizeEvent(QResizeEvent* event) {
    state_->thicknessEditor->hide();
    QWidget::resizeEvent(event);
    if (state_->failureLabel) state_->failureLabel->setGeometry(rect());
    if (!state_->view.IsNull() && isVisible() && event->size().isValid()) {
        try {
            clearSelectionRectangle();
            state_->view->MustBeResized();
            redraw();
            // 缩小、分隔条拖动以及原生窗口延迟调整后仍需要一次 Qt 绘制事件。
            update();
        } catch (const Standard_Failure& error) {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::rebuildCloud() {
    if (state_->context.IsNull()) return;
    if (!state_->cloud.IsNull()) state_->context->Remove(state_->cloud, false);
    state_->cloud.Nullify();
    if (state_->points.empty()) return;
    if (state_->points.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("Display point count exceeds the OCCT array limit.");
    Handle(Graphic3d_ArrayOfPoints) points = new Graphic3d_ArrayOfPoints(static_cast<int>(state_->points.size()), true);
    for (const auto &point : state_->points)
    {
        const gp_Pnt position(point.position.x, point.position.y, point.position.z);
        const bool selected = state_->selection && std::binary_search(state_->selection->begin(),
                                                                      state_->selection->end(), point.pointIndex);
        if (selected)
        {
            points->AddVertex(position, Quantity_Color(0.04, 0.45, 1.0, Quantity_TOC_sRGB));
        }
        else if (state_->cloudHeightColors)
        {
            const auto &rgb = point.colorSrgb;
            points->AddVertex(position, Quantity_Color(rgb[0], rgb[1], rgb[2], Quantity_TOC_sRGB));
        }
        else
        {
            points->AddVertex(position, Quantity_Color(0.18, 0.56, 0.61, Quantity_TOC_sRGB));
        }
    }
    state_->cloud = new AIS_PointCloud();
    state_->cloud->SetDisplayMode(AIS_PointCloud::DM_Points);
    state_->cloud->SetPoints(points);
    updateCloudClip();
    state_->cloud->SetColor(Quantity_Color(0.18, 0.56, 0.61, Quantity_TOC_sRGB));
    if (state_->cloudVisible) state_->context->Display(state_->cloud, 0, -1, false);
    updateMeasurementPicking();
}

void OccViewport::rebuildMesh()
{
    if (!state_->model)
    {
        return;
    }
    state_->model->rebuild(state_->parts);
    state_->model->setVisible(state_->modelVisible, state_->thicknessVisible);
    updateMeasurementPicking();
    state_->model->selectObjects(state_->selectedObjects);
}

void OccViewport::updateMeasurementPicking()
{
    if (state_->context.IsNull())
    {
        return;
    }
    const bool pointPick = state_->measurementEnabled || state_->referencePickMode == ReferencePlanePickMode::ThreePoints;
    const bool referencePick = state_->referencePickMode != ReferencePlanePickMode::None;
    const auto mode = pointPick ? ModelPickMode::Body
        : referencePick ? ModelPickMode::Face : state_->pickMode;
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
        state_->model->setSelectable(state_->modelPickingEnabled || pointPick || referencePick, mode);
    }
    state_->context->SetPixelTolerance(state_->measurementEnabled
        ? qRound(6 * devicePixelRatioF()) : qRound(5 * devicePixelRatioF()));
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
            if (e[0] * point.X() + e[1] * point.Y() + e[2] * point.Z() + e[3] < -1e-7) continue;
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
        const auto captured = state_->measurementSnap.find(pixel.x(), pixel.y(),
            MeasurementSnap::RadiusPixels * devicePixelRatioF(), [this](const Vec3 &point)
            {
                return isMeasurementPointVisible(gp_Pnt(point.x, point.y, point.z), state_->context,
                    state_->view, [this](const auto &owner) { return state_->model->isSurfaceOwner(owner); });
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
    if (state_->context.IsNull()) return;
    for (const auto &object : state_->holeGuide) state_->context->Remove(object, false);
    state_->holeGuide.clear();
    if (points.size() == 3)
    {
        const auto point = [](Vec3 p) { return gp_Pnt(p.x,p.y,p.z); };
        Handle(AIS_Shape) edge = new AIS_Shape(BRepBuilderAPI_MakeEdge(point(points[0]),point(points[2])).Edge());
        edge->SetColor(Quantity_Color(0.0, 0.48, 1.0, Quantity_TOC_sRGB)); edge->SetWidth(2);
        edge->SetZLayer(Graphic3d_ZLayerId_Topmost);
        state_->context->Display(edge,0,-1,false); state_->holeGuide.push_back(edge);
        const QStringList names{QStringLiteral("● 1"),QStringLiteral("● 中"),QStringLiteral("● 2")};
        for (int i=0;i<3;++i)
        {
            Handle(AIS_TextLabel) label = new AIS_TextLabel();
            label->SetText(TCollection_ExtendedString(names[i].toUtf8().constData(),true));
            label->SetPosition(point(points[i])); label->SetHeight(16); label->SetFont("Microsoft YaHei");
            label->SetColor(Quantity_Color(0.0, 0.48, 1.0, Quantity_TOC_sRGB)); label->SetZLayer(Graphic3d_ZLayerId_Topmost);
            state_->context->Display(label,0,-1,false); state_->holeGuide.push_back(label);
        }
    }
    redraw();
}

void OccViewport::clearPlateEditReference()
{
    if(!state_->context.IsNull()) for(const auto &object:state_->plateEditReference)
        state_->context->Remove(object,false);
    state_->plateEditReference.clear();redraw();
}

void OccViewport::setPlateEditReference(const std::vector<ModelDisplayPart> &original,
    const std::vector<std::string> &sources,const std::string &target,bool removedMaterial)
{
    clearPlateEditReference();if(state_->context.IsNull()) return;
    for(const auto &part:original) {
        const bool isTarget=part.id==target;
        if(!isTarget&&std::find(sources.begin(),sources.end(),part.id)==sources.end()) continue;
        if(removedMaterial&&!isTarget&&!part.triangles.empty()) {
            Handle(Poly_Triangulation) mesh=new Poly_Triangulation(static_cast<int>(part.vertices.size()),static_cast<int>(part.triangles.size()),false);
            for(std::size_t i=0;i<part.vertices.size();++i) {const auto &p=part.vertices[i];mesh->SetNode(static_cast<int>(i+1),gp_Pnt(p.x,p.y,p.z));}
            for(std::size_t i=0;i<part.triangles.size();++i) {const auto &t=part.triangles[i];mesh->SetTriangle(static_cast<int>(i+1),Poly_Triangle(t[0]+1,t[1]+1,t[2]+1));}
            mesh->ComputeNormals();Handle(AIS_Triangulation) ghost=new AIS_Triangulation(mesh);
            ghost->SetColor(Quantity_Color(.95,.2,.18,Quantity_TOC_sRGB));ghost->SetTransparency(.55);
            ghost->SetPolygonOffsets(1,2,2);ghost->SetInfiniteState(true);
            state_->context->Display(ghost,0,-1,false);state_->plateEditReference.push_back(ghost);
        }
        TopoDS_Compound edges;BRep_Builder builder;builder.MakeCompound(edges);
        for(const auto &points:part.edges) {
            if(points.size()<2) continue;BRepBuilderAPI_MakePolygon polygon;
            for(const auto &p:points) polygon.Add(gp_Pnt(p.x,p.y,p.z));
            if(polygon.IsDone()) builder.Add(edges,polygon.Wire());
        }
        Handle(AIS_Shape) wire=new AIS_Shape(edges);
        wire->SetColor(isTarget?Quantity_Color(1,.55,.08,Quantity_TOC_sRGB):Quantity_Color(.1,.65,.95,Quantity_TOC_sRGB));
        wire->SetWidth(isTarget?2:1);wire->SetInfiniteState(true);
        state_->context->Display(wire,0,-1,false);state_->plateEditReference.push_back(wire);
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

void OccViewport::setBatchHolePreview(const std::vector<std::pair<PlateHoleBase,PlateHole>> &items,const Vec3 &origin)
{
    state_->holePreviewBase.reset();state_->batchHolePreview=items;state_->holePreviewOrigin=origin;
    rebuildHolePreview();update();
}
void OccViewport::setPlateCropEnabled(bool enabled, bool preserveRectangle)
{
    if (state_->plateCropEnabled == enabled && preserveRectangle) return;
    state_->plateCropEnabled = enabled;
    if (!preserveRectangle) clearSelectionRectangle();
    updateSelectionCursor(state_->lastMouse);
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
        if (state_->holePreviewBase) tools=detail::plateHoleTools(*state_->holePreviewBase,state_->holePreviewRequest);
        for (const auto &[base,request]:state_->batchHolePreview) {
            const auto batch=detail::plateHoleTools(base,request);tools.insert(tools.end(),batch.begin(),batch.end());
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

void OccViewport::redraw()
{
    // 工具在主窗口构造/销毁阶段也会清理预览，此时 OpenGL 视图可能尚未创建。
    if (state_->view.IsNull()) return;
    if ((state_->measurementEnabled || state_->referencePickMode == ReferencePlanePickMode::ThreePoints) &&
        state_->measurementPresentation)
    {
        const double ratio = devicePixelRatioF();
        state_->measurementPresentation->update(state_->measurement, state_->measurementHover,
            qRound(width() * ratio), qRound(height() * ratio), ratio);
    }
    if (state_->model)
    {
        const double ratio = devicePixelRatioF();
        state_->model->updateLabels(qRound(width() * ratio), qRound(height() * ratio), ratio);
    }
    state_->view->Redraw();
}

void OccViewport::setPointCloud(std::vector<CloudDisplayPoint> points,
                                std::shared_ptr<const CloudSelection> selection)
{
    setMeasurementEnabled(false);
    state_->points = std::move(points);
    try {
        state_->selection = std::move(selection);
        clearSelectionRectangle();
        rebuildCloud();
        update();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setCloudHeightColors(bool enabled)
{
    state_->cloudHeightColors = enabled;
    try
    {
        rebuildCloud();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    catch (const std::exception &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setModel(std::vector<ModelDisplayPart> parts)
{
    state_->thicknessEditor->hide();
    endTransformPreview();
    setMeasurementEnabled(false);
    state_->selectedObjects.clear();
    state_->parts = std::move(parts);
    state_->measurementSnap.rebuild(state_->parts);
    try
    {
        rebuildMesh();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    catch (const std::exception &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setThicknessEditingEnabled(bool enabled)
{
    state_->thicknessEditingEnabled = enabled;
    if (!enabled)
    {
        state_->thicknessEditor->hide();
    }
}

void OccViewport::setModelPickingEnabled(bool enabled)
{
    if (state_->modelPickingEnabled == enabled) return;
    state_->modelPickingEnabled = enabled;
    try
    {
        updateMeasurementPicking();
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setModelPickMode(ModelPickMode mode)
{
    if (state_->pickMode == mode) return;
    state_->pickMode = mode;
    try
    {
        clearSelectionRectangle();
        setSelectedPart({});
        updateMeasurementPicking();
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::detectObject(const QPoint &pixel)
{
    state_->context->MoveTo(pixel.x(), pixel.y(), state_->view, false);
    const auto owner = state_->context->DetectedOwner();
    if (!state_->model || state_->model->detectedPartId(owner).empty()) return;
    const auto selector = state_->context->MainSelector();
    std::optional<gp_Pnt> position;
    for (int rank = 1; rank <= selector->NbPicked(); ++rank)
    {
        if (selector->Picked(rank) == owner)
        {
            position = selector->PickedPoint(rank);
            break;
        }
    }
    if (position && !isMeasurementPointVisible(*position, state_->context, state_->view,
        [this](const auto &candidate) { return state_->model->isSurfaceOwner(candidate); }))
    {
        state_->context->ClearDetected(false);
    }
}

void OccViewport::setSelectedPart(const std::string &id)
{
    setSelectedParts(id.empty() ? std::vector<std::string>{} : std::vector<std::string>{id});
}

std::vector<std::string> OccViewport::selectedParts() const
{
    std::vector<std::string> ids;
    if (state_->pickMode == ModelPickMode::Body)
        for (const auto &selection : state_->selectedObjects) ids.push_back(selection.partId);
    return ids;
}

const std::vector<ModelSelection> &OccViewport::selectedObjects() const
{
    return state_->selectedObjects;
}

void OccViewport::setSelectedParts(const std::vector<std::string> &ids)
{
    std::vector<ModelSelection> selections;
    for (const auto &id : ids) selections.push_back({id, ModelPickMode::Body, 0});
    setSelectedObjects(selections);
}

void OccViewport::setSelectedObjects(const std::vector<ModelSelection> &selections)
{
    std::vector<ModelSelection> valid;
    if (state_->modelVisible)
        for (const auto &selection : selections)
            if (selection.kind == state_->pickMode &&
                std::any_of(state_->parts.begin(), state_->parts.end(),
                    [&](const auto &part) { return isValidModelSelection(part, selection); }) &&
                std::find(valid.begin(), valid.end(), selection) == valid.end())
                valid.push_back(selection);
    state_->selectedObjects = std::move(valid);
    try
    {
        if (state_->model) state_->model->selectObjects(state_->selectedObjects);
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setCloudVisible(bool visible) {
    if (!visible)
    {
        setMeasurementEnabled(false);
    }
    state_->cloudVisible = visible;
    if (!visible)
    {
        clearSelectionRectangle();
    }
    if (!state_->context.IsNull() && !state_->cloud.IsNull()) {
        try {
            if (visible) state_->context->Display(state_->cloud, 0, -1, false);
            else state_->context->Erase(state_->cloud, false);
            updateMeasurementPicking();
            update();
        } catch (const Standard_Failure& error) {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::setModelWireframe(bool wireframe)
{
    state_->modelWireframe = wireframe;
    try
    {
        if (state_->model) state_->model->setWireframe(wireframe);
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setModelVisible(bool visible)
{
    if (!visible)
    {
        setMeasurementEnabled(false);
    }
    state_->modelVisible = visible;
    state_->thicknessEditor->hide();
    if (!visible)
    {
        setSelectedPart({});
        if (onModelObjectsSelected) onModelObjectsSelected({});
    }
    try
    {
        if (state_->model)
        {
            state_->model->setVisible(visible, state_->thicknessVisible);
        }
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setReferencePlane(std::shared_ptr<const ReferencePlane> plane, PlaneSection section, const Vec3 &origin,
                                   std::optional<ReferencePlane> sectionPlane)
{
    state_->referencePlane = plane ? std::optional<ReferencePlane>(localReferencePlane(*plane, origin)) : std::nullopt;
    state_->sectionPlane = sectionPlane ? std::optional<ReferencePlane>(localReferencePlane(*sectionPlane, origin)) : std::nullopt;
    state_->section = section;
    try
    {
        updateCloudClip();
        updateReferencePresentation();
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setSectionPreviewVisible(bool visible)
{
    state_->sectionPreviewVisible = visible;
    updateReferencePresentation();
    update();
}

void OccViewport::updateCloudClip()
{
    if (state_->cloud.IsNull()) return;
    if (!state_->cloudClip.IsNull()) state_->cloud->RemoveClipPlane(state_->cloudClip);
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
    if (mode != ReferencePlanePickMode::None) setMeasurementEnabled(false);
    state_->referencePickMode = mode;
    setSelectedPart({});
    state_->referencePoints.clear();
    state_->measurementHover.reset();
    state_->measurement.clear();
    if (state_->measurementPresentation) state_->measurementPresentation->clear();
    clearSelectionRectangle();
    setCursor(mode == ReferencePlanePickMode::None ? Qt::ArrowCursor : Qt::CrossCursor);
    updateMeasurementPicking();
    updateReferencePresentation();
    update();
}

void OccViewport::setReferencePreview(std::vector<Vec3> points)
{
    state_->referencePoints = std::move(points);
    updateReferencePresentation();
    update();
}

void OccViewport::updateReferencePresentation()
{
    if (!state_->referencePresentation) return;
    const bool picking = state_->referencePickMode != ReferencePlanePickMode::None;
    auto plane = picking ? std::optional<ReferencePlane>{} : state_->referencePlane;
    if (!picking && state_->section.enabled)
        plane = state_->sectionPreviewVisible ? state_->sectionPlane : std::nullopt;
    if (picking && state_->referencePoints.size() == 3)
        plane = makeReferencePlane({state_->referencePoints[0], state_->referencePoints[1], state_->referencePoints[2]});
    Vec3 low{}, high{};
    bool first = true;
    const auto include = [&](const Vec3 &p) {
        if (first) { low = high = p; first = false; }
        else
        {
            low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
            high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
        }
    };
    for (const auto &p : state_->points) include(p.position);
    for (const auto &part : state_->parts) for (const auto &p : part.vertices) include(p);
    for (const auto &p : state_->referencePoints) include(p);
    state_->referencePresentation->update(picking ? state_->referencePoints : std::vector<Vec3>{}, plane,
        low, high, !picking && state_->section.enabled ? state_->section.offsetMm : 0, devicePixelRatioF());
}

void OccViewport::pickReference(const QPoint &pixel)
{
    QString message;
    try
    {
        if (state_->referencePickMode == ReferencePlanePickMode::ThreePoints)
        {
            const auto picked = measurementPoint(pixel);
            if (!picked) return;
            auto points = state_->referencePoints;
            if (points.size() == 3) points.clear();
            if (std::any_of(points.begin(), points.end(), [&](const Vec3 &p) {
                return std::hypot(p.x - picked->position.x, p.y - picked->position.y, p.z - picked->position.z) < 1e-6;
            })) throw std::invalid_argument("该点已选择，请选择其他位置。");
            points.push_back(picked->position);
            if (points.size() == 3) (void)makeReferencePlane({points[0], points[1], points[2]});
            setReferencePreview(std::move(points));
        }
        else if (state_->model)
        {
            detectObject(pixel);
            const auto owner = state_->context->DetectedOwner();
            const auto selected = state_->model->detectedSelection(owner);
            if (selected.partId.empty() || selected.kind != ModelPickMode::Face) return;
            const auto part = std::find_if(state_->parts.begin(), state_->parts.end(),
                [&](const auto &p) { return p.id == selected.partId; });
            if (part == state_->parts.end()) return;
            const auto plane = referencePlaneFromFace(*part, selected.index);
            state_->model->selectOwner(owner);
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
    if (onReferencePicked) onReferencePicked(state_->referencePoints, message);
    redraw();
}

void OccViewport::setThicknessVisible(bool visible)
{
    state_->thicknessVisible = visible;
    state_->thicknessEditor->hide();
    setModelVisible(state_->modelVisible);
}

void OccViewport::createViewCube()
{
    // 参考 CamAssemblyWelding 的原生 AIS 导航，始终位于右上角且不计入工件包围盒。
    state_->viewCube = new AIS_ViewCube();
    auto &cube = state_->viewCube;
    cube->SetInfiniteState(true);
    cube->SetBoxColor(Quantity_Color(0.57, 0.61, 0.66, Quantity_TOC_sRGB));
    cube->SetTextColor(Quantity_Color(0.15, 0.18, 0.21, Quantity_TOC_sRGB));
    cube->SetFont("Microsoft YaHei");
    cube->SetBoxSideLabel(V3d_Xpos, QStringLiteral("右").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Xneg, QStringLiteral("左").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Ypos, QStringLiteral("前").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Yneg, QStringLiteral("后").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Zpos, QStringLiteral("上").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Zneg, QStringLiteral("下").toUtf8().constData());
    cube->Attributes()->SetDatumAspect(new Prs3d_DatumAspect());
    const auto &datum = cube->Attributes()->DatumAspect();
    const std::array axes{Prs3d_DatumParts_XAxis, Prs3d_DatumParts_YAxis, Prs3d_DatumParts_ZAxis};
    const std::array colors{Quantity_NOC_RED, Quantity_NOC_GREEN, Quantity_NOC_BLUE};
    for (std::size_t index = 0; index < axes.size(); ++index)
    {
        datum->ShadingAspect(axes[index])->SetColor(colors[index]);
        datum->TextAspect(axes[index])->SetColor(colors[index]);
    }
    cube->SetAutoStartAnimation(false);
    cube->SetFixedAnimationLoop(false);
    cube->SetResetCamera(true);
    cube->SetFitSelected(false);
    cube->SetDuration(0.25);
    cube->ViewAnimation()->SetView(state_->view);
    state_->cubePixelRatio = 0;
    updateViewCubeScale();
    state_->context->Display(cube, 0, 0, false);
}

void OccViewport::updateViewCubeScale()
{
    const double ratio = devicePixelRatioF();
    if (state_->viewCube.IsNull() || state_->cubePixelRatio == ratio)
    {
        return;
    }
    state_->cubePixelRatio = ratio;
    state_->viewCube->SetSize(60 * ratio);
    state_->viewCube->SetAxesPadding(4 * ratio);
    state_->viewCube->SetFontHeight(18 * ratio);
    state_->viewCube->SetTransformPersistence(new Graphic3d_TransformPers(
        Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
        NCollection_Vec2<int>(qRound(90 * ratio), qRound(90 * ratio))));
    if (state_->context->IsDisplayed(state_->viewCube))
    {
        state_->context->Redisplay(state_->viewCube, false);
    }
}

void OccViewport::stopNavigationAnimation()
{
    state_->navigationTimer->stop();
    if (!state_->viewCube.IsNull())
    {
        state_->viewCube->ViewAnimation()->Stop();
    }
}

void OccViewport::advanceNavigationAnimation()
{
    if (state_->viewCube.IsNull() || state_->view.IsNull())
    {
        state_->navigationTimer->stop();
        return;
    }
    try
    {
        state_->viewCube->UpdateAnimation(false);
        redraw(); // 每一帧同步板厚遮挡与标签位置。
        if (!state_->viewCube->HasAnimation())
        {
            state_->navigationTimer->stop();
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::showBottomView(const Vec3 &normal, const Vec3 &longitudinal)
{
    clearSelectionRectangle();
    if (state_->view.IsNull())
    {
        return;
    }
    try
    {
        stopNavigationAnimation();
        const gp_Dir towardEye(normal.x, normal.y, normal.z);
        const gp_Dir along(longitudinal.x, longitudinal.y, longitudinal.z);
        // Up = 法线 × 长轴，使相机的屏幕右方向沿工件长轴，而不是按世界 XY 猜测。
        const gp_Dir up = towardEye.Crossed(along);
        state_->view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        state_->view->SetProj(towardEye.X(), towardEye.Y(), towardEye.Z());
        state_->view->SetUp(up.X(), up.Y(), up.Z());
        state_->context->ClearDetected(false);
        redraw();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

bool OccViewport::beginTransformPreview(const std::vector<std::string> &ids, Vec3 origin, const ModelTransform &request)
{
    endTransformPreview();
    if (!state_->model || state_->view.IsNull()) return false;
    const auto objects = state_->model->objectsForParts(ids);
    if (objects.empty()) return false;
    setMeasurementEnabled(false);
    clearSelectionRectangle();
    state_->model->setTransformPreview(true);
    try
    {
        state_->transform = std::make_unique<OccTransformPreview>(state_->context,state_->view,objects,
            state_->parts,ids,origin,request,devicePixelRatioF());
        redraw();
        return true;
    }
    catch (const Standard_Failure &error)
    {
        state_->model->setTransformPreview(false);
        if (onError) onError(QString::fromUtf8(error.what()));
        return false;
    }
}
void OccViewport::updateTransformPreview(const ModelTransform &request)
{
    if (!state_->transform) return;
    state_->transform->apply(request);
    redraw();
}
void OccViewport::endTransformPreview()
{
    if (!state_->transform) return;
    state_->transform.reset();
    if (state_->model)
    {
        state_->model->setTransformPreview(false);
        state_->model->selectObjects(state_->selectedObjects);
    }
    update();
}
void OccViewport::fitAll() {
    if (state_->view.IsNull()) return;
    try {
        stopNavigationAnimation();
        clearSelectionRectangle();
        if (!state_->points.empty() || !state_->parts.empty()) state_->view->FitAll(0.05, false);
        redraw();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setCloudEditingEnabled(bool enabled)
{
    state_->cloudEditingEnabled = enabled;
    if (!enabled && !state_->plateCropEnabled)
    {
        state_->rectangle.finish();
        state_->selectionDragged = false;
    }
    updateSelectionCursor(state_->lastMouse);
}

void OccViewport::setCloudSelectionMode(CloudSelectionMode mode)
{
    if (state_->selectionMode == mode) return;
    clearSelectionRectangle();
    state_->selectionMode = mode;
    update();
}

void OccViewport::setCloudSelection(std::shared_ptr<const CloudSelection> selection, bool preserveRectangle)
{
    if (!preserveRectangle)
    {
        clearSelectionRectangle();
        update();
    }
    if (state_->selection == selection) return;
    state_->selection = std::move(selection);
    try
    {
        rebuildCloud();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    catch (const std::exception &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::clearSelectionRectangle()
{
    state_->rectangle.clear();
    state_->selectionDragged = false;
    updateSelectionCursor(state_->lastMouse);
    if (!state_->selectionBand.IsNull() && !state_->context.IsNull())
    {
        state_->context->Erase(state_->selectionBand, false);
    }
}

bool OccViewport::canSelectRectangle() const
{
    return !state_->transform && state_->referencePickMode == ReferencePlanePickMode::None &&
           !state_->measurementEnabled && (state_->plateCropEnabled ||
           (state_->cloudEditingEnabled && state_->cloudVisible));
}

void OccViewport::updateSelectionCursor(const QPoint &current)
{
    if (state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None) return;
    const auto handle = canSelectRectangle()
        ? state_->rectangle.hitTest(current, 6.0 * devicePixelRatioF()) : RectangleHandle::None;
    setCursor(SelectionRectangle::cursor(handle));
}

void OccViewport::drawSelectionRectangle()
{
    if (state_->selectionBand.IsNull())
    {
        state_->selectionBand = new AIS_RubberBand(Quantity_Color(1.0, 0.72, 0.05, Quantity_TOC_sRGB), Aspect_TOL_SOLID,
                                                   2.0 * devicePixelRatioF());
        state_->selectionBand->SetInfiniteState(true);
    }
    const int h = qRound(height() * devicePixelRatioF());
    const auto &rectangle = state_->rectangle.rect();
    state_->selectionBand->SetRectangle(qRound(rectangle.left()), h - qRound(rectangle.bottom()),
                                        qRound(rectangle.right()), h - qRound(rectangle.top()));
    state_->context->Display(state_->selectionBand, 0, -1, false);
    state_->context->Redisplay(state_->selectionBand, false);
    state_->view->Redraw();
}

CloudSelectionRect OccViewport::selectionRectangle() const
{
    const auto &camera = state_->view->Camera();
    const auto matrix = camera->ProjectionMatrix() * camera->OrientationMatrix();
    CloudSelectionRect result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t col = 0; col < 4; ++col)
        {
            result.viewProjection[row * 4 + col] = matrix.GetValue(row, col);
        }
    }
    result.zeroToOneDepth = camera->IsZeroToOneDepth();
    const double w = qRound(width() * devicePixelRatioF());
    const double h = qRound(height() * devicePixelRatioF());
    const auto &rectangle = state_->rectangle.rect();
    result.minX = std::clamp(2.0 * rectangle.left() / w - 1, -1.0, 1.0);
    result.maxX = std::clamp(2.0 * rectangle.right() / w - 1, -1.0, 1.0);
    result.minY = std::clamp(1 - 2.0 * rectangle.bottom() / h, -1.0, 1.0);
    result.maxY = std::clamp(1 - 2.0 * rectangle.top() / h, -1.0, 1.0);
    return result;
}

bool OccViewport::finishSelectionRectangle(const QPoint &current)
{
    if (!state_->rectangle.dragging()) return false;
    const bool editing = state_->rectangle.editing();
    const double ratio = devicePixelRatioF();
    const double dragThreshold = editing ? 0 : QApplication::startDragDistance() * ratio;
    const bool dragged = state_->selectionDragged ||
        (current - state_->leftPress).manhattanLength() > dragThreshold;
    if (dragged)
        state_->rectangle.update(current, {double(qRound(width() * ratio)), double(qRound(height() * ratio))}, 4 * ratio);
    state_->rectangle.finish();
    state_->selectionDragged = false;
    updateSelectionCursor(current);
    if (dragged && state_->rectangle.rect().isValid())
    {
        drawSelectionRectangle();
        const auto rectangle = selectionRectangle();
        if (state_->plateCropEnabled)
        {
            if (onPlateCrop) onPlateCrop(rectangle);
        }
        else if (onCloudSelection)
        {
            onCloudSelection(rectangle, state_->selectionMode, editing);
        }
        return true;
    }
    if (editing) return true; // 单击框内或边缘保留选区，不穿透拾取模型。
    clearSelectionRectangle();
    if (dragged) return true;
    if (!state_->modelPickingEnabled)
    {
        if (state_->selectionMode == CloudSelectionMode::Replace && onCloudSelection)
            onCloudSelection({}, CloudSelectionMode::Clear, false);
        return true;
    }
    return false;
}

void OccViewport::mousePressEvent(QMouseEvent *event)
{
    state_->pressedThicknessLabel = false;
    setFocus(Qt::MouseFocusReason);
    const auto ratio = devicePixelRatioF();
    state_->lastMouse = QPoint(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (state_->view.IsNull()) return;
    try
    {
        stopNavigationAnimation();
        if (event->button() == Qt::LeftButton)
        {
            state_->leftPress = state_->lastMouse;
            state_->context->MoveTo(state_->lastMouse.x(), state_->lastMouse.y(), state_->view, false);
            const bool overCube = isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube);
            if (canSelectRectangle() && !overCube && state_->rectangle.beginEdit(state_->lastMouse, 6 * ratio))
            {
                state_->selectionDragged = false;
                state_->context->ClearDetected(false);
                updateSelectionCursor(state_->lastMouse);
                event->accept();
                update();
                return;
            }
            clearSelectionRectangle();
            if (state_->thicknessEditingEnabled && state_->model &&
                state_->model->labelAt(event->position(), ratio))
            {
                state_->pressedThicknessLabel = true;
                event->accept();
                return; // 标签不能穿透到背后的模型或启动点云框选。
            }
            if (state_->transform)
            {
                state_->transform->startDrag(state_->lastMouse.x(),state_->lastMouse.y());
                event->accept();
                return;
            }
            if (canSelectRectangle() && !overCube)
                state_->rectangle.beginCreate(state_->lastMouse);
            if (state_->measurementEnabled)
            {
                state_->context->ClearDetected(false);
            }
        }
        if (event->button() == Qt::RightButton)
        {
            state_->rightPress = state_->lastMouse;
            state_->rightDragged = false;
            state_->view->StartRotation(state_->lastMouse.x(), state_->lastMouse.y());
        }
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (state_->view.IsNull()) return;
    const auto ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    try
    {
        if (event->buttons().testFlag(Qt::LeftButton) && state_->transform && state_->transform->dragging())
        {
            if (const auto request = state_->transform->drag(current.x(),current.y()); request && onTransformDragged)
                onTransformDragged(*request);
            redraw();
        }
        else if (event->buttons().testFlag(Qt::RightButton))
        {
            state_->rightDragged = state_->rightDragged || (current - state_->rightPress).manhattanLength() >
                                                               QApplication::startDragDistance() * ratio;
            if (state_->rightDragged)
            {
                clearSelectionRectangle();
                state_->view->Rotation(current.x(), current.y());
                redraw();
            }
        }
        else if (event->buttons().testFlag(Qt::MiddleButton))
        {
            clearSelectionRectangle();
            state_->view->Pan(current.x() - state_->lastMouse.x(), state_->lastMouse.y() - current.y());
            redraw();
        }
        else if (event->buttons().testFlag(Qt::LeftButton) && state_->rectangle.dragging())
        {
            const double dragThreshold = state_->rectangle.editing() ? 0 : QApplication::startDragDistance() * ratio;
            if (state_->selectionDragged ||
                (current - state_->leftPress).manhattanLength() > dragThreshold)
            {
                state_->selectionDragged = true;
                state_->rectangle.update(current, {double(qRound(width() * ratio)), double(qRound(height() * ratio))}, 4 * ratio);
                drawSelectionRectangle();
            }
        }
        else if (event->buttons() == Qt::NoButton)
        {
            if (state_->measurementEnabled || state_->referencePickMode == ReferencePlanePickMode::ThreePoints)
            {
                state_->measurementHover = measurementPoint(current);
                redraw();
            }
            else
            {
                detectObject(current);
                updateSelectionCursor(current);
                if (isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube))
                    setCursor(Qt::ArrowCursor);
                else if (canSelectRectangle() && state_->rectangle.hitTest(current, 6 * ratio) != RectangleHandle::None)
                    state_->context->ClearDetected(false);
                state_->view->Redraw();
            }
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    state_->lastMouse = current;
}

void OccViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && state_->pressedThicknessLabel)
    {
        state_->pressedThicknessLabel = false;
        event->accept();
        return;
    }
    if (state_->view.IsNull())
    {
        return;
    }
    const double ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (event->button() == Qt::RightButton)
    {
        if (!state_->rightDragged && (state_->pickMode==ModelPickMode::Body || state_->transform || state_->cloudEditingEnabled || state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None || (hasEditContextMenu && hasEditContextMenu())) && onCloudContextMenu)
        {
            if (state_->selectedObjects.empty() && state_->modelPickingEnabled && state_->pickMode==ModelPickMode::Body && state_->model) {
                detectObject(current);
                const auto hit=state_->model->detectedSelection(state_->context->DetectedOwner());
                if (!hit.partId.empty()) { setSelectedObjects({hit});if(onModelObjectsSelected) onModelObjectsSelected(state_->selectedObjects); }
            }
            onCloudContextMenu(event->globalPosition().toPoint());
        }
        return;
    }
    if (event->button() != Qt::LeftButton)
    {
        return;
    }
    try
    {
        if (state_->transform)
        {
            state_->transform->finishDrag();
            redraw();
            event->accept();
            return;
        }
        if (finishSelectionRectangle(current))
        {
            event->accept();
            return;
        }
        if ((current - state_->leftPress).manhattanLength() > QApplication::startDragDistance() * ratio)
        {
            return;
        }
        detectObject(current);
        const auto owner = Handle(AIS_ViewCubeOwner)::DownCast(state_->context->DetectedOwner());
        if (!owner.IsNull() && isDetectedInteractive(owner, state_->viewCube))
        {
            clearSelectionRectangle();
            state_->viewCube->StartAnimation(owner);
            state_->navigationTimer->start();
            event->accept();
            return;
        }
        if (state_->referencePickMode != ReferencePlanePickMode::None)
        {
            pickReference(current);
            event->accept();
            return;
        }
        if (state_->measurementEnabled)
        {
            state_->context->ClearDetected(false);
            state_->measurementHover = measurementPoint(current);
            if (state_->measurementHover && state_->measurement.pick(state_->measurementHover->position,
                                                                    state_->measurementHover->snapped))
            {
                if (const auto result = state_->measurement.result(); result && onMeasurementCompleted)
                {
                    onMeasurementCompleted(*result);
                }
            }
            redraw();
            event->accept();
            return;
        }
        if (state_->modelPickingEnabled && state_->model)
        {
            const auto selectedOwner = state_->context->DetectedOwner();
            const auto selected = state_->model->detectedSelection(selectedOwner);
            const auto modifiers = event->modifiers();
            const bool toggle = modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::ControlModifier);
            if (selected.partId.empty() && state_->cloudEditingEnabled &&
                !toggle && state_->selectionMode == CloudSelectionMode::Replace && onCloudSelection)
                onCloudSelection({}, CloudSelectionMode::Clear, false);
            setSelectedObjects(updateModelSelection(state_->selectedObjects, selected, state_->selectionMode, toggle));
            if (onModelObjectsSelected) onModelObjectsSelected(state_->selectedObjects);
            redraw();
            event->accept();
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        if (onEditEscape && onEditEscape()) { event->accept(); return; }
        if (state_->transform)
        {
            if (onTransformCancelled) onTransformCancelled();
            event->accept();
            return;
        }
        if (state_->referencePickMode != ReferencePlanePickMode::None)
        {
            if (onReferenceCancelled) onReferenceCancelled();
            event->accept();
            return;
        }
        if (state_->measurementEnabled)
        {
            setMeasurementEnabled(false);
            event->accept();
            return;
        }
        clearSelectionRectangle();
        setSelectedPart({});
        if (onModelObjectsSelected) onModelObjectsSelected({});
        if (state_->cloudEditingEnabled && onCloudSelection)
        {
            onCloudSelection({}, CloudSelectionMode::Clear, false);
        }
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OccViewport::leaveEvent(QEvent *event)
{
    state_->measurementHover.reset();
    QWidget::leaveEvent(event);
    if (!state_->context.IsNull())
    {
        state_->context->ClearDetected(false);
        update();
    }
}

void OccViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && canSelectRectangle())
    {
        const double ratio = devicePixelRatioF();
        const QPoint pixel(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
        if (!state_->context.IsNull() && !isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube) &&
            state_->rectangle.beginEdit(pixel, 6 * ratio))
        {
            state_->leftPress = pixel;
            state_->selectionDragged = false;
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && state_->thicknessEditingEnabled && state_->model)
    {
        const double ratio = devicePixelRatioF();
        if (const auto label = state_->model->labelAt(event->position(), ratio))
        {
            clearSelectionRectangle();
            state_->pressedThicknessLabel = true;
            if (onThicknessLabelEditRequested && onThicknessLabelEditRequested(label->partId))
            {
                const QPoint center(qRound(label->pixelRect.center().x() / ratio),
                                    qRound(label->pixelRect.center().y() / ratio));
                state_->thicknessEditor->open(label->partId, label->thicknessMm, mapToGlobal(center));
            }
            event->accept();
            return;
        }
    }
    if (state_->transform || state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None)
    {
        event->accept();
        return;
    }
    if (!state_->context.IsNull())
    {
        const double ratio = devicePixelRatioF();
        state_->context->MoveTo(qRound(event->position().x() * ratio),
                               qRound(event->position().y() * ratio), state_->view, false);
        if (isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube))
        {
            event->accept();
            return;
        }
    }
    fitAll();
}

void OccViewport::wheelEvent(QWheelEvent* event) {
    if (state_->view.IsNull()) return;
    try {
        stopNavigationAnimation();
        clearSelectionRectangle();
        const auto steps = std::clamp(event->angleDelta().y() / 120.0, -5.0, 5.0);
        state_->view->SetZoom(std::pow(1.15, steps));
        redraw();
        event->accept();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

} // namespace reverse::workbench
