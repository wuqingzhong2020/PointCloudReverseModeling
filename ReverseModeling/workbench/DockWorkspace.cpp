/*
 * 文件描述：
 * 以 ADS 固定中央视图和工具内容，用独立 Qt 标签栏保留各侧全部窗口入口。
 * 图钉手动收起整组，保留工具内容和编辑状态，并保存、恢复版本化布局。
 */
#include <QAction>
#include <QAbstractButton>
#include <QEvent>
#include <QGuiApplication>
#include <QGridLayout>
#include <QMainWindow>
#include <QMenu>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <QTabBar>

#include <AutoHideDockContainer.h>
#include <AutoHideSideBar.h>
#include <AutoHideTab.h>
#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <DockWidgetTab.h>
#include <FloatingDockContainer.h>
#include <IconProvider.h>

#include "workbench/DockWorkspace.h"
#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
constexpr int layoutVersion = 6;
constexpr auto pinButtonProperty = "workspace.pinButton";

ads::SideBarLocation panelSide(DockPosition position)
{
    return position == DockPosition::Left ? ads::SideBarLeft :
           position == DockPosition::Right ? ads::SideBarRight : ads::SideBarBottom;
}

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

bool shouldCancelDockToolOnHide(const QWidget *tool)
{
    if (!tool->isHidden())
    {
        for (auto *parent = tool->parentWidget(); parent; parent = parent->parentWidget())
        {
            if (auto *dock = qobject_cast<ads::CDockWidget *>(parent))
            {
                return dock->isClosed();
            }
        }
    }
    return true;
}

