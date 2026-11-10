/* 文件描述：管理原生 OCCT 窗口生命周期、点云和模型显示更新，不执行建模算法。 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QEvent>
#include <QGuiApplication>
#include <QLabel>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>

#include <Graphic3d_ArrayOfPoints.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>

#if defined(_WIN32)
#include <WNT_Window.hxx>
#elif defined(__linux__)
#include <Xw_Window.hxx>
#endif

#include "geometry/PlateHoles.h"
#include "workbench/ModelFocus.h"
#include "workbench/OccSelectionBand.h"
#include "workbench/OccViewportState.h"
#include "workbench/ThicknessLabelEditor.h"

namespace reverse::workbench
{
OccViewport::OccViewport(QWidget *parent) : QWidget(parent), state_(std::make_unique<State>())
{
    setMinimumSize(400, 300);
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setToolTip(QStringLiteral(
        "双击板厚标签修改；左键按点/线/面/体拾取模型；Shift 追加/取消；点模式框选点云；右键进入框选切割后拖框切除板件端部；右键旋转，中键平移，Esc 清空"));
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

OccViewport::~OccViewport()
{
    release();
}

void OccViewport::release() noexcept
{
    try
    {
        stopNavigationAnimation();
        state_->holeGuide.clear();
        state_->holePreview.clear();
        state_->transform.reset();
        state_->referencePresentation.reset();
        state_->measurementPresentation.reset();
        if (!state_->context.IsNull())
            state_->context->RemoveAll(false);
        state_->focusMarker.Nullify();
        state_->focusLabel.Nullify();
        state_->focusPartId.clear();
        state_->viewCube.Nullify();
        state_->selectionBand.Nullify();
        state_->cloud.Nullify();
        state_->model.reset();
        state_->context.Nullify();
        if (!state_->view.IsNull())
            state_->view->Remove();
    }
    catch (...)
    {
        // 析构期间不能传播图形驱动异常。
    }
    state_->view.Nullify();
    state_->viewer.Nullify();
    state_->driver.Nullify();
    state_->display.Nullify();
}

void OccViewport::fail(const QString &message)
{
    release();
    state_->failed = true;
    setAttribute(Qt::WA_PaintOnScreen, false);
    setAttribute(Qt::WA_NoSystemBackground, false);
    if (!state_->failureLabel)
    {
        state_->failureLabel = new QLabel(this);
        state_->failureLabel->setAlignment(Qt::AlignCenter);
        state_->failureLabel->setWordWrap(true);
        state_->failureLabel->setMargin(24);
        state_->failureLabel->setStyleSheet(QStringLiteral("background:#18212d;color:#f2d5c4;"));
    }
    state_->failureLabel->setGeometry(rect());
    state_->failureLabel->setText(QStringLiteral("三维视图初始化或绘制失败\n%1\n文件处理服务仍可使用。").arg(message));
    state_->failureLabel->show();
    if (onError)
        onError(message);
}

void OccViewport::initialize()
{
    if (!state_->view.IsNull() || state_->failed || state_->initializing || !isVisible())
        return;
    state_->initializing = true;
    try
    {
#if defined(__linux__)
        if (QGuiApplication::platformName() != QStringLiteral("xcb"))
        {
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
        if (!window->IsMapped())
            window->Map();
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
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    catch (const std::exception &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    state_->initializing = false;
}

QPaintEngine *OccViewport::paintEngine() const
{
    return state_->failed ? QWidget::paintEngine() : nullptr;
}

bool OccViewport::event(QEvent *event)
{
    if (event->type() == QEvent::WinIdChange && !state_->initializing && !state_->view.IsNull())
    {
        release();
        QTimer::singleShot(0, this, [this] { initialize(); });
    }
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (event->type() == QEvent::DevicePixelRatioChange)
    {
        // 等 Qt 更新原生窗口的物理像素尺寸后，在绘制入口同步 OCCT 视口。
        QTimer::singleShot(0, this, [this] { update(); });
    }
#endif
    return QWidget::event(event);
}

void OccViewport::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // 首次 showEvent 可能早于 splitter/父窗口最终布局与原生 HWND 的尺寸提交。
    // 延迟到本轮事件完成后绑定视图，避免使用构造时 400×300 的最小尺寸。
    QTimer::singleShot(0, this,
                       [this]
                       {
                           initialize();
                           update();
                       });
}

void OccViewport::paintEvent(QPaintEvent *)
{
    if (state_->view.IsNull())
        return;
    try
    {
        // 原生 OCCT 绘制不经过 Qt backing store；每次绘制都读取 HWND 当前客户区
        // 同步 OpenGL viewport 和相机纵横比，不能只依赖可能提前到达的 resizeEvent。
        state_->view->MustBeResized();
        updateViewCubeScale();
        redraw();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::resizeEvent(QResizeEvent *event)
{
    state_->thicknessEditor->hide();
    QWidget::resizeEvent(event);
    if (state_->failureLabel)
        state_->failureLabel->setGeometry(rect());
    if (!state_->view.IsNull() && isVisible() && event->size().isValid())
    {
        try
        {
            clearSelectionRectangle();
            state_->view->MustBeResized();
            redraw();
            // 缩小、分隔条拖动以及原生窗口延迟调整后仍需要一次 Qt 绘制事件。
            update();
        }
        catch (const Standard_Failure &error)
        {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::rebuildCloud()
{
    if (state_->context.IsNull())
        return;
    if (!state_->cloud.IsNull())
        state_->context->Remove(state_->cloud, false);
    state_->cloud.Nullify();
    if (state_->points.empty())
        return;
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
    if (state_->cloudVisible)
        state_->context->Display(state_->cloud, 0, -1, false);
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
void OccViewport::redraw()
{
    // 工具在主窗口构造/销毁阶段也会清理预览，此时 OpenGL 视图可能尚未创建。
    if (state_->view.IsNull())
        return;
    if ((state_->measurementEnabled || (state_->referencePickMode == ReferencePlanePickMode::ThreePoints ||
                                        state_->referencePickMode == ReferencePlanePickMode::OrderedPoints)) &&
        state_->measurementPresentation)
    {
        const double ratio = devicePixelRatioF();
        state_->measurementPresentation->update(state_->measurement, state_->measurementHover, qRound(width() * ratio),
                                                qRound(height() * ratio), ratio);
    }
    if (state_->model)
    {
        const double ratio = devicePixelRatioF();
        state_->model->updateLabels(qRound(width() * ratio), qRound(height() * ratio), ratio);
    }
    updateFocusLocator();
    updateEndCapEdges();
    state_->view->Redraw();
}
void OccViewport::setPointCloud(std::vector<CloudDisplayPoint> points, std::shared_ptr<const CloudSelection> selection)
{
    setMeasurementEnabled(false);
    state_->points = std::move(points);
    try
    {
        state_->selection = std::move(selection);
        clearSelectionRectangle();
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
    state_->focusPartId.clear();
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
void OccViewport::setCloudVisible(bool visible)
{
    if (!visible)
    {
        setMeasurementEnabled(false);
    }
    state_->cloudVisible = visible;
    if (!visible)
    {
        clearSelectionRectangle();
    }
    if (!state_->context.IsNull() && !state_->cloud.IsNull())
    {
        try
        {
            if (visible)
                state_->context->Display(state_->cloud, 0, -1, false);
            else
                state_->context->Erase(state_->cloud, false);
            updateMeasurementPicking();
            update();
        }
        catch (const Standard_Failure &error)
        {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::setModelWireframe(bool wireframe)
{
    state_->modelWireframe = wireframe;
    try
    {
        if (state_->model)
            state_->model->setWireframe(wireframe);
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
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
        if (onModelObjectsSelected)
            onModelObjectsSelected({});
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
    state_->thicknessEditor->hide();
    setModelVisible(state_->modelVisible);
}

} // namespace reverse::workbench
