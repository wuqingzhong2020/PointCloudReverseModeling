/* 文件描述：模块只聚合命令定义，业务回调由使用方绑定。 */
#include "model/ModelModule.h"
#include "model/ReconstructLocalCommand.h"
#include "model/DeleteCommand.h"
#include "model/ReconstructCommand.h"
#include "model/ParametricCommand.h"
#include "model/ExportStepCommand.h"
#include "model/DeviationCommand.h"
#include "model/MoveCommand.h"
#include "model/RotateCommand.h"
#include "model/FilletCommand.h"
#include "model/ChamferCommand.h"
#include "model/AddHoleCommand.h"
#include "model/SetThicknessCommand.h"
#include "model/ExpandContourCommand.h"
#include "model/GenerateCoverPlateCommand.h"
#include "model/GenerateBasePlateCommand.h"
#include "model/MirrorCompleteCommand.h"
#include "model/ExtendToPlateCommand.h"
#include "model/TrimToPlateCommand.h"
#include "model/MergeToPlateCommand.h"
#include "model/SplitToPlateCommand.h"
#include "model/CropPartsCommand.h"
#include "model/RepairHolesCommand.h"
#include "model/AddHolesCommand.h"
#include "model/ExportCwrmCommand.h"
#include "model/ExportPcdCommand.h"
#include "model/EditPlateCommand.h"
#include "model/SetChannelSteelCommand.h"
#include "model/OptimizeCrossPlateCommand.h"
#include "model/OptimizeEndCapCommand.h"
#include "model/SetAngleSteelCommand.h"
#include "model/BuildByPointsCommand.h"
#include "model/ObstacleFromPointsCommand.h"
namespace rmcmd::model
{
void registerModelModule(rm::CommandManager &manager)
{
    ReconstructLocalCommand::registerDefinition(manager);
    DeleteCommand::registerDefinition(manager);
    ReconstructCommand::registerDefinition(manager);
    ParametricCommand::registerDefinition(manager);
    ExportStepCommand::registerDefinition(manager);
    DeviationCommand::registerDefinition(manager);
    MoveCommand::registerDefinition(manager);
    RotateCommand::registerDefinition(manager);
    FilletCommand::registerDefinition(manager);
    ChamferCommand::registerDefinition(manager);
    AddHoleCommand::registerDefinition(manager);
    SetThicknessCommand::registerDefinition(manager);
    ExpandContourCommand::registerDefinition(manager);
    GenerateCoverPlateCommand::registerDefinition(manager);
    GenerateBasePlateCommand::registerDefinition(manager);
    MirrorCompleteCommand::registerDefinition(manager);
    ExtendToPlateCommand::registerDefinition(manager);
    TrimToPlateCommand::registerDefinition(manager);
    MergeToPlateCommand::registerDefinition(manager);
    SplitToPlateCommand::registerDefinition(manager);
    CropPartsCommand::registerDefinition(manager);
    RepairHolesCommand::registerDefinition(manager);
    AddHolesCommand::registerDefinition(manager);
    ExportCwrmCommand::registerDefinition(manager);
    ExportPcdCommand::registerDefinition(manager);
    EditPlateCommand::registerDefinition(manager);
    SetChannelSteelCommand::registerDefinition(manager);
    OptimizeCrossPlateCommand::registerDefinition(manager);
    OptimizeEndCapCommand::registerDefinition(manager);
    SetAngleSteelCommand::registerDefinition(manager);
    BuildByPointsCommand::registerDefinition(manager);
    ObstacleFromPointsCommand::registerDefinition(manager);
}
}