DockWorkspace::DockWorkspace(QMainWindow *window, QWidget *viewport)
    : QObject(window),
      m_window(window),
      m_manager(nullptr),
      m_menu(new QMenu(QStringLiteral("窗口"), window)),
      m_layout(nullptr)
{
    ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::XmlCompressionEnabled, false);
    // 边缘标签点击后固定展开；鼠标移出、点击模型不收起，图钉手动收起整组。
    ads::CDockManager::setAutoHideConfigFlags(ads::CDockManager::AutoHideFlags(ads::CDockManager::DefaultAutoHideConfig) |
        ads::CDockManager::AutoHideButtonTogglesArea | ads::CDockManager::AutoHideButtonCheckable);
    ads::CDockManager::setAutoHideConfigFlag(ads::CDockManager::AutoHideShowOnMouseOver, false);
    ads::CDockManager::setAutoHideConfigFlag(ads::CDockManager::AutoHideOpenOnDragHover, false);
    ads::CDockManager::setAutoHideConfigFlag(ads::CDockManager::AutoHideHasCloseButton, false);
    ads::CDockManager::setAutoHideConfigFlag(ads::CDockManager::AutoHideHasMinimizeButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::TabsAtBottom, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::ActiveTabHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaDynamicTabsMenuButtonVisibility, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasCloseButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaCloseButtonClosesTab, true);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
    ads::CDockManager::setConfigFlag(ads::CDockManager::DoubleClickUndocksWidget, false);
    auto pin = WorkbenchTheme::icon(QStringLiteral("pin"));
    ads::CDockManager::iconProvider().registerCustomIcon(ads::AutoHideIcon, pin);
    ads::CDockManager::iconProvider().registerCustomIcon(ads::DockAreaMinimizeIcon,
                                                        WorkbenchTheme::icon(QStringLiteral("minimize")));
    ads::CDockManager::iconProvider().registerCustomIcon(ads::DockAreaCloseIcon,
                                                        WorkbenchTheme::icon(QStringLiteral("dock-close")));
    ads::CDockManager::iconProvider().registerCustomIcon(ads::DockAreaMenuIcon,
                                                        WorkbenchTheme::icon(QStringLiteral("dock-menu")));
    auto *workspace = new QWidget(window);
    workspace->setObjectName(QStringLiteral("workspace.frame"));
    m_layout = new QGridLayout(workspace);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);
    m_layout->setRowStretch(0, 1);
    m_layout->setColumnStretch(1, 1);
    m_manager = new ads::CDockManager(workspace);
    m_layout->addWidget(m_manager, 0, 1);
    window->setCentralWidget(workspace);
    m_manager->setObjectName(QStringLiteral("workspace.docks"));
    m_manager->setStyleSheet(QString()); // 统一使用应用 QSS，不叠加 ADS 默认浅色样式。
    // ADS 只负责内容布局和收起状态；入口全部由外层 QTabBar 提供，原生侧栏不再占空间。
    for (auto side : {ads::SideBarLeft, ads::SideBarRight, ads::SideBarTop, ads::SideBarBottom})
    {
        auto *bar = m_manager->autoHideSideBar(side);
        if (side == ads::SideBarLeft || side == ads::SideBarRight)
        {
            bar->setFixedWidth(0);
        }
        else
        {
            bar->setFixedHeight(0);
        }
    }
    connect(m_manager, &ads::CDockManager::stateRestored, this, &DockWorkspace::normalizeFixedPanels);
    connect(m_manager, &ads::CDockManager::dockAreaCreated, this, [this](ads::CDockAreaWidget *area)
    {
        area->titleBar()->setContextMenuPolicy(Qt::PreventContextMenu);
        if (auto *button = area->titleBarButton(ads::TitleBarButtonAutoHide))
        {
            button->setToolTip(QStringLiteral("自动隐藏 / 固定窗口"));
            button->setAccessibleName(QStringLiteral("自动隐藏 / 固定窗口"));
            button->setProperty(pinButtonProperty, true);
            button->installEventFilter(this);
            // 本工作台只保留两种状态；固定时回到原侧标签组，不创建额外浮动/分割窗口。
            disconnect(button, &QAbstractButton::clicked, area->titleBar(), nullptr);
            connect(button, &QAbstractButton::clicked, area, [this, area]
            {
                auto *current = area->currentDockWidget();
                if (!current) return;
                if (current->isAutoHide())
                {
                    fixPanel(current);
                }
                else
                {
                    const auto docks = area->openedDockWidgets();
                    for (auto *dock : docks)
                    {
                        hidePanel(dock->objectName());
                    }
                }
            });
        }
        if (auto *button = area->titleBarButton(ads::TitleBarButtonClose))
            button->setToolTip(QStringLiteral("关闭当前窗口"));
        if (auto *button = area->titleBarButton(ads::TitleBarButtonTabsMenu))
            button->setToolTip(QStringLiteral("切换窗口"));
        if (auto *button = area->titleBarButton(ads::TitleBarButtonMinimize))
            button->setToolTip(QStringLiteral("收起到边缘标签"));
    });
    connect(m_manager, &ads::CDockManager::autoHideWidgetCreated, this,
            [this](ads::CAutoHideDockContainer *container)
    {
        auto *tab = container->autoHideTab();
        tab->setContextMenuPolicy(Qt::PreventContextMenu);
        // 替换临时弹出入口，等待鼠标事件完成后再移除侧栏标签，避免在事件栈中重排控件。
        disconnect(tab, &QAbstractButton::pressed, container, &ads::CAutoHideDockContainer::toggleCollapseState);
        const auto id = container->dockWidget()->objectName();
        connect(tab, &QAbstractButton::pressed, this, [this, id] { showPanel(id); }, Qt::QueuedConnection);
    });
    auto *central = new ads::CDockWidget(m_manager, QStringLiteral("三维视图"));
    central->setObjectName(QStringLiteral("dock.viewport"));
    central->setFeature(ads::CDockWidget::NoTab, true);
    viewport->setMinimumSize(1, 1);
    central->setWidget(viewport, ads::CDockWidget::ForceNoScrollArea);
    m_manager->setCentralWidget(central);
    central->setFeature(ads::CDockWidget::DockWidgetPinnable, false);
    m_menu->setObjectName(QStringLiteral("menu.windows"));
    m_menu->setIcon(WorkbenchTheme::icon(QStringLiteral("window")));
}

