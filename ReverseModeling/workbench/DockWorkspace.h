/*
 * 文件描述：
 * 管理工作台停靠窗口、显隐菜单和用户布局，保持工具窗口实例及内容的生命周期。
 */
#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>
#include <functional>

class QMainWindow;
class QMenu;
class QGridLayout;
class QTabBar;
class QWidget;
namespace ads
{
class CDockManager;
class CDockWidget;
} // namespace ads

namespace reverse::workbench
{
// 收起自动隐藏面板只是改变可见性；切换工具页或关闭 Dock 才结束编辑。
bool shouldCancelDockToolOnHide(const QWidget *tool);

enum class DockPosition
{
    Left,
    Right,
    Bottom
};

class DockWorkspace final : public QObject
{
  public:
    DockWorkspace(QMainWindow *window, QWidget *viewport);
    void addPanel(const QString &id, const QString &title, QWidget *content, DockPosition position,
                  const QString &belowPanel = {});
    void initializeLayout();
    void restoreLayout();
    void saveLayout() const;
    void resetLayout();
    void showPanel(const QString &id);
    void hidePanel(const QString &id);
    void hidePanels();
    QMenu *windowMenu() const;
    std::function<void(const QString &)> onPanelClosed;

  private:
    bool eventFilter(QObject *object, QEvent *event) override;
    void fixPanel(ads::CDockWidget *dock);
    void normalizeFixedPanels();
    void addPanelTab(const QString &id, const QString &title, DockPosition position);
    void syncPanelTabs();
    void applyDefaultSizes();
    QMainWindow *m_window;
    ads::CDockManager *m_manager;
    QMenu *m_menu;
    QGridLayout *m_layout;
    QMap<DockPosition, QTabBar *> m_tabBars;
    QMap<QString, ads::CDockWidget *> m_panels;
    QMap<QString, DockPosition> m_positions;
    QMap<DockPosition, ads::CDockWidget *> m_anchors;
    QByteArray m_defaultState;
    QMap<DockPosition, int> m_panelSizes;
};
} // namespace reverse::workbench
