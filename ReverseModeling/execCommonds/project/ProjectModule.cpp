/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "project/ProjectModule.h"
#include "project/SnapshotCommand.h"
namespace rmcmd::project
{
void registerProjectModule(rm::CommandManager &manager)
{
    SnapshotCommand::registerDefinition(manager);
}
}
