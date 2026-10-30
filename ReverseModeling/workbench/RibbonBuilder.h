/*
 * 文件描述：
 * 装配工作台功能区和视图工具条，共用工作台动作及窗口菜单。
 */
#pragma once

class QMenu;
class QWidget;
class SARibbonBar;

namespace reverse::workbench
{
class WorkbenchActions;
void buildRibbon(SARibbonBar *ribbon, WorkbenchActions &actions, QMenu *windows, QMenu *recentFiles);
QWidget *createViewToolbar(QWidget *parent, WorkbenchActions &actions);
} // namespace reverse::workbench
