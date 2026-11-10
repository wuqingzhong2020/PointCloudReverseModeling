/* 文件描述：创建编辑工具并连接建模动作；业务执行仍经 CommandService。 */

#include <algorithm>
#include <exception>
#include <string>

#include <QAction>
#include <QFrame>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QShortcut>
#include <QToolButton>

#include "workbench/CrossPlateTool.h"
#include "workbench/DockWorkspace.h"
#include "workbench/EdgeTreatmentTool.h"
#include "workbench/EndCapTool.h"
#include "workbench/HoleTool.h"
#include "workbench/LocalReconstructionTool.h"
#include "workbench/MainWindowState.h"
#include "workbench/ModelInspector.h"
#include "workbench/ModelTransformTool.h"
#include "workbench/OccViewport.h"
#include "workbench/PlateAdjustmentTool.h"
#include "workbench/PlateEditTool.h"
#include "workbench/PlateGenerationTool.h"
#include "workbench/PointModelingTool.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/SectionTool.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/UiConversions.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"

namespace reverse::workbench
{
void MainWindow::initializeCoreTools()
{
    const auto connectTool = [this](auto *tool)
    {
        tool->hide();
        tool->onChanged = [this] { updateActions(); };
        tool->onCommitted = [this] { refreshDisplay(false); };
    };
    using enum ActionId;
    auto &actions = *state_->actions;
    state_->parameters = new ReconstructionPanel(this);
    state_->operation = new OperationPanel(actions, this);
    state_->referenceTool = new ReferencePlaneTool(state_->service, *state_->viewport, state_->operation);
    state_->referenceTool->hide();
    state_->sectionTool->onChanged = [this] { updateActions(); };
    connect(state_->sectionTool, &QMenu::aboutToShow, this,
            [this] { state_->viewport->setSectionPreviewVisible(true); });
    connect(state_->sectionTool, &QMenu::aboutToHide, this,
            [this] { state_->viewport->setSectionPreviewVisible(false); });
    state_->transformTool = new ModelTransformTool(state_->service, *state_->viewport, state_->operation);
    state_->edgeTool = new EdgeTreatmentTool(state_->service, *state_->viewport, state_->operation);
    state_->holeTool = new HoleTool(state_->service, *state_->viewport, state_->operation);
    state_->thicknessTool =
        new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Thickness, state_->operation);
    state_->contourTool =
        new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Contour, state_->operation);
    state_->mirrorTool =
        new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Mirror, state_->operation);
    connectTool(state_->transformTool);
    connectTool(state_->edgeTool);
    connectTool(state_->holeTool);
    connectTool(state_->thicknessTool);
    connectTool(state_->contourTool);
    connectTool(state_->mirrorTool);
}

void MainWindow::initializePlateInteraction()
{
    using enum ActionId;
    auto *platePrompt = state_->platePrompt;
    auto *promptText = state_->platePromptText;
    auto *cancelPlate = state_->cancelPlateButton;
    auto *hint = state_->viewportHint;
    state_->plateEditTool = new PlateEditTool(state_->service, *state_->viewport, state_->operation);
    state_->plateEditTool->hide();
    auto *plateEscape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    plateEscape->setEnabled(false);
    connect(plateEscape, &QShortcut::activated, this, [this] { state_->plateEditTool->cancel(); });
    connect(cancelPlate, &QToolButton::clicked, this, [this] { state_->plateEditTool->cancel(); });
    state_->plateEditTool->onChanged = [this, plateEscape, platePrompt, hint]
    {
        const bool editing = state_->plateEditTool->active() && state_->plateEditTool->direct();
        plateEscape->setEnabled(editing);
        platePrompt->setVisible(editing);
        hint->setVisible(!editing);
        updateActions();
    };
    state_->plateEditTool->onCommitted = [this] { refreshDisplay(false); };
    state_->plateEditTool->onHint = [this, hint, promptText](const QString &message)
    {
        hint->setText(message);
        promptText->setText(message);
        if (state_->status)
            state_->status->setText(message);
    };
    state_->viewport->onPlateCrop = [this](const auto &rectangle) { state_->plateEditTool->crop(rectangle); };
    state_->inspector->onContextMenu = [this](const QPoint &point) { modelContextMenu(point); };
}

