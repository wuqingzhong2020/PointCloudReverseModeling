/*
 * 文件描述：
 * 管理工作台停靠窗口、显隐菜单和用户布局，保持工具窗口实例及内容的生命周期。
 */
#pragma once

#include <QByteArray>
#include <QMap>
#include <QObject>
#include <QString>

class QMainWindow;
class QMenu;
class QWidget;
namespace ads
{
class CDockManager;
class CDockWidget;
} // namespace ads

namespace reverse::workbench
{
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

  private:
    QMainWindow *m_window;
    ads::CDockManager *m_manager;
    QMenu *m_menu;
    QMap<QString, ads::CDockWidget *> m_panels;
    QMap<DockPosition, ads::CDockWidget *> m_anchors;
    QByteArray m_defaultState;
};
} // namespace reverse::workbench