bool DockWorkspace::eventFilter(QObject *object, QEvent *event)
{
    if (object->property(pinButtonProperty).toBool())
    {
        // ADS 在固定与自动隐藏切换后会改写提示，保持同一个图钉、同一套中文含义。
        if (event->type() == QEvent::ToolTipChange)
        {
            auto *button = static_cast<QWidget *>(object);
            const auto tip = QStringLiteral("自动隐藏 / 固定窗口");
            if (button->toolTip() != tip) button->setToolTip(tip);
        }
        return QObject::eventFilter(object, event);
    }
    return QObject::eventFilter(object, event);
}

void DockWorkspace::fixPanel(ads::CDockWidget *dock)
{
    const auto position = m_positions.value(dock->objectName());
    ads::CDockWidget *anchor = nullptr;
    for (auto *other : m_panels)
    {
        if (other != dock && !other->isAutoHide() && !other->isClosed() &&
            m_positions.value(other->objectName()) == position)
        {
            anchor = other;
            break;
        }
    }
    const bool wasHidden = dock->isAutoHide();
    if (wasHidden)
    {
        const auto size = dock->autoHideDockContainer()->size();
        m_panelSizes.insert(position, position == DockPosition::Bottom ? size.height() : size.width());
        dock->setAutoHide(false);
        if (!anchor && position == DockPosition::Bottom)
        {
            // 底部只占中央视图下方；侧边窗口保持全高，二者不相互遮盖。
            m_manager->addDockWidget(ads::BottomDockWidgetArea, dock, m_manager->centralWidget()->dockAreaWidget());
        }
    }
    auto *area = anchor ? anchor->dockAreaWidget() : dock->dockAreaWidget();
    for (auto *other : m_panels)
    {
        if (!other->isAutoHide() && !other->isClosed() &&
            m_positions.value(other->objectName()) == position && other->dockAreaWidget() != area)
        {
            m_manager->addDockWidgetTabToArea(other, area);
        }
    }
    dock->toggleView(true);
    dock->setAsCurrentTab();
    if (wasHidden && !anchor)
    {
        auto *splitter = qobject_cast<QSplitter *>(area->parentWidget());
        if (splitter && splitter->count() == 2)
        {
            auto sizes = splitter->sizes();
            const int index = splitter->indexOf(area);
            const int total = sizes[0] + sizes[1];
            const int wanted = m_panelSizes.value(position, position == DockPosition::Bottom ? 250 : 340);
            sizes[index] = qMin(wanted, qMax(1, total - 100));
            sizes[1 - index] = qMax(1, total - sizes[index]);
            splitter->setSizes(sizes);
        }
    }
    syncPanelTabs();
}

void DockWorkspace::normalizeFixedPanels()
{
    // 兼容已保存的布局：同侧多块固定区域收为一个标签组，并保留当前标签。
    QList<ads::CDockWidget *> current;
    for (auto *dock : m_panels)
    {
        if (!dock->isAutoHide() && !dock->isClosed() && dock->isCurrentTab())
        {
            current.append(dock);
        }
    }
    for (auto *dock : current)
    {
        fixPanel(dock);
    }
    syncPanelTabs();
}

