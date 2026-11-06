/*
 * 文件描述：
 * 以 ADS 固定中央视图并注册左右/底部工具窗口，复用 toggleViewAction 同步窗口菜单。
 * 保存版本化布局，恢复失败回退默认布局，并找回落在已断开显示器上的浮动窗口。
 */
#include <QAction>
#include <QGuiApplication>
#include <QMainWindow>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QSplitter>

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include "workbench/DockWorkspace.h"
#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
constexpr int layoutVersion = 4;

void recoverWindow(QWidget *window)
{
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty())
    {
        return;
    }
    bool reachable = false;
    const QRect titleArea(window->frameGeometry().topLeft(), QSize(window->width(), 32));
    for (auto *screen : screens)
    {
        if (screen->availableGeometry().intersected(titleArea).width() >= 80)
        {
            reachable = true;
            break;
        }
    }
    if (!reachable)
    {
        const QRect available = QGuiApplication::primaryScreen()->availableGeometry();
        window->resize(window->size().boundedTo(available.size() - QSize(40, 60)));
        window->move(available.topLeft() + QPoint(20, 30));
    }
}
} // namespace

DockWorkspace::DockWorkspace(QMainWindow *window, QWidget *viewport)
    : QObject(window),
      m_window(window),
      m_manager(nullptr),
      m_menu(new QMenu(QStringLiteral("窗口"), window))
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    // 面板标题栏不显示右侧工具按钮；显隐仍由标签关闭按钮和“窗口”菜单控制。
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    m_manager = new ads::CDockManager(window);
    m_manager->setObjectName(QStringLiteral("workspace.docks"));
    m_manager->setStyleSheet(QString()); // 统一使用应用 QSS，不叠加 ADS 默认浅色样式。
    auto *central = new ads::CDockWidget(m_manager, QStringLiteral("三维视图"));
    central->setObjectName(QStringLiteral("dock.viewport"));
    central->setFeature(ads::CDockWidget::NoTab, true);
    viewport->setMinimumSize(1, 1);
    central->setWidget(viewport, ads::CDockWidget::ForceNoScrollArea);
    m_manager->setCentralWidget(central);
    m_menu->setObjectName(QStringLiteral("menu.windows"));
    m_menu->setIcon(WorkbenchTheme::icon(QStringLiteral("window")));
}

void DockWorkspace::addPanel(const QString &id, const QString &title, QWidget *content, DockPosition position,
                            const QString &belowPanel)
{
    auto *dock = new ads::CDockWidget(m_manager, title);
    dock->setObjectName(id);
    content->setMinimumSize(content->minimumSize().expandedTo(
        QSize(position == DockPosition::Bottom ? 180 : 340, position == DockPosition::Bottom ? 150 : 200)));
    dock->setWidget(content, ads::CDockWidget::ForceNoScrollArea);
    dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
    if (m_panels.contains(belowPanel))
    {
        m_manager->addDockWidget(ads::BottomDockWidgetArea, dock, m_panels.value(belowPanel)->dockAreaWidget());
    }
    else if (m_anchors.contains(position))
    {
        m_manager->addDockWidgetTabToArea(dock, m_anchors.value(position)->dockAreaWidget());
    }
    else
    {
        const auto area = position == DockPosition::Left    ? ads::LeftDockWidgetArea
                          : position == DockPosition::Right ? ads::RightDockWidgetArea
                                                            : ads::BottomDockWidgetArea;
        // 底部工具页只分割中央视图，左右侧栏保持贯穿工作区全高。
        auto *relativeArea = position == DockPosition::Bottom ? m_manager->centralWidget()->dockAreaWidget() : nullptr;
        m_manager->addDockWidget(area, dock, relativeArea);
        m_anchors.insert(position, dock);
    }
    dock->toggleViewAction()->setObjectName(QStringLiteral("window.") + id);
    setActionHelp(dock->toggleViewAction(), QStringLiteral("显示或隐藏“%1”窗口。").arg(title),
                  QStringLiteral("点击菜单项切换，勾选表示窗口已显示；关闭后可从此处重新打开。"));
    m_menu->addAction(dock->toggleViewAction());
    m_panels.insert(id, dock);
}

