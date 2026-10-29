/*
 * 文件描述：
 * 提供工作台深色主题与按逻辑尺寸绘制的自有图标。
 */
#pragma once

#include <QIcon>
#include <QString>

class QApplication;

namespace reverse::workbench
{
class WorkbenchTheme
{
  public:
    static void apply(QApplication &application);
    static QIcon icon(const QString &name);
};
} // namespace reverse::workbench
