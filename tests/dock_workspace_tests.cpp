/* 文件描述：真实 ADS 事件验证点击后固定展开、同侧单标签组、手动收起及布局恢复。 */
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QFile>
#include <QHideEvent>
#include <QMainWindow>
#include <QMenu>
#include <QMouseEvent>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTabBar>
#include <AutoHideDockContainer.h>
#include <AutoHideTab.h>
#include <DockAreaTabBar.h>
#include <DockAreaTitleBar.h>
#include <DockAreaWidget.h>
#include <DockManager.h>
#include <DockWidget.h>
#include <gtest/gtest.h>
#include "workbench/DockWorkspace.h"
#include "workbench/WorkbenchTheme.h"

namespace
{
using namespace reverse::workbench;
void events()
{
    // 处理侧栏点击的排队固定操作及随后发生的布局事件。
    QApplication::processEvents();
    QApplication::processEvents();
}
QRect frame(QWidget *widget) { return QRect(widget->mapToGlobal(QPoint()), widget->size()); }

class EditingPage : public QWidget
{
  public:
    bool editing = false;
  protected:
    void hideEvent(QHideEvent *event) override
    {
        if (shouldCancelDockToolOnHide(this)) editing = false;
        QWidget::hideEvent(event);
    }
};

class DockWorkspaceTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        ASSERT_TRUE(QFile::exists(":/workbench/workbench.qss"));
        QSettings().clear();
        view = new QWidget;
        workspace = new DockWorkspace(&window, view);
        workspace->addPanel("dock.commands", "命令窗口", new QWidget, DockPosition::Bottom);
        workspace->addPanel("dock.modelTree", "模型树", new QWidget, DockPosition::Right);
        workspace->addPanel("dock.reconstruction", "重建参数", new QWidget, DockPosition::Right);
        pages = new QStackedWidget;
        edit = new EditingPage;
        pages->addWidget(edit);
        pages->addWidget(new QWidget);
        workspace->addPanel("dock.operation", "操作面板", pages, DockPosition::Right);
        workspace->addPanel("dock.tasks", "任务信息", new QWidget, DockPosition::Bottom);
        workspace->initializeLayout();
        workspace->onPanelClosed = [this](const QString &id)
        {
            if (id == "dock.operation") { ++closed; edit->editing = false; }
        };
        window.resize(1400, 850);
        window.show();
        events();
    }
    void TearDown() override { workspace->onPanelClosed = {}; }
    ads::CDockWidget *dock(const char *name)
    {
        return window.findChild<ads::CDockManager *>()->findDockWidget(name);
    }
    QTabBar *tabs(const char *side)
    {
        return window.findChild<QTabBar *>(QStringLiteral("workspace.tabs.") + QString::fromLatin1(side));
    }
    void clickTab(const char *id)
    {
        for (auto *bar : window.findChildren<QTabBar *>())
        {
            if (!bar->property("workspaceTabs").toBool())
            {
                continue;
            }
            for (int i = 0; i < bar->count(); ++i)
            {
                if (bar->tabData(i).toString() != QString::fromLatin1(id))
                {
                    continue;
                }
                const QPointF local(bar->tabRect(i).center());
                QMouseEvent press(QEvent::MouseButtonPress, local, bar->mapToGlobal(local.toPoint()),
                                  Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &press);
                QMouseEvent release(QEvent::MouseButtonRelease, local, bar->mapToGlobal(local.toPoint()),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(bar, &release);
                events();
                return;
            }
        }
        FAIL() << "Missing persistent tab: " << id;
    }
    void pin(const char *name)
    {
        auto *button = dock(name)->dockAreaWidget()->titleBarButton(ads::TitleBarButtonAutoHide);
        ASSERT_NE(button, nullptr);
        button->click();
        events();
    }
    QMainWindow window;
    DockWorkspace *workspace{};
    QWidget *view{};
    QStackedWidget *pages{};
    EditingPage *edit{};
    int closed = 0;
};

