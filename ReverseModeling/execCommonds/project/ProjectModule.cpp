/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "project/ProjectModule.h"
#include "project/UndoCommand.h"
#include "project/RedoCommand.h"
#include "project/ResetCommand.h"
#include "project/SnapshotCommand.h"
#include "project/SetOutputDirCommand.h"
#include "project/ExportAndExitCommand.h"
#include "project/SetReferencePlaneCommand.h"
#include "project/ClearReferencePlaneCommand.h"
#include "project/SectionCommand.h"
namespace rmcmd::project
{
void registerProjectModule(rm::CommandManager &manager)
{
    UndoCommand::registerDefinition(manager);
    RedoCommand::registerDefinition(manager);
    ResetCommand::registerDefinition(manager);
    SnapshotCommand::registerDefinition(manager);
    SetOutputDirCommand::registerDefinition(manager);
    ExportAndExitCommand::registerDefinition(manager);
    SetReferencePlaneCommand::registerDefinition(manager);
    ClearReferencePlaneCommand::registerDefinition(manager);
    SectionCommand::registerDefinition(manager);
}
}
