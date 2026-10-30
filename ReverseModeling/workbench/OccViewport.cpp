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
#include <AIS_RubberBand.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Prs3d_Drawer.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_TypeOfColor.hxx>
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
#include "workbench/OccPicking.h"
#include "workbench/OccViewport.h"

namespace reverse::workbench {

struct OccViewport::State {
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_PointCloud) cloud;
    Handle(AIS_ViewCube) viewCube;
    Handle(AIS_RubberBand) selectionBand;
    std::shared_ptr<const CloudSelection> selection;
    CloudSelectionMode selectionMode{CloudSelectionMode::Replace};
    bool cloudEditingEnabled{};
    bool modelPickingEnabled{};
    std::string selectedPart;
    bool selecting{};
    bool rightDragged{};
    QPoint rightPress;
    QTimer *navigationTimer{};
    double cubePixelRatio{};
    std::unique_ptr<OccModelPresentation> model;
    std::vector<CloudDisplayPoint> points;
    std::vector<ModelDisplayPart> parts;
    QPoint lastMouse;
    QPoint leftPress;
    bool cloudVisible = true;
    bool cloudHeightColors = true;
    bool modelVisible = true;
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
    setToolTip(QStringLiteral("可选：体时左键选部件；可选：点时左键框选点云；右键拖动旋转，中键平移，Esc 清空选择"));
    state_->navigationTimer = new QTimer(this);
    state_->navigationTimer->setInterval(16);
    connect(state_->navigationTimer, &QTimer::timeout, this, &OccViewport::advanceNavigationAnimation);
}

OccViewport::~OccViewport() { release(); }

