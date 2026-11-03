/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "project/ProjectModule.h"
#include "project/SnapshotCommand.h"
#include "project/SetOutputDirCommand.h"
#include "project/ExportAndExitCommand.h"
namespace rmcmd::project
{
void registerProjectModule(rm::CommandManager &manager)
{
    SnapshotCommand::registerDefinition(manager);
    SetOutputDirCommand::registerDefinition(manager);
    ExportAndExitCommand::registerDefinition(manager);
}
}
