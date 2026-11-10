/* 文件描述：主窗口各实现文件共用的私有状态；不暴露给几何或命令层。 */
#pragma once
#include <utility>
#include "commands/CommandService.h"
#include "workbench/MainWindow.h"

class QFrame;
class QLabel;
class QProgressBar;
class QTimer;
class QToolButton;

namespace reverse::workbench
{
class OccViewport;
class WorkbenchActions;
class RecentFilesMenu;
class DockWorkspace;
class ReconstructionPanel;
class OperationPanel;
class ReferencePlaneTool;
class SectionTool;
class ModelTransformTool;
class EdgeTreatmentTool;
class HoleTool;
class PlateAdjustmentTool;
class LocalReconstructionTool;
class PointModelingTool;
class PlateEditTool;
class PlateGenerationTool;
class StructuralSteelTool;
class CrossPlateTool;
class EndCapTool;
class ModelInspector;
class CommandConsoleWidget;

struct MainWindow::State
{
    enum class Operation
    {
        None,
        StartRecording,
        InitializeOutput,
        ExportAndExit,
        Import,
        Reconstruct,
        DeleteModel,
        SelectMirrorAssembly,
        MirrorCompleteAll,
        EditThickness,
        Export,
        ExportCloud,
        Deviation,
        SelectCloud,
        EditCloud,
        History
    };
    explicit State(CommandEnvironment environment) : service(std::move(environment)) {}

    CommandService service;
    std::string activeTask;
    StartupRequest startup;
    bool startupPending{};
    Operation pending{Operation::None};
    std::string cloudBeforeEdit;
    bool closing{};
    bool cancellationRequested{};
    bool parametersDirty{true};
    OccViewport *viewport{};
    QFrame *platePrompt{};
    QLabel *platePromptText{};
    QToolButton *cancelPlateButton{};
    QLabel *viewportHint{};
    WorkbenchActions *actions{};
    RecentFilesMenu *recentFiles{};
    DockWorkspace *workspace{};
    ReconstructionPanel *parameters{};
    OperationPanel *operation{};
    ReferencePlaneTool *referenceTool{};
    SectionTool *sectionTool{};
    ModelTransformTool *transformTool{};
    EdgeTreatmentTool *edgeTool{};
    HoleTool *holeTool{};
    PlateAdjustmentTool *thicknessTool{};
    std::uint64_t thicknessEditRevision{}, mirrorSelectionRevision{};
    std::vector<std::string> mirrorAssemblyParts;
    std::string thicknessEditPart;
    PlateAdjustmentTool *contourTool{};
    PlateAdjustmentTool *mirrorTool{};
    LocalReconstructionTool *localTool{};
    PointModelingTool *pointTool{};
    PlateEditTool *plateEditTool{};
    PlateGenerationTool *generationTool{};
    StructuralSteelTool *steelTool{};
    CrossPlateTool *crossTool{};
    EndCapTool *capTool{};
    std::string displayedCloudId, displayedModelId;
    std::uint64_t displayedHistoryRestoreRevision{};
    ModelInspector *inspector{};
    CommandConsoleWidget *console{};
    QLabel *taskDetails{};
    QLabel *status{};
    QProgressBar *progress{};
    QTimer *timer{};
};
} // namespace reverse::workbench
