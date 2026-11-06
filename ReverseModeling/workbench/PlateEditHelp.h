/* 文件描述：为右键板件编辑菜单附加图文悬停说明，不触发模型预览或修改。 */
#pragma once

#include <string>

class QAction;
class QMenu;

namespace reverse::workbench
{
void addPlateEditHelp(QMenu &menu, QAction &action, const std::string &operation, bool all = false);
}
