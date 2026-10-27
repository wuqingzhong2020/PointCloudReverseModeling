/*
 * 文件描述：
 * 使用原生 QWidget 窗口承载 OCCT/AIS，显示点云采样和模型三角化结果，
 * 实现缩放、平移、旋转及窗口释放；不保存模型业务状态或执行建模算法。
 */
#include "reverse/workbench/OccViewport.h"

#include <AIS_InteractiveContext.hxx>
#include <AIS_PointCloud.hxx>
#include <AIS_Triangulation.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Poly_Triangle.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <Quantity_TypeOfColor.hxx>
#include <Standard_Failure.hxx>
#include <V3d_View.hxx>
#include <V3d_TypeOfOrientation.hxx>
#include <V3d_Viewer.hxx>

#if defined(_WIN32)
#include <WNT_Window.hxx>
#elif defined(__linux__)
#include <Xw_Window.hxx>
#endif

#include <QGuiApplication>
#include <QEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
#include <QTimer>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace reverse::workbench {

struct OccViewport::State {
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_PointCloud) cloud;
    Handle(AIS_Triangulation) model;
    std::vector<Vertex> points;
    std::vector<Vertex> vertices;
    std::vector<Triangle> triangles;
    QPoint lastMouse;
    bool cloudVisible = true;
    bool modelVisible = true;
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
    setToolTip(QStringLiteral("右键拖动旋转，中键拖动平移，滚轮缩放，双击适合视图"));
}

OccViewport::~OccViewport() { release(); }

void OccViewport::release() noexcept {
    try {
        if (!state_->context.IsNull()) state_->context->RemoveAll(false);
        state_->cloud.Nullify();
        state_->model.Nullify();
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
        state_->view->SetBackgroundColor(Quantity_Color(0.08, 0.11, 0.16, Quantity_TOC_RGB));
        state_->view->SetProj(V3d_XposYnegZpos);
        state_->context = new AIS_InteractiveContext(state_->viewer);
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
        state_->view->Redraw();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (state_->failureLabel) state_->failureLabel->setGeometry(rect());
    if (!state_->view.IsNull() && isVisible() && event->size().isValid()) {
        try {
            state_->view->MustBeResized();
            state_->view->Redraw();
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
    Handle(Graphic3d_ArrayOfPoints) points = new Graphic3d_ArrayOfPoints(static_cast<int>(state_->points.size()));
    for (const auto& point : state_->points) points->AddVertex(gp_Pnt(point[0], point[1], point[2]));
    state_->cloud = new AIS_PointCloud();
    state_->cloud->SetDisplayMode(AIS_PointCloud::DM_Points);
    state_->cloud->SetPoints(points);
    state_->cloud->SetColor(Quantity_Color(0.55, 0.78, 0.94, Quantity_TOC_RGB));
    if (state_->cloudVisible) state_->context->Display(state_->cloud, false);
}

void OccViewport::rebuildMesh() {
    if (state_->context.IsNull()) return;
    if (!state_->model.IsNull()) state_->context->Remove(state_->model, false);
    state_->model.Nullify();
    if (state_->vertices.empty() || state_->triangles.empty()) return;
    const auto maxIndex = static_cast<std::size_t>(std::numeric_limits<int>::max());
    if (state_->vertices.size() > maxIndex || state_->triangles.size() > maxIndex)
        throw std::runtime_error("Display mesh exceeds the OCCT array limit.");
    Handle(Poly_Triangulation) mesh = new Poly_Triangulation(
        static_cast<int>(state_->vertices.size()), static_cast<int>(state_->triangles.size()), false);
    for (std::size_t i = 0; i < state_->vertices.size(); ++i) {
        const auto& p = state_->vertices[i];
        mesh->SetNode(static_cast<int>(i + 1), gp_Pnt(p[0], p[1], p[2]));
    }
    for (std::size_t i = 0; i < state_->triangles.size(); ++i) {
        const auto& t = state_->triangles[i];
        if (t[0] >= state_->vertices.size() || t[1] >= state_->vertices.size() || t[2] >= state_->vertices.size())
            throw std::runtime_error("Display mesh contains an invalid triangle index.");
        mesh->SetTriangle(static_cast<int>(i + 1), Poly_Triangle(
            static_cast<int>(t[0] + 1), static_cast<int>(t[1] + 1), static_cast<int>(t[2] + 1)));
    }
    mesh->ComputeNormals();
    state_->model = new AIS_Triangulation(mesh);
    // AIS_Triangulation 的实体三角网格只在模式 0 绘制；AIS_Shape 的着色模式 1
    // 不适用于它。将模式绑定到对象，确保隐藏后再次显示仍使用同一呈现。
    state_->model->SetDisplayMode(0);
    state_->model->SetColor(Quantity_Color(0.95, 0.66, 0.25, Quantity_TOC_RGB));
    state_->model->SetTransparency(0.25);
    if (state_->modelVisible) state_->context->Display(state_->model, false);
}

void OccViewport::setPointCloud(const std::vector<Vertex>& points) {
    state_->points = points;
    try {
        rebuildCloud();
        update();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setMesh(const std::vector<Vertex>& vertices, const std::vector<Triangle>& triangles) {
    state_->vertices = vertices;
    state_->triangles = triangles;
    try {
        rebuildMesh();
        update();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    } catch (const std::exception& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setCloudVisible(bool visible) {
    state_->cloudVisible = visible;
    if (!state_->context.IsNull() && !state_->cloud.IsNull()) {
        try {
            if (visible) state_->context->Display(state_->cloud, false);
            else state_->context->Erase(state_->cloud, false);
            update();
        } catch (const Standard_Failure& error) {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::setModelVisible(bool visible) {
    state_->modelVisible = visible;
    if (!state_->context.IsNull() && !state_->model.IsNull()) {
        try {
            if (visible) state_->context->Display(state_->model, false);
            else state_->context->Erase(state_->model, false);
            update();
        } catch (const Standard_Failure& error) {
            fail(QString::fromUtf8(error.what()));
        }
    }
}

void OccViewport::fitAll() {
    if (state_->view.IsNull()) return;
    try {
        if (!state_->points.empty() || !state_->vertices.empty()) state_->view->FitAll(0.05, false);
        state_->view->Redraw();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::mousePressEvent(QMouseEvent* event) {
    const auto ratio = devicePixelRatioF();
    state_->lastMouse = QPoint(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (state_->view.IsNull()) return;
    try {
        if (event->button() == Qt::RightButton)
            state_->view->StartRotation(state_->lastMouse.x(), state_->lastMouse.y());
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::mouseMoveEvent(QMouseEvent* event) {
    if (state_->view.IsNull()) return;
    const auto ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    try {
        if (event->buttons().testFlag(Qt::RightButton)) state_->view->Rotation(current.x(), current.y());
        else if (event->buttons().testFlag(Qt::MiddleButton))
            state_->view->Pan(current.x() - state_->lastMouse.x(), state_->lastMouse.y() - current.y());
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
    state_->lastMouse = current;
}

void OccViewport::mouseDoubleClickEvent(QMouseEvent*) { fitAll(); }

void OccViewport::wheelEvent(QWheelEvent* event) {
    if (state_->view.IsNull()) return;
    try {
        const auto steps = std::clamp(event->angleDelta().y() / 120.0, -5.0, 5.0);
        state_->view->SetZoom(std::pow(1.15, steps));
        state_->view->Redraw();
        event->accept();
    } catch (const Standard_Failure& error) {
        fail(QString::fromUtf8(error.what()));
    }
}

} // namespace reverse::workbench