void DockWorkspace::addPanelTab(const QString &id, const QString &title, DockPosition position)
{
    auto *tabs = m_tabBars.value(position);
    if (!tabs)
    {
        tabs = new QTabBar(m_layout->parentWidget());
        const auto side = position == DockPosition::Left ? QStringLiteral("left") :
                          position == DockPosition::Right ? QStringLiteral("right") : QStringLiteral("bottom");
        tabs->setObjectName(QStringLiteral("workspace.tabs.") + side);
        tabs->setAccessibleName(QStringLiteral("工具窗口标签"));
        tabs->setProperty("workspaceTabs", true);
        tabs->setProperty("expanded", false);
        tabs->setShape(position == DockPosition::Bottom ? QTabBar::RoundedSouth :
                       position == DockPosition::Right ? QTabBar::RoundedEast : QTabBar::RoundedWest);
        tabs->setExpanding(false);
        tabs->setDrawBase(false);
        tabs->setAutoHide(false);
        tabs->setUsesScrollButtons(true);
        tabs->setElideMode(Qt::ElideNone);
        tabs->setMovable(false);
        tabs->setTabsClosable(false);
        if (position == DockPosition::Bottom)
        {
            tabs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            m_layout->addWidget(tabs, 1, 1);
        }
        else
        {
            tabs->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
            m_layout->addWidget(tabs, 0, position == DockPosition::Left ? 0 : 2, 2, 1);
        }
        m_tabBars.insert(position, tabs);
        connect(tabs, &QTabBar::currentChanged, this, [this, tabs](int index)
        {
            if (index >= 0)
            {
                showPanel(tabs->tabData(index).toString());
            }
        });
        connect(tabs, &QTabBar::tabBarClicked, this, [this, tabs](int index)
        {
            // 收起后点击仍被记住的当前标签，也要能重新展开。
            if (index >= 0 && index == tabs->currentIndex())
            {
                showPanel(tabs->tabData(index).toString());
            }
        });
    }
    const QSignalBlocker blocker(tabs);
    const int index = tabs->addTab(title);
    tabs->setTabData(index, id);
    tabs->setTabToolTip(index, title);
}

void DockWorkspace::syncPanelTabs()
{
    for (auto *tabs : m_tabBars)
    {
        int current = -1;
        for (int i = 0; i < tabs->count(); ++i)
        {
            auto *dock = m_panels.value(tabs->tabData(i).toString());
            if (dock && !dock->isAutoHide() && !dock->isClosed() && dock->isCurrentTab())
            {
                current = i;
                break;
            }
        }
        const QSignalBlocker blocker(tabs);
        if (current >= 0)
        {
            tabs->setCurrentIndex(current);
        }
        if (tabs->property("expanded").toBool() != (current >= 0))
        {
            tabs->setProperty("expanded", current >= 0);
            tabs->style()->unpolish(tabs);
            tabs->style()->polish(tabs);
            tabs->update();
        }
    }
}

void DockWorkspace::addPanel(const QString &id, const QString &title, QWidget *content, DockPosition position,
                            const QString &belowPanel)
{
    auto *dock = new ads::CDockWidget(m_manager, title);
    dock->setObjectName(id);
    dock->setProperty("toolWindow", true);
    content->setMinimumSize(content->minimumSize().expandedTo(
        QSize(position == DockPosition::Bottom ? 180 : 340, position == DockPosition::Bottom ? 150 : 200)));
    dock->setWidget(content, ads::CDockWidget::ForceNoScrollArea);
    dock->setMinimumSizeHintMode(ads::CDockWidget::MinimumSizeHintFromContentMinimumSize);
    dock->setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, false);
    dock->setFeature(ads::CDockWidget::DockWidgetClosable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetFloatable, false);
    dock->setFeature(ads::CDockWidget::DockWidgetMovable, false);
    dock->setFeature(ads::CDockWidget::NoTab, true);
    dock->tabWidget()->setContextMenuPolicy(Qt::PreventContextMenu);
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
    auto *action = m_menu->addAction(title);
    action->setObjectName(QStringLiteral("window.") + id);
    setActionHelp(action, QStringLiteral("展开“%1”窗口。").arg(title),
                  QStringLiteral("点击展开；通过图钉切换自动隐藏或固定窗口。"));
    connect(action, &QAction::triggered, this, [this, id] { showPanel(id); });
    connect(dock, &ads::CDockWidget::viewToggled, this, [this, id](bool open)
    {
        // 已经收起的 Dock 再关闭时不会产生子控件 HideEvent，仍需通知工具结束编辑。
        if (!open && onPanelClosed) onPanelClosed(id);
    });
    m_panels.insert(id, dock);
    m_positions.insert(id, position);
    addPanelTab(id, title, position);
    connect(dock, &ads::CDockWidget::visibilityChanged, this, [this](bool) { syncPanelTabs(); });
}