TEST_F(DockWorkspaceTest, InitialLayoutHasOnlyRightToolsAndBottomTabs)
{
    for (const auto *id : {"dock.modelTree", "dock.operation", "dock.reconstruction"})
    {
        auto *panel = dock(id);
        ASSERT_NE(panel, nullptr) << id;
        EXPECT_TRUE(panel->isAutoHide());
        EXPECT_FALSE(panel->isClosed());
        EXPECT_TRUE(panel->toggleViewAction()->isChecked());
        EXPECT_EQ(panel->autoHideLocation(), ads::SideBarRight);
        ASSERT_NE(panel->sideTabWidget(), nullptr);
        EXPECT_FALSE(panel->sideTabWidget()->text().isEmpty());
        EXPECT_FALSE(panel->features().testFlag(ads::CDockWidget::DockWidgetClosable));
        EXPECT_FALSE(panel->features().testFlag(ads::CDockWidget::DockWidgetFloatable));
        EXPECT_FALSE(panel->features().testFlag(ads::CDockWidget::DockWidgetMovable));
    }
    EXPECT_EQ(closed, 0);
    EXPECT_EQ(dock("dock.operation")->widget(), pages);
    auto *manager = window.findChild<ads::CDockManager *>();
    ASSERT_NE(manager, nullptr);
    ASSERT_NE(manager->centralWidget(), nullptr);
    EXPECT_FALSE(manager->centralWidget()->features().testFlag(ads::CDockWidget::DockWidgetPinnable));
    EXPECT_EQ(dock("dock.properties"), nullptr);
    auto *commands = dock("dock.commands");
    EXPECT_TRUE(commands->isAutoHide());
    EXPECT_EQ(commands->autoHideLocation(), ads::SideBarBottom);
    EXPECT_EQ(dock("dock.tasks")->autoHideLocation(), ads::SideBarBottom);
    pin("dock.commands");
    pin("dock.tasks");
    workspace->showPanel("dock.commands");
    events();
    auto *bottomTabs = tabs("bottom");
    ASSERT_NE(bottomTabs, nullptr);
    EXPECT_TRUE(bottomTabs->isVisible());
    EXPECT_GT(bottomTabs->mapToGlobal(QPoint()).y(), commands->mapToGlobal(QPoint()).y());
    EXPECT_EQ(commands->dockAreaWidget(), dock("dock.tasks")->dockAreaWidget());
    EXPECT_TRUE(commands->isVisible());
    pin("dock.commands");
    EXPECT_EQ(dock("dock.commands")->autoHideLocation(), ads::SideBarBottom);
    ASSERT_NE(dock("dock.tasks"), nullptr);
    EXPECT_EQ(dock("dock.tasks")->autoHideLocation(), ads::SideBarBottom);
    EXPECT_FALSE(dock("dock.tasks")->isClosed());
}

