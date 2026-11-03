/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "model/ModelModule.h"
#include "model/ReconstructCommand.h"
#include "model/ParametricCommand.h"
#include "model/ExportStepCommand.h"
#include "model/DeviationCommand.h"
namespace rmcmd::model
{
void registerModelModule(rm::CommandManager &manager)
{
    ReconstructCommand::registerDefinition(manager);
    ParametricCommand::registerDefinition(manager);
    ExportStepCommand::registerDefinition(manager);
    DeviationCommand::registerDefinition(manager);
}
}