void OccViewport::release() noexcept {
    try {
        stopNavigationAnimation();
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
        state_->view->SetBackgroundColor(Quantity_Color(0.031, 0.082, 0.129, Quantity_TOC_sRGB));
        state_->view->SetProj(V3d_XposYnegZpos);
        state_->context = new AIS_InteractiveContext(state_->viewer);
        state_->context->SelectionStyle()->SetColor(Quantity_Color(0.10, 0.66, 0.95, Quantity_TOC_sRGB));
        state_->context->SelectionStyle()->SetDisplayMode(0);
        state_->context->HighlightStyle()->SetColor(Quantity_Color(0.30, 0.82, 0.90, Quantity_TOC_sRGB));
        state_->context->HighlightStyle()->SetDisplayMode(0);
        createViewCube();
        state_->model = std::make_unique<OccModelPresentation>(state_->context, state_->view);
        rebuildCloud();
        rebuildMesh();
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
    state_->cloud->SetColor(Quantity_Color(0.18, 0.56, 0.61, Quantity_TOC_sRGB));
    if (state_->cloudVisible) state_->context->Display(state_->cloud, 0, -1, false);
}

void OccViewport::rebuildMesh()
{
    if (!state_->model)
    {
        return;
    }
    state_->model->rebuild(state_->parts);
    state_->model->setVisible(state_->modelVisible, state_->thicknessVisible);
    state_->model->setSelectable(state_->modelPickingEnabled);
    state_->model->selectPart(state_->selectedPart);
}

void OccViewport::redraw()
{
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
    state_->selectedPart.clear();
    state_->parts = std::move(parts);
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

void OccViewport::setModelPickingEnabled(bool enabled)
{
    if (state_->modelPickingEnabled == enabled) return;
    state_->modelPickingEnabled = enabled;
    try
    {
        if (state_->model) state_->model->setSelectable(enabled);
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setSelectedPart(const std::string &id)
{
    const bool exists = state_->modelVisible && std::any_of(state_->parts.begin(), state_->parts.end(),
        [&id](const auto &part) { return part.id == id; });
    state_->selectedPart = exists ? id : std::string{};
    try
    {
        if (state_->model) state_->model->selectPart(state_->selectedPart);
        update();
    }
    catch (const Standard_Failure &error) { fail(QString::fromUtf8(error.what())); }
}

void OccViewport::setCloudVisible(bool visible) {
    state_->cloudVisible = visible;
    if (!visible)
    {
        clearSelectionRectangle();
    }
    if (!state_->context.IsNull() && !state_->cloud.IsNull()) {
        try {
            if (visible) state_->context->Display(state_->cloud, 0, -1, false);
            else state_->context->Erase(state_->cloud, false);
            update();
        } catch (const Standard_Failure& error) {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::setModelVisible(bool visible)
{
    state_->modelVisible = visible;
    if (!visible && !state_->selectedPart.empty())
    {
        setSelectedPart({});
        if (onModelPartSelected) onModelPartSelected({});
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

void OccViewport::setThicknessVisible(bool visible)
{
    state_->thicknessVisible = visible;
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
        fitAll();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
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
    if (!enabled)
    {
        state_->selecting = false;
    }
}

void OccViewport::setCloudSelectionMode(CloudSelectionMode mode)
{
    state_->selectionMode = mode;
}

void OccViewport::setCloudSelection(std::shared_ptr<const CloudSelection> selection)
{
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
    state_->selecting = false;
    if (!state_->selectionBand.IsNull() && !state_->context.IsNull())
    {
        state_->context->Erase(state_->selectionBand, false);
    }
}

void OccViewport::drawSelectionRectangle(const QPoint &current)
{
    if (state_->selectionBand.IsNull())
    {
        state_->selectionBand = new AIS_RubberBand(Quantity_Color(1.0, 0.72, 0.05, Quantity_TOC_sRGB), Aspect_TOL_SOLID,
                                                   2.0 * devicePixelRatioF());
        state_->selectionBand->SetInfiniteState(true);
    }
    const int w = qRound(width() * devicePixelRatioF());
    const int h = qRound(height() * devicePixelRatioF());
    const auto start = state_->leftPress;
    state_->selectionBand->SetRectangle(
        std::clamp(std::min(start.x(), current.x()), 0, w), h - std::clamp(std::max(start.y(), current.y()), 0, h),
        std::clamp(std::max(start.x(), current.x()), 0, w), h - std::clamp(std::min(start.y(), current.y()), 0, h));
    state_->context->Display(state_->selectionBand, 0, -1, false);
    state_->context->Redisplay(state_->selectionBand, false);
    state_->view->Redraw();
}

CloudSelectionRect OccViewport::selectionRectangle(const QPoint &current) const
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
    const auto start = state_->leftPress;
    result.minX = std::clamp(2.0 * std::min(start.x(), current.x()) / w - 1, -1.0, 1.0);
    result.maxX = std::clamp(2.0 * std::max(start.x(), current.x()) / w - 1, -1.0, 1.0);
    result.minY = std::clamp(1 - 2.0 * std::max(start.y(), current.y()) / h, -1.0, 1.0);
    result.maxY = std::clamp(1 - 2.0 * std::min(start.y(), current.y()) / h, -1.0, 1.0);
    return result;
}

void OccViewport::mousePressEvent(QMouseEvent *event)
{
    setFocus(Qt::MouseFocusReason);
    const auto ratio = devicePixelRatioF();
    state_->lastMouse = QPoint(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (state_->view.IsNull()) return;
    try
    {
        stopNavigationAnimation();
        if (event->button() == Qt::LeftButton)
        {
            clearSelectionRectangle();
            state_->leftPress = state_->lastMouse;
            state_->context->MoveTo(state_->lastMouse.x(), state_->lastMouse.y(), state_->view, false);
            state_->selecting = state_->cloudEditingEnabled && state_->cloudVisible &&
                !isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube);
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
        if (event->buttons().testFlag(Qt::RightButton))
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
        else if (event->buttons().testFlag(Qt::LeftButton) && state_->selecting)
        {
            if ((current - state_->leftPress).manhattanLength() > QApplication::startDragDistance() * ratio)
            {
                drawSelectionRectangle(current);
            }
        }
        else if (event->buttons() == Qt::NoButton)
        {
            state_->context->MoveTo(current.x(), current.y(), state_->view, false);
            state_->view->Redraw();
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
    if (state_->view.IsNull())
    {
        return;
    }
    const double ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (event->button() == Qt::RightButton)
    {
        if (!state_->rightDragged && state_->cloudEditingEnabled && onCloudContextMenu)
        {
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
        if (state_->selecting)
        {
            state_->selecting = false;
            if ((current - state_->leftPress).manhattanLength() > QApplication::startDragDistance() * ratio)
            {
                const auto rectangle = selectionRectangle(current);
                if (rectangle.minX < rectangle.maxX && rectangle.minY < rectangle.maxY && onCloudSelection)
                {
                    drawSelectionRectangle(current);
                    onCloudSelection(rectangle, state_->selectionMode);
                }
                return;
            }
            // 单击空白清空当前选区；加选/减选模式的小幅点击不改变已有选区。
            if (state_->selectionMode == CloudSelectionMode::Replace && onCloudSelection)
            {
                onCloudSelection({}, CloudSelectionMode::Clear);
            }
            return;
        }
        if ((current - state_->leftPress).manhattanLength() > QApplication::startDragDistance() * ratio)
        {
            return;
        }
        state_->context->MoveTo(current.x(), current.y(), state_->view, false);
        const auto owner = Handle(AIS_ViewCubeOwner)::DownCast(state_->context->DetectedOwner());
        if (!owner.IsNull() && isDetectedInteractive(owner, state_->viewCube))
        {
            clearSelectionRectangle();
            state_->viewCube->StartAnimation(owner);
            state_->navigationTimer->start();
            event->accept();
            return;
        }
        if (state_->modelPickingEnabled && state_->model)
        {
            const auto id = state_->model->detectedPartId(state_->context->DetectedOwner());
            setSelectedPart(id);
            if (onModelPartSelected) onModelPartSelected(id);
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
        clearSelectionRectangle();
        setSelectedPart({});
        if (onModelPartSelected) onModelPartSelected({});
        if (state_->cloudEditingEnabled && onCloudSelection)
        {
            onCloudSelection({}, CloudSelectionMode::Clear);
        }
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OccViewport::leaveEvent(QEvent *event)
{
    QWidget::leaveEvent(event);
    if (!state_->context.IsNull())
    {
        state_->context->ClearDetected(false);
        update();
    }
}

void OccViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
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
