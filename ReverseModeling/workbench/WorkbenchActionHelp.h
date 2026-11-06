/*
 * 文件描述：统一配置动作悬停帮助，动态状态追加到说明中而不覆盖用途和操作步骤。
 */
#pragma once

#include <QString>

class QAction;

namespace reverse::workbench
{
class WorkbenchActions;

void setActionHelp(QAction *action, const QString &purpose, const QString &operation,
                   const QString &requirement = {});
void setActionHelpContext(QAction *action, const QString &context);
void initializeActionHelp(WorkbenchActions &actions);
} // namespace reverse::workbench
