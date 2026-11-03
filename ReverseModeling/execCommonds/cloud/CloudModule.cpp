/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "cloud/CloudModule.h"
#include "cloud/ImportCommand.h"
#include "cloud/SelectCommand.h"
#include "cloud/SelectIndicesCommand.h"
#include "cloud/CropCommand.h"
#include "cloud/DeleteCommand.h"
#include "cloud/UndoCommand.h"
#include "cloud/RedoCommand.h"
#include "cloud/ResetCommand.h"
#include "cloud/ExportCommand.h"
namespace rmcmd::cloud
{
void registerCloudModule(rm::CommandManager &manager)
{
    ImportCommand::registerDefinition(manager);
    SelectCommand::registerDefinition(manager);
    SelectIndicesCommand::registerDefinition(manager);
    CropCommand::registerDefinition(manager);
    DeleteCommand::registerDefinition(manager);
    UndoCommand::registerDefinition(manager);
    RedoCommand::registerDefinition(manager);
    ResetCommand::registerDefinition(manager);
    ExportCommand::registerDefinition(manager);
}
}