void MainWindow::initializeReconstructionTools()
{
    const auto connectTool = [this](auto *tool)
    {
        tool->hide();
        tool->onChanged = [this] { updateActions(); };
        tool->onCommitted = [this] { refreshDisplay(false); };
    };
    using enum ActionId;
    state_->pointTool = new PointModelingTool(state_->service, *state_->viewport, state_->operation);
    state_->localTool = new LocalReconstructionTool(state_->service, *state_->viewport, state_->operation);
    state_->generationTool = new PlateGenerationTool(state_->service, *state_->viewport, state_->operation);
    state_->steelTool = new StructuralSteelTool(state_->service, *state_->viewport, state_->operation);
    state_->crossTool = new CrossPlateTool(state_->service, *state_->viewport, state_->operation);
    state_->capTool = new EndCapTool(state_->service, *state_->viewport, state_->operation);
    connectTool(state_->pointTool);
    connectTool(state_->localTool);
    connectTool(state_->generationTool);
    connectTool(state_->steelTool);
    connectTool(state_->crossTool);
    connectTool(state_->capTool);
}

void MainWindow::connectThicknessEditing()
{
    using enum ActionId;
    state_->viewport->onThicknessLabelEditRequested = [this](const std::string &id)
    {
        if (!state_->activeTask.empty() || state_->closing || !state_->service.isIdle())
        {
            return false;
        }
        const auto project = state_->service.project();
        if (!project.model ||
            std::find(project.model->partIds.begin(), project.model->partIds.end(), id) == project.model->partIds.end())
        {
            return false;
        }
        state_->thicknessEditRevision = project.revision;
        state_->thicknessEditPart = id;
        state_->thicknessTool->cancel();
        state_->contourTool->cancel();
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
        state_->steelTool->cancel();
        state_->crossTool->cancel();
        state_->capTool->cancel();
        state_->localTool->cancel();
        state_->pointTool->cancel();
        state_->plateEditTool->cancel();
        state_->edgeTool->cancel();
        state_->holeTool->cancel();
        state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        state_->viewport->setSelectedParts({id});
        state_->inspector->selectParts({id});
        updateActions();
        return true;
    };
    state_->viewport->onThicknessLabelSubmitted = [this](const std::string &id, double thickness)
    {
        if (!state_->activeTask.empty() || state_->closing || !state_->service.isIdle())
        {
            return;
        }
        try
        {
            state_->activeTask = state_->service.submitThickness({{id}, thickness}, state_->thicknessEditRevision);
            state_->pending = State::Operation::EditThickness;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error)
        {
            appendLog(uiText(error.what()));
        }
    };
}

void MainWindow::connectEditEscape()
{
    using enum ActionId;
    state_->viewport->hasEditContextMenu = [this]
    {
        return state_->pointTool->active() || state_->plateEditTool->active() || state_->localTool->active() ||
               state_->edgeTool->active() || state_->holeTool->active() || state_->thicknessTool->active() ||
               (state_->contourTool->active() || state_->mirrorTool->active()) || state_->generationTool->active() ||
               state_->steelTool->active() || (state_->crossTool->active() || state_->capTool->active());
    };
    state_->viewport->onEditEscape = [this]
    {
        if (state_->pointTool->active())
        {
            state_->pointTool->cancel();
            return true;
        }
        if (state_->plateEditTool->active())
        {
            state_->plateEditTool->cancel();
            return true;
        }
        if (state_->capTool->active())
        {
            state_->capTool->cancel();
            return true;
        }
        if (state_->crossTool->active())
        {
            state_->crossTool->cancel();
            return true;
        }
        if (state_->steelTool->active())
        {
            state_->steelTool->cancel();
            return true;
        }
        if (state_->generationTool->active())
        {
            state_->generationTool->cancel();
            return true;
        }
        if (state_->contourTool->active())
        {
            state_->contourTool->cancel();
            return true;
        }
        if (state_->localTool->active())
        {
            state_->localTool->cancel();
            state_->pointTool->cancel();
            return true;
        }
        if (state_->mirrorTool->active())
        {
            state_->mirrorTool->cancel();
            return true;
        }
        if (state_->thicknessTool->active())
        {
            state_->thicknessTool->cancel();
            return true;
        }
        if (state_->holeTool->active())
        {
            state_->holeTool->cancel();
            return true;
        }
        if (!state_->edgeTool->active())
            return false;
        state_->edgeTool->cancel();
        return true;
    };
    state_->referenceTool->onChanged = [this] { refreshProjectPanels(); };
}

void MainWindow::cancelModelTools()
{
    state_->thicknessTool->cancel();
    state_->contourTool->cancel();
    state_->mirrorTool->cancel();
    state_->holeTool->cancel();
    state_->edgeTool->cancel();
    state_->transformTool->cancel();
    state_->referenceTool->cancel();
    state_->generationTool->cancel();
    state_->steelTool->cancel();
    state_->crossTool->cancel();
    state_->capTool->cancel();
    state_->localTool->cancel();
    state_->pointTool->cancel();
    state_->plateEditTool->cancel();
}