void DockWorkspace::initializeLayout()
{
    hidePanels();
    showPanel(QStringLiteral("dock.reconstruction"));
    showPanel(QStringLiteral("dock.operation"));
    showPanel(QStringLiteral("dock.modelTree"));
    showPanel(QStringLiteral("dock.properties"));
    showPanel(QStringLiteral("dock.commands"));
    applyDefaultSizes();
    m_defaultState = m_manager->saveState(layoutVersion);
    m_menu->addSeparator();
    auto *hide = m_menu->addAction(QStringLiteral("隐藏所有辅助窗口"));
    hide->setObjectName(QStringLiteral("window.hideAll"));
    setActionHelp(hide, QStringLiteral("隐藏辅助面板，保留三维视图和顶部工具栏。"),
                  QStringLiteral("点击隐藏；之后通过“窗口”菜单重新显示所需面板。"));
    connect(hide, &QAction::triggered, this,
            [this]
            {
                hidePanels();
            });
    auto *reset = m_menu->addAction(QStringLiteral("恢复默认布局"));
    reset->setObjectName(QStringLiteral("window.resetLayout"));
    setActionHelp(reset, QStringLiteral("恢复左侧操作面板、底部命令窗口和右侧模型树/属性布局。"),
                  QStringLiteral("点击恢复窗口排列；不会重置点云、模型、参数或日志。"));
    connect(reset, &QAction::triggered, this,
            [this]
            {
                resetLayout();
            });
}

void DockWorkspace::restoreLayout()
{
    QSettings settings;
    // 新布局首次启用只继承旧主窗大小，避免旧停靠状态覆盖新的初始排列。
    const auto previousGeometry = settings.value(QStringLiteral("ui/layout/v3/geometry"),
                                                 settings.value(QStringLiteral("ui/layout/v2/geometry"))).toByteArray();
    settings.beginGroup(QStringLiteral("ui/layout/v4"));
    const auto geometry = settings.value(QStringLiteral("geometry"), previousGeometry).toByteArray();
    if (!geometry.isEmpty())
    {
        m_window->restoreGeometry(geometry);
    }
    const auto state = settings.value(QStringLiteral("docks")).toByteArray();
    if (state.isEmpty() || !m_manager->restoreState(state, layoutVersion))
    {
        resetLayout();
    }
    recoverWindow(m_window);
    for (auto *floating : m_manager->floatingWidgets())
    {
        recoverWindow(floating);
    }
}

void DockWorkspace::saveLayout() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui/layout/v4"));
    settings.setValue(QStringLiteral("geometry"), m_window->saveGeometry());
    settings.setValue(QStringLiteral("docks"), m_manager->saveState(layoutVersion));
}

void DockWorkspace::resetLayout()
{
    if (!m_defaultState.isEmpty())
    {
        if (!m_manager->restoreState(m_defaultState, layoutVersion))
        {
            hidePanels();
        }
        else
        {
            applyDefaultSizes();
        }
    }
}

void DockWorkspace::applyDefaultSizes()
{
    const int width = m_manager->width();
    const int height = m_manager->height();
    // 外层三列：左侧工具页、中央视图/命令、右侧模型树/属性。
    if (auto *left = m_anchors.value(DockPosition::Left))
    {
        auto *columns = qobject_cast<QSplitter *>(left->dockAreaWidget()->parentWidget());
        if (columns && columns->orientation() == Qt::Horizontal && columns->count() == 3)
        {
            const int centerWidth = qMax(1, width - 680 - 2 * columns->handleWidth());
            columns->setSizes({340, centerWidth, 340});
        }
    }
    if (auto *right = m_anchors.value(DockPosition::Right))
    {
        m_manager->setSplitterSizes(right->dockAreaWidget(), {height / 2, height / 2});
    }
    if (auto *bottom = m_anchors.value(DockPosition::Bottom))
    {
        m_manager->setSplitterSizes(bottom->dockAreaWidget(), {height / 2, height / 2});
    }
}

void DockWorkspace::showPanel(const QString &id)
{
    if (auto *dock = m_panels.value(id))
    {
        dock->toggleView(true);
        dock->setAsCurrentTab();
        if (dock->isFloating())
        {
            dock->window()->raise();
        }
    }
}

void DockWorkspace::hidePanel(const QString &id)
{
    if (auto *dock = m_panels.value(id))
    {
        dock->toggleView(false);
    }
}

void DockWorkspace::hidePanels()
{
    for (auto *dock : m_panels)
    {
        dock->toggleView(false);
    }
}

QMenu *DockWorkspace::windowMenu() const
{
    return m_menu;
}
} // namespace reverse::workbench