void DockWorkspace::initializeLayout()
{
    for (auto *dock : m_panels) dock->toggleView(true);
    applyDefaultSizes();
    for (const auto *id : {"dock.modelTree", "dock.reconstruction", "dock.operation", "dock.commands", "dock.tasks"})
        hidePanel(QString::fromLatin1(id));
    m_defaultState = m_manager->saveState(layoutVersion);
    m_menu->addSeparator();
    auto *hide = m_menu->addAction(QStringLiteral("全部自动隐藏"));
    hide->setObjectName(QStringLiteral("window.hideAll"));
    setActionHelp(hide, QStringLiteral("将工具窗口收起到对应边缘标签。"),
                  QStringLiteral("点击收起；之后点击边缘标签或窗口菜单展开。"));
    connect(hide, &QAction::triggered, this,
            [this]
            {
                hidePanels();
            });
    auto *reset = m_menu->addAction(QStringLiteral("恢复默认布局"));
    reset->setObjectName(QStringLiteral("window.resetLayout"));
    setActionHelp(reset, QStringLiteral("恢复右侧和底部默认自动隐藏的布局。"),
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
    const auto previousGeometry = settings.value(QStringLiteral("ui/layout/v5/geometry"),
                                                 settings.value(QStringLiteral("ui/layout/v4/geometry"))).toByteArray();
    settings.beginGroup(QStringLiteral("ui/layout/v6"));
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
    settings.beginGroup(QStringLiteral("ui/layout/v6"));
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
    // 初始仅右侧工具组与底部命令组。右侧默认收起，固定后约 340 像素宽。
    if (auto *right = m_anchors.value(DockPosition::Right); right && !right->isAutoHide())
    {
        auto *columns = qobject_cast<QSplitter *>(right->dockAreaWidget()->parentWidget());
        if (columns && columns->orientation() == Qt::Horizontal && columns->count() == 2)
        {
            columns->setSizes({qMax(1, width - 340 - columns->handleWidth()), 340});
        }
    }
    for (auto *dock : m_panels)
    {
        if (dock->isAutoHide() && dock->autoHideLocation() == ads::SideBarRight)
            dock->autoHideDockContainer()->setSize(340);
    }
    if (auto *bottom = m_anchors.value(DockPosition::Bottom); bottom && !bottom->isAutoHide())
    {
        m_manager->setSplitterSizes(bottom->dockAreaWidget(), {height / 2, height / 2});
    }
}

void DockWorkspace::showPanel(const QString &id)
{
    if (auto *dock = m_panels.value(id))
    {
        fixPanel(dock);
    }
}

void DockWorkspace::hidePanel(const QString &id)
{
    if (auto *dock = m_panels.value(id))
    {
        const auto position = m_positions.value(id);
        if (!dock->isAutoHide() && dock->dockAreaWidget())
        {
            const auto size = dock->dockAreaWidget()->size();
            m_panelSizes.insert(position, position == DockPosition::Bottom ? size.height() : size.width());
        }
        dock->toggleView(true);
        dock->setAutoHide(true, panelSide(position));
        if (auto *container = dock->autoHideDockContainer())
        {
            if (m_panelSizes.contains(position))
            {
                container->setSize(m_panelSizes.value(position));
            }
            container->collapseView(true);
        }
        syncPanelTabs();
    }
}

void DockWorkspace::hidePanels()
{
    for (auto it = m_panels.cbegin(); it != m_panels.cend(); ++it)
    {
        hidePanel(it.key());
    }
}

QMenu *DockWorkspace::windowMenu() const
{
    return m_menu;
}
} // namespace reverse::workbench
