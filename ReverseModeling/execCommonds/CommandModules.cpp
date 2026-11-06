/* 文件描述：按模块清单顺序注册全部定义，不绑定业务。 */
#include "CommandModules.h"
#include "cloud/CloudModule.h"
#include "model/ModelModule.h"
#include "project/ProjectModule.h"
#include "file/FileModule.h"
namespace rmcmd
{
void registerAllCommandModules(rm::CommandManager &manager)
{
    cloud::registerCloudModule(manager);
    model::registerModelModule(manager);
    project::registerProjectModule(manager);
    file::registerFileModule(manager);
}
}