void MainWindow::connectSteelActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    for (const auto id : {ChannelSteel, AngleSteel})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    cancelModelTools();
                    state_->actions->get(ActionId::Measure)->setChecked(false);
                    state_->actions->get(ActionId::SelectFace)->setChecked(true);
                    state_->operation->showCustom(state_->actions->get(id)->text(), state_->steelTool, id);
                    state_->workspace->showPanel(QStringLiteral("dock.operation"));
                    state_->steelTool->begin(id == ChannelSteel ? StructuralSteelKind::Channel
                                                                : StructuralSteelKind::Angle);
                });
    }
    connect(actions.get(CrossPlate), &QAction::triggered, this,
            [this]
            {
                cancelModelTools();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(state_->actions->get(CrossPlate)->text(), state_->crossTool, CrossPlate);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->crossTool->begin();
            });
    connect(actions.get(EndCapPlate), &QAction::triggered, this,
            [this]
            {
                cancelModelTools();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(state_->actions->get(EndCapPlate)->text(), state_->capTool, EndCapPlate);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->capTool->begin();
            });
}

void MainWindow::connectCreationActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    for (const auto id : {OuterPlate, BottomPlate})
    {
        connect(
            actions.get(id), &QAction::triggered, this,
            [this, id]
            {
                cancelModelTools();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(id == OuterPlate ? ActionId::SelectFace : ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(state_->actions->get(id)->text(), state_->generationTool, id);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->generationTool->begin(id == OuterPlate ? PlateGenerationKind::Cover
                                                               : PlateGenerationKind::Base);
            });
    }
    for (const auto id : {BuildByPoints, ObstacleByPoints})
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    state_->plateEditTool->cancel();
                    state_->localTool->cancel();
                    state_->pointTool->cancel();
                    state_->generationTool->cancel();
                    state_->steelTool->cancel();
                    state_->crossTool->cancel();
                    state_->capTool->cancel();
                    state_->mirrorTool->cancel();
                    state_->contourTool->cancel();
                    state_->thicknessTool->cancel();
                    state_->holeTool->cancel();
                    state_->edgeTool->cancel();
                    state_->transformTool->cancel();
                    state_->referenceTool->cancel();
                    state_->actions->get(ActionId::Measure)->setChecked(false);
                    state_->actions->get(ActionId::SelectPoint)->setChecked(true);
                    if (id == ObstacleByPoints)
                    {
                        state_->actions->get(ActionId::ShowModel)->setChecked(true);
                        state_->actions->get(ActionId::ShowCloud)->setChecked(true);
                        state_->actions->get(ActionId::Select)->setChecked(true);
                        state_->viewport->setCloudSelectionMode(CloudSelectionMode::Replace);
                    }
                    state_->operation->showCustom(id == ObstacleByPoints ? QStringLiteral("选点生成障碍物")
                                                                         : QStringLiteral("选点建模"),
                                                  state_->pointTool);
                    state_->workspace->showPanel(QStringLiteral("dock.operation"));
                    state_->pointTool->begin(id == ObstacleByPoints);
                });
    connect(actions.get(LocalReconstruct), &QAction::triggered, this,
            [this]
            {
                state_->mirrorTool->cancel();
                state_->generationTool->cancel();
                state_->steelTool->cancel();
                state_->crossTool->cancel();
                state_->capTool->cancel();
                state_->contourTool->cancel();
                state_->thicknessTool->cancel();
                state_->holeTool->cancel();
                state_->edgeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(QStringLiteral("局部重建"), state_->localTool);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->localTool->begin();
            });
}

