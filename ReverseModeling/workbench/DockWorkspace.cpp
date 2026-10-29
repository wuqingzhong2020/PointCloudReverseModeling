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

#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <FloatingDockContainer.h>

#include "workbench/DockWorkspace.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
constexpr int layoutVersion = 1;

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
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaCloseButtonClosesTab, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, true);
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

void DockWorkspace::addPanel(const QString &id, const QString &title, QWidget *content, DockPosition position)
{
    auto *dock = new ads::CDockWidget(m_manager, title);
    dock->setObjectName(id);
    content->setMinimumSize(position == DockPosition::Bottom ? 180 : 340, position == DockPosition::Bottom ? 150 : 200);
    dock->setWidget(content, ads::CDockWidget::ForceNoScrollArea);
    dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
    if (m_anchors.contains(position))
    {
        m_manager->addDockWidgetTabToArea(dock, m_anchors.value(position)->dockAreaWidget());
    }
    else
    {
        const auto area = position == DockPosition::Left    ? ads::LeftDockWidgetArea
                          : position == DockPosition::Right ? ads::RightDockWidgetArea
                                                            : ads::BottomDockWidgetArea;
        m_manager->addDockWidget(area, dock);
        m_anchors.insert(position, dock);
    }
    dock->toggleViewAction()->setObjectName(QStringLiteral("window.") + id);
    m_menu->addAction(dock->toggleViewAction());
    m_panels.insert(id, dock);
}

void DockWorkspace::initializeLayout()
{
    if (m_anchors.contains(DockPosition::Right))
    {
        m_manager->setSplitterSizes(m_anchors.value(DockPosition::Right)->dockAreaWidget(), {360, 900, 300});
    }
    if (m_anchors.contains(DockPosition::Bottom))
    {
        m_manager->setSplitterSizes(m_anchors.value(DockPosition::Bottom)->dockAreaWidget(), {650, 190});
    }
    hidePanels();
    m_defaultState = m_manager->saveState(layoutVersion);
    m_menu->addSeparator();
    auto *hide = m_menu->addAction(QStringLiteral("隐藏所有辅助窗口"));
    hide->setObjectName(QStringLiteral("window.hideAll"));
    connect(hide, &QAction::triggered, this,
            [this]
            {
                hidePanels();
            });
    auto *reset = m_menu->addAction(QStringLiteral("恢复默认布局"));
    reset->setObjectName(QStringLiteral("window.resetLayout"));
    connect(reset, &QAction::triggered, this,
            [this]
            {
                resetLayout();
            });
}

void DockWorkspace::restoreLayout()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ui/layout/v1"));
    const auto geometry = settings.value(QStringLiteral("geometry")).toByteArray();
    if (!geometry.isEmpty())
    {
        m_window->restoreGeometry(geometry);
    }
    const auto state = settings.value(QStringLiteral("docks")).toByteArray();
    if (!state.isEmpty() && !m_manager->restoreState(state, layoutVersion))
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
    settings.beginGroup(QStringLiteral("ui/layout/v1"));
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
