/*
 * 文件描述：
 * 管理成功导入的点云文件历史，提供可持久保存的最近文件菜单。
 */
#pragma once

#include <QMenu>

namespace reverse::workbench
{

class RecentFilesMenu final : public QMenu
{
  public:
    explicit RecentFilesMenu(QWidget *parent);
    void recordOpened(const QString &path);

  private:
    void rebuild();
};

} // namespace reverse::workbench