void MainWindow::connectAdjustmentActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    connect(actions.get(ExtendContour), &QAction::triggered, this,
            [this]
            {
                state_->mirrorTool->cancel();
                state_->generationTool->cancel();
                state_->steelTool->cancel();
                state_->crossTool->cancel();
                state_->capTool->cancel();
                state_->localTool->cancel();
                state_->pointTool->cancel();
                state_->plateEditTool->cancel();
                state_->thicknessTool->cancel();
                state_->holeTool->cancel();
                state_->edgeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(QStringLiteral("外扩板件轮廓"), state_->contourTool,
                                              ActionId::ExtendContour);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->contourTool->begin();
            });
    connect(actions.get(MirrorComplete), &QAction::triggered, this,
            [this]
            {
                state_->generationTool->cancel();
                state_->steelTool->cancel();
                state_->crossTool->cancel();
                state_->capTool->cancel();
                state_->localTool->cancel();
                state_->pointTool->cancel();
                state_->plateEditTool->cancel();
                state_->contourTool->cancel();
                state_->thicknessTool->cancel();
                state_->holeTool->cancel();
                state_->edgeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(QStringLiteral("镜像补全"), state_->mirrorTool, ActionId::MirrorComplete);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->mirrorTool->begin();
            });
    connect(actions.get(MirrorCompleteAll), &QAction::triggered, this,
            [this]
            {
                if (!state_->service.isIdle() || !state_->activeTask.empty())
                    return;
                state_->generationTool->cancel();
                state_->steelTool->cancel();
                state_->crossTool->cancel();
                state_->capTool->cancel();
                state_->localTool->cancel();
                state_->pointTool->cancel();
                state_->plateEditTool->cancel();
                state_->contourTool->cancel();
                state_->mirrorTool->cancel();
                state_->thicknessTool->cancel();
                state_->holeTool->cancel();
                state_->edgeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                const auto project = state_->service.project();
                if (!project.model || project.model->partIds.size() < 2)
                    return;
                const auto selected = state_->viewport->selectedParts();
                if (selected.empty())
                {
                    state_->status->setText(QStringLiteral("请先选择目标工件的任意一个部件，再执行全部镜像补全。"));
                    return;
                }
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->mirrorSelectionRevision = project.revision;
                try
                {
                    state_->activeTask =
                        state_->service.submitConnectedPartsInspection(selected.front(), project.revision);
                    state_->pending = State::Operation::SelectMirrorAssembly;
                    state_->cancellationRequested = false;
                    state_->progress->setValue(0);
                    updateActions();
                }
                catch (const std::exception &error)
                {
                    reportError(uiText(error.what()));
                }
            });
    connect(actions.get(PlateThickness), &QAction::triggered, this,
            [this]
            {
                state_->contourTool->cancel();
                state_->mirrorTool->cancel();
                state_->generationTool->cancel();
                state_->steelTool->cancel();
                state_->crossTool->cancel();
                state_->capTool->cancel();
                state_->localTool->cancel();
                state_->pointTool->cancel();
                state_->plateEditTool->cancel();
                state_->holeTool->cancel();
                state_->edgeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                state_->operation->showCustom(QStringLiteral("修改板厚"), state_->thicknessTool,
                                              ActionId::PlateThickness);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->thicknessTool->begin();
            });
}

void MainWindow::connectHoleTransformActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    for (const auto id : {WeldHole, BottomHole})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    state_->transformTool->cancel();
                    state_->referenceTool->cancel();
                    state_->edgeTool->cancel();
                    state_->actions->get(ActionId::Measure)->setChecked(false);
                    state_->actions->get(ActionId::SelectSolid)->setChecked(true);
                    state_->operation->showCustom(QStringLiteral("添加孔"), state_->holeTool);
                    state_->workspace->showPanel(QStringLiteral("dock.operation"));
                    if (id == WeldHole && state_->viewport->selectedParts().size() > 1)
                        beginPlateEdit("holes");
                    else
                        state_->holeTool->begin(id == WeldHole ? PlateHoleKind::Weld : PlateHoleKind::Bottom);
                });
    }
    connect(actions.get(Fillet), &QAction::triggered, this,
            [this]
            {
                state_->holeTool->cancel();
                state_->transformTool->cancel();
                state_->referenceTool->cancel();
                state_->actions->get(ActionId::Measure)->setChecked(false);
                state_->actions->get(ActionId::SelectEdge)->setChecked(true);
                state_->operation->showCustom(QStringLiteral("圆角 / 倒角"), state_->edgeTool);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->edgeTool->begin();
            });
    for (const auto id : {Move, Rotate})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    state_->referenceTool->cancel();
                    state_->operation->showCustom(state_->actions->get(id)->text(), state_->transformTool);
                    state_->workspace->showPanel(QStringLiteral("dock.operation"));
                    state_->transformTool->begin(id == ActionId::Move ? ModelTransformKind::Move
                                                                      : ModelTransformKind::Rotate);
                });
    }
    connect(actions.get(BasePlane), &QAction::triggered, this,
            [this]
            {
                state_->operation->showCustom(QStringLiteral("设置基准面"), state_->referenceTool);
                state_->workspace->showPanel(QStringLiteral("dock.operation"));
                state_->referenceTool->begin();
                updateActions();
            });
    connect(actions.get(Section), &QAction::triggered, this,
            [this](bool enabled) { state_->sectionTool->setSectionEnabled(enabled); });
}

} // namespace reverse::workbench
