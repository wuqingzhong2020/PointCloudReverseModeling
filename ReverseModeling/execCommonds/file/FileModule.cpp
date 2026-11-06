/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "file/FileModule.h"
#include "file/ConvertCommand.h"
namespace rmcmd::file
{
void registerFileModule(rm::CommandManager &manager)
{
    ConvertCommand::registerDefinition(manager);
}
}