TEST_F(DockWorkspaceTest, TabAndMenuFixPanelsUntilManuallyCollapsed)
{
    auto *panel = dock("dock.operation");
    clickTab("dock.operation");
    events();
    EXPECT_FALSE(panel->isAutoHide());
    EXPECT_TRUE(panel->isVisible());
    const QPointF local(20, view->height() / 2);
    QMouseEvent click(QEvent::MouseButtonPress, local, view->mapToGlobal(local.toPoint()),
                      Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(view, &click);
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(panel, &leave);
    events();
    EXPECT_TRUE(panel->isVisible());
    EXPECT_FALSE(panel->isAutoHide());
    workspace->showPanel("dock.commands");
    events();
    EXPECT_TRUE(panel->isVisible());
    EXPECT_TRUE(dock("dock.commands")->isVisible());
    EXPECT_EQ(closed, 0);
    pin("dock.operation");
    EXPECT_TRUE(panel->isAutoHide());
    EXPECT_FALSE(panel->autoHideDockContainer()->isVisible());
    clickTab("dock.operation");
    events();
    EXPECT_TRUE(panel->isVisible());
    EXPECT_FALSE(panel->isAutoHide());
}

TEST_F(DockWorkspaceTest, EachSideHasOneFixedAreaAndOneVisiblePanel)
{
    for (int pass = 0; pass < 3; ++pass)
    {
        for (const auto *id : {"dock.operation", "dock.modelTree", "dock.reconstruction", "dock.operation"})
        {
            auto *selected = dock(id);
            if (selected->isAutoHide())
            {
                clickTab(id);
            }
            else
            {
                workspace->showPanel(id);
            }
            events();
            ASSERT_FALSE(selected->isAutoHide());
            EXPECT_TRUE(selected->isVisible());
            int visible = 0;
            for (const auto *other : {"dock.operation", "dock.modelTree", "dock.reconstruction"})
            {
                visible += dock(other)->isVisible() ? 1 : 0;
                if (!dock(other)->isAutoHide())
                {
                    EXPECT_EQ(dock(other)->dockAreaWidget(), selected->dockAreaWidget());
                }
            }
            EXPECT_EQ(visible, 1);
            for (const auto *bottomId : {"dock.tasks", "dock.commands"})
            {
                auto *bottom = dock(bottomId);
                if (bottom->isAutoHide())
                {
                    clickTab(bottomId);
                }
                else
                {
                    workspace->showPanel(bottomId);
                }
                events();
                EXPECT_TRUE(selected->isVisible());
                EXPECT_TRUE(bottom->isVisible());
                EXPECT_EQ(int(dock("dock.tasks")->isVisible()) + int(dock("dock.commands")->isVisible()), 1);
                EXPECT_FALSE(frame(selected).intersects(frame(bottom)));
            }
        }
        workspace->hidePanels();
        events();
    }
}

TEST_F(DockWorkspaceTest, PersistentTabsKeepAllEntriesAndTheirPositions)
{
    // 使用离屏测试屏幕内的窗口尺寸，恢复布局时无需触发移回可见屏幕的纠正逻辑。
    window.resize(760, 540);
    window.move(0, 0);
    events();
    auto *rightTabs = tabs("right");
    auto *bottomTabs = tabs("bottom");
    ASSERT_NE(rightTabs, nullptr);
    ASSERT_NE(bottomTabs, nullptr);
    EXPECT_EQ(tabs("left"), nullptr);
    EXPECT_EQ(rightTabs->count(), 3);
    EXPECT_EQ(bottomTabs->count(), 2);
    const auto rightFrame = rightTabs->geometry();
    const auto bottomFrame = bottomTabs->geometry();
    const auto checkTabs = [&]
    {
        EXPECT_TRUE(rightTabs->isVisible());
        EXPECT_TRUE(bottomTabs->isVisible());
        EXPECT_EQ(rightTabs->geometry(), rightFrame);
        EXPECT_EQ(bottomTabs->geometry(), bottomFrame);
        EXPECT_EQ(rightTabs->tabData(0).toString(), "dock.modelTree");
        EXPECT_EQ(rightTabs->tabData(1).toString(), "dock.reconstruction");
        EXPECT_EQ(rightTabs->tabData(2).toString(), "dock.operation");
        EXPECT_EQ(bottomTabs->tabData(0).toString(), "dock.commands");
        EXPECT_EQ(bottomTabs->tabData(1).toString(), "dock.tasks");
    };
    for (int pass = 0; pass < 3; ++pass)
    {
        for (const auto *id : {"dock.operation", "dock.modelTree", "dock.reconstruction"})
        {
            clickTab(id);
            checkTabs();
            EXPECT_EQ(rightTabs->tabData(rightTabs->currentIndex()).toString(), id);
            EXPECT_TRUE(rightTabs->property("expanded").toBool());
            for (const auto *bottomId : {"dock.commands", "dock.tasks"})
            {
                clickTab(bottomId);
                checkTabs();
                EXPECT_EQ(bottomTabs->tabData(bottomTabs->currentIndex()).toString(), bottomId);
                EXPECT_TRUE(bottomTabs->property("expanded").toBool());
            }
        }
        workspace->hidePanels();
        events();
        checkTabs();
        EXPECT_FALSE(rightTabs->property("expanded").toBool());
        EXPECT_FALSE(bottomTabs->property("expanded").toBool());
        // 点击当前记住的标签也能展开，而不必先点击其他标签。
        clickTab("dock.tasks");
        EXPECT_TRUE(dock("dock.tasks")->isVisible());
        clickTab("dock.reconstruction");
        EXPECT_TRUE(dock("dock.reconstruction")->isVisible());
    }
    workspace->saveLayout();
    workspace->resetLayout();
    workspace->restoreLayout();
    events();
    checkTabs();
    EXPECT_EQ(rightTabs->tabData(rightTabs->currentIndex()).toString(), "dock.reconstruction");
    EXPECT_EQ(bottomTabs->tabData(bottomTabs->currentIndex()).toString(), "dock.tasks");
}

TEST_F(DockWorkspaceTest, MenuAndKeyboardKeepPersistentTabSelectionInSync)
{
    workspace->showPanel("dock.operation");
    events();
    EXPECT_EQ(tabs("right")->tabData(tabs("right")->currentIndex()).toString(), "dock.operation");
    auto *menuAction = workspace->windowMenu()->findChild<QAction *>("window.dock.modelTree");
    ASSERT_NE(menuAction, nullptr);
    menuAction->trigger();
    events();
    EXPECT_EQ(tabs("right")->tabData(tabs("right")->currentIndex()).toString(), "dock.modelTree");
    tabs("bottom")->setCurrentIndex(0);
    tabs("bottom")->setCurrentIndex(1);
    events();
    EXPECT_TRUE(dock("dock.tasks")->isVisible());
    EXPECT_TRUE(dock("dock.modelTree")->isVisible());
    EXPECT_EQ(tabs("bottom")->count(), 2);
    EXPECT_EQ(tabs("right")->count(), 3);
}

TEST_F(DockWorkspaceTest, LeftSideUsesTheSamePersistentSingleGroupRule)
{
    workspace->addPanel("dock.left1", "左侧一", new QWidget, DockPosition::Left);
    workspace->addPanel("dock.left2", "左侧二", new QWidget, DockPosition::Left);
    workspace->hidePanel("dock.left1");
    workspace->hidePanel("dock.left2");
    events();
    clickTab("dock.left1");
    events();
    clickTab("dock.left2");
    events();
    EXPECT_FALSE(dock("dock.left2")->isAutoHide());
    EXPECT_TRUE(dock("dock.left2")->isVisible());
    EXPECT_FALSE(dock("dock.left1")->isVisible());
    EXPECT_EQ(dock("dock.left1")->dockAreaWidget(), dock("dock.left2")->dockAreaWidget());
    workspace->showPanel("dock.commands");
    events();
    EXPECT_TRUE(dock("dock.left2")->isVisible());
    EXPECT_FALSE(frame(view).intersects(frame(dock("dock.left2"))));
    EXPECT_FALSE(frame(dock("dock.commands")).intersects(frame(dock("dock.left2"))));
}

TEST_F(DockWorkspaceTest, WideRightPanelStaysOnRightInsteadOfNearestTopEdge)
{
    window.resize(1710, 765);
    pin("dock.modelTree");
    pin("dock.reconstruction");
    auto *manager = window.findChild<ads::CDockManager *>();
    manager->addDockWidget(ads::BottomDockWidgetArea, dock("dock.reconstruction"), dock("dock.modelTree")->dockAreaWidget());
    events();
    auto *row = dock("dock.modelTree")->dockAreaWidget()->parentWidget();
    auto *columns = qobject_cast<QSplitter *>(row->parentWidget());
    ASSERT_NE(columns, nullptr);
    ASSERT_EQ(columns->count(), 2);
    columns->setSizes({1120, 590});
    events();
    pin("dock.modelTree");
    EXPECT_EQ(dock("dock.modelTree")->autoHideLocation(), ads::SideBarRight);
}

TEST_F(DockWorkspaceTest, AutoHideKeepsEditingButPageSwitchCancels)
{
    workspace->showPanel("dock.operation");
    events();
    edit->editing = true;
    workspace->showPanel("dock.modelTree");
    events();
    EXPECT_TRUE(edit->editing);
    workspace->showPanel("dock.operation");
    workspace->hidePanel("dock.operation");
    events();
    EXPECT_TRUE(edit->editing);
    pages->setCurrentIndex(1);
    events();
    EXPECT_FALSE(edit->editing);
    pages->setCurrentIndex(0);
    workspace->showPanel("dock.operation");
    edit->editing = true;
    pin("dock.operation");
    EXPECT_TRUE(edit->editing);
    workspace->showPanel("dock.operation");
    EXPECT_TRUE(edit->editing);
    workspace->hidePanel("dock.operation");
    workspace->hidePanel("dock.operation");
    events();
    EXPECT_TRUE(edit->editing);
    EXPECT_EQ(closed, 0);
    EXPECT_FALSE(dock("dock.operation")->isClosed());
}

TEST_F(DockWorkspaceTest, LayoutRestoresChangesAndResetReturnsRightAndBottomDefaults)
{
    pin("dock.operation");
    pin("dock.commands");
    workspace->saveLayout();
    workspace->resetLayout();
    events();
    EXPECT_TRUE(dock("dock.operation")->isAutoHide());
    EXPECT_TRUE(dock("dock.modelTree")->isAutoHide());
    EXPECT_TRUE(dock("dock.commands")->isAutoHide());
    workspace->restoreLayout();
    events();
    EXPECT_FALSE(dock("dock.operation")->isAutoHide());
    EXPECT_TRUE(dock("dock.modelTree")->isAutoHide());
    EXPECT_FALSE(dock("dock.commands")->isAutoHide());
    EXPECT_EQ(dock("dock.operation")->widget(), pages);
    workspace->showPanel("dock.operation");
    events();
    EXPECT_TRUE(pages->isVisible());
}

TEST_F(DockWorkspaceTest, RestoredSplitAreasAreMergedOnEachSide)
{
    workspace->showPanel("dock.modelTree");
    workspace->showPanel("dock.operation");
    workspace->showPanel("dock.commands");
    workspace->showPanel("dock.tasks");
    auto *manager = window.findChild<ads::CDockManager *>();
    manager->addDockWidget(ads::BottomDockWidgetArea, dock("dock.operation"), dock("dock.modelTree")->dockAreaWidget());
    manager->addDockWidget(ads::RightDockWidgetArea, dock("dock.tasks"), dock("dock.commands")->dockAreaWidget());
    events();
    workspace->saveLayout();
    workspace->resetLayout();
    workspace->restoreLayout();
    events();
    EXPECT_EQ(dock("dock.operation")->dockAreaWidget(), dock("dock.modelTree")->dockAreaWidget());
    EXPECT_EQ(dock("dock.tasks")->dockAreaWidget(), dock("dock.commands")->dockAreaWidget());
    EXPECT_EQ(int(dock("dock.operation")->isVisible()) + int(dock("dock.modelTree")->isVisible()), 1);
    EXPECT_EQ(int(dock("dock.tasks")->isVisible()) + int(dock("dock.commands")->isVisible()), 1);
}

TEST_F(DockWorkspaceTest, CollapsedWidthSurvivesSavedLayout)
{
    workspace->showPanel("dock.modelTree");
    events();
    auto *area = dock("dock.modelTree")->dockAreaWidget();
    auto *splitter = qobject_cast<QSplitter *>(area->parentWidget());
    ASSERT_NE(splitter, nullptr);
    splitter->setSizes({850, 500});
    events();
    const int expectedWidth = area->width();
    workspace->hidePanel("dock.modelTree");
    workspace->saveLayout();
    workspace->resetLayout();
    workspace->restoreLayout();
    workspace->showPanel("dock.modelTree");
    events();
    EXPECT_NEAR(dock("dock.modelTree")->dockAreaWidget()->width(), expectedWidth, 2);
}

TEST_F(DockWorkspaceTest, BottomTabSwitchAndModeChangeDoNotDetachTabBar)
{
    pin("dock.commands");
    pin("dock.tasks");
    auto *commands = dock("dock.commands");
    auto *area = commands->dockAreaWidget();
    auto *bottomTabs = tabs("bottom");
    ASSERT_NE(bottomTabs, nullptr);
    auto *tabParent = bottomTabs->parentWidget();
    for (int i = 0; i < 4; ++i)
    {
        workspace->showPanel("dock.tasks");
        workspace->showPanel("dock.commands");
        events();
        EXPECT_EQ(bottomTabs->parentWidget(), tabParent);
        EXPECT_TRUE(bottomTabs->isVisible());
        EXPECT_FALSE(bottomTabs->isWindow());
        EXPECT_EQ(bottomTabs->count(), 2);
    }
    EXPECT_FALSE(area->titleBarButton(ads::TitleBarButtonClose)->isVisible());
    EXPECT_EQ(area->titleBarButton(ads::TitleBarButtonAutoHide)->toolTip(), QStringLiteral("自动隐藏 / 固定窗口"));
    workspace->hidePanel("dock.commands");
    events();
    EXPECT_TRUE(commands->isAutoHide());
    EXPECT_FALSE(commands->isClosed());
    EXPECT_FALSE(dock("dock.tasks")->isClosed());
    pin("dock.commands");
    events();
    EXPECT_TRUE(commands->isVisible());
    EXPECT_EQ(commands->dockAreaWidget(), dock("dock.tasks")->dockAreaWidget());
    EXPECT_EQ(commands->dockAreaWidget()->titleBarButton(ads::TitleBarButtonAutoHide)->toolTip(),
              QStringLiteral("自动隐藏 / 固定窗口"));
}

TEST_F(DockWorkspaceTest, RightExpansionResizesViewAndConsoleThenRestoresBoth)
{
    pin("dock.commands");
    events();
    const auto originalView = frame(view);
    const auto originalConsole = frame(dock("dock.commands"));
    const auto originalWindow = window.geometry();
    workspace->showPanel("dock.modelTree");
    events();
    auto *panel = dock("dock.modelTree")->dockAreaWidget();
    EXPECT_LT(view->width(), originalView.width());
    EXPECT_FALSE(frame(view).intersects(frame(panel)));
    EXPECT_FALSE(frame(dock("dock.commands")).intersects(frame(panel)));
    const int firstWidth = view->width();
    auto *columns = qobject_cast<QSplitter *>(panel->parentWidget());
    ASSERT_NE(columns, nullptr);
    auto sizes = columns->sizes();
    const int index = columns->indexOf(panel);
    sizes[index] += 80;
    sizes[1 - index] -= 80;
    columns->setSizes(sizes);
    events();
    EXPECT_LT(view->width(), firstWidth);
    EXPECT_EQ(window.geometry(), originalWindow);
    workspace->hidePanel("dock.modelTree");
    events();
    EXPECT_EQ(frame(view), originalView);
    EXPECT_EQ(frame(dock("dock.commands")), originalConsole);
}

TEST_F(DockWorkspaceTest, BottomAndRightExpansionReserveSpaceWithoutCoveringView)
{
    const auto original = frame(view);
    workspace->showPanel("dock.commands");
    events();
    auto *bottom = dock("dock.commands");
    EXPECT_LT(view->height(), original.height());
    EXPECT_FALSE(frame(view).intersects(frame(bottom)));
    workspace->showPanel("dock.modelTree");
    events();
    auto *right = dock("dock.modelTree");
    EXPECT_FALSE(frame(view).intersects(frame(right)));
    EXPECT_FALSE(frame(view).intersects(frame(bottom)));
    workspace->hidePanel("dock.commands");
    workspace->hidePanel("dock.modelTree");
    events();
    EXPECT_EQ(frame(view), original);
    workspace->showPanel("dock.modelTree");
    events();
    EXPECT_FALSE(frame(view).intersects(frame(dock("dock.modelTree"))));
    pin("dock.modelTree");
    events();
    EXPECT_EQ(frame(view), original);
}

TEST_F(DockWorkspaceTest, WindowMenuExpandsWithoutIntroducingClosedState)
{
    auto *action = workspace->windowMenu()->findChild<QAction *>("window.dock.operation");
    ASSERT_NE(action, nullptr);
    EXPECT_FALSE(action->isCheckable());
    action->trigger();
    events();
    EXPECT_TRUE(dock("dock.operation")->isVisible());
    EXPECT_FALSE(dock("dock.operation")->isAutoHide());
    action->trigger();
    events();
    EXPECT_FALSE(dock("dock.operation")->isClosed());
    EXPECT_TRUE(dock("dock.operation")->isVisible());
    EXPECT_FALSE(dock("dock.operation")->isAutoHide());
    workspace->hidePanels();
    events();
    for (const auto *id : {"dock.commands", "dock.tasks", "dock.modelTree", "dock.operation", "dock.reconstruction"})
    {
        EXPECT_TRUE(dock(id)->isAutoHide());
        EXPECT_FALSE(dock(id)->isClosed());
        EXPECT_FALSE(dock(id)->autoHideDockContainer()->isVisible());
    }
}

TEST_F(DockWorkspaceTest, ResizeAndRestoreDoNotLeaveReservedSpace)
{
    workspace->showPanel("dock.modelTree");
    events();
    window.resize(760, 540);
    events();
    auto *right = dock("dock.modelTree");
    EXPECT_EQ(window.size(), QSize(760, 540));
    EXPECT_GT(view->width(), 0);
    EXPECT_GT(view->height(), 0);
    EXPECT_FALSE(frame(view).intersects(frame(right)));
    workspace->saveLayout();
    workspace->restoreLayout();
    events();
    workspace->hidePanels();
    events();
    const auto restored = frame(view);
    workspace->showPanel("dock.commands");
    events();
    EXPECT_LT(view->height(), restored.height());
    workspace->hidePanels();
    events();
    EXPECT_EQ(frame(view), restored);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ReverseModelingTests");
    QCoreApplication::setApplicationName("DockWorkspaceTests");
    QTemporaryDir settings;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings.path());
    WorkbenchTheme::apply(app);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
