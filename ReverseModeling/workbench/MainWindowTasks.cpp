/* 文件描述：按任务与工程快照更新工作台显示、进度和动作可用状态。 */

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

#include <QAction>
#include <QInputDialog>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>
#include <QTreeWidget>

#include "commands/SectionPlane.h"
#include "workbench/CommandConsoleWidget.h"
#include "workbench/CrossPlateTool.h"
#include "workbench/DockWorkspace.h"
#include "workbench/EdgeTreatmentTool.h"
#include "workbench/EndCapTool.h"
#include "workbench/HoleTool.h"
#include "workbench/LocalReconstructionTool.h"
#include "workbench/MainWindowState.h"
#include "workbench/ModelDisplayData.h"
#include "workbench/ModelInspector.h"
#include "workbench/ModelTransformTool.h"
#include "workbench/OccViewport.h"
#include "workbench/PlateAdjustmentTool.h"
#include "workbench/PlateEditTool.h"
#include "workbench/PlateGenerationTool.h"
#include "workbench/PointCloudDisplayData.h"
#include "workbench/PointModelingTool.h"
#include "workbench/RecentFilesMenu.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/SectionTool.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/UiConversions.h"
#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"

namespace reverse::workbench
{
void MainWindow::updateActions()
{
    const bool workbenchIdle = state_->activeTask.empty() && !state_->closing && !state_->startupPending;
    const bool idle = workbenchIdle && state_->service.isIdle();
    if (state_->console)
    {
        state_->console->setWorkbenchBusy(!workbenchIdle);
    }
    const auto project = state_->service.project();
    auto &actions = *state_->actions;
    actions.get(ActionId::Open)->setEnabled(idle);
    actions.get(ActionId::ShowCloud)->setEnabled(idle);
    const bool cloudAvailable =
        idle && project.cloud && !project.cloud->points.empty() && actions.get(ActionId::ShowCloud)->isChecked();
    const bool transformActive = state_->pointTool->showingPreview() || state_->transformTool->active() ||
                                 state_->edgeTool->showingPreview() || state_->holeTool->showingPreview() ||
                                 state_->thicknessTool->showingPreview() || state_->contourTool->showingPreview() ||
                                 state_->mirrorTool->showingPreview() || state_->generationTool->showingPreview() ||
                                 state_->steelTool->showingPreview() ||
                                 (state_->crossTool->showingPreview() || state_->capTool->showingPreview()) ||
                                 (state_->localTool->showingPreview() || state_->plateEditTool->showingPreview());
    const bool cloudEditable = cloudAvailable && !transformActive && actions.get(ActionId::SelectPoint)->isChecked();
    const bool modelAvailable = idle && project.model && actions.get(ActionId::ShowModel)->isChecked();
    actions.get(ActionId::Measure)->setEnabled(cloudAvailable || modelAvailable);
    if (!(cloudAvailable || modelAvailable))
    {
        actions.get(ActionId::Measure)->setChecked(false);
    }
    if (state_->edgeTool->active() && !actions.get(ActionId::SelectEdge)->isChecked())
        actions.get(ActionId::SelectEdge)->setChecked(true);
    if ((state_->holeTool->active() || state_->thicknessTool->active() ||
         (state_->contourTool->active() || state_->mirrorTool->active())) &&
        !actions.get(ActionId::SelectSolid)->isChecked())
        actions.get(ActionId::SelectSolid)->setChecked(true);
    if (state_->localTool->active() && !actions.get(ActionId::SelectSolid)->isChecked())
        actions.get(ActionId::SelectSolid)->setChecked(true);
    if (state_->generationTool->active())
        actions
            .get(state_->generationTool->pickMode() == ModelPickMode::Face ? ActionId::SelectFace
                                                                           : ActionId::SelectSolid)
            ->setChecked(true);
    if (state_->steelTool->active())
        actions.get(ActionId::SelectFace)->setChecked(true);
    if ((state_->crossTool->active() || state_->capTool->active()))
        actions.get(ActionId::SelectSolid)->setChecked(true);
    if (!state_->pointTool->active())
    {
        state_->viewport->setCloudEditingEnabled(cloudEditable);
        state_->viewport->setModelPickMode(actions.get(ActionId::SelectPoint)->isChecked()  ? ModelPickMode::Vertex
                                           : actions.get(ActionId::SelectEdge)->isChecked() ? ModelPickMode::Edge
                                           : actions.get(ActionId::SelectFace)->isChecked() ? ModelPickMode::Face
                                                                                            : ModelPickMode::Body);
        state_->viewport->setModelPickingEnabled(modelAvailable && !transformActive);
    }
    state_->viewport->setThicknessEditingEnabled(
        modelAvailable && !transformActive && !state_->plateEditTool->active() && !state_->referenceTool->picking() &&
        !(state_->crossTool->active() || state_->capTool->active()) && !actions.get(ActionId::Measure)->isChecked() &&
        actions.get(ActionId::ShowThickness)->isChecked());
    state_->inspector->tree()->setEnabled(
        !state_->plateEditTool->cropping() && !transformActive &&
        ((!state_->thicknessTool->active() && !(state_->contourTool->active() || state_->mirrorTool->active())) ||
         idle));
    actions.get(ActionId::Select)->setEnabled(cloudEditable || modelAvailable);
    actions.get(ActionId::SelectPoint)->setEnabled(cloudAvailable || modelAvailable);
    actions.get(ActionId::SelectEdge)->setEnabled(modelAvailable);
    actions.get(ActionId::SelectFace)->setEnabled(modelAvailable);
    actions.get(ActionId::SelectSolid)->setEnabled(modelAvailable);
    for (const auto id : {ActionId::AddSelection, ActionId::RemoveSelection})
    {
        actions.get(id)->setEnabled(cloudEditable || modelAvailable);
    }
    const bool selected = cloudEditable && project.selection && !project.selection->empty();
    actions.get(ActionId::CropCloud)->setEnabled(selected);
    actions.get(ActionId::DeleteCloud)->setEnabled(selected);
    actions.get(ActionId::Undo)->setEnabled(idle && project.canUndo);
    actions.get(ActionId::Redo)->setEnabled(idle && project.canRedo);
    actions.get(ActionId::Reset)->setEnabled(idle && project.canReset);
    setActionHelpContext(actions.get(ActionId::Undo), project.canUndo
                                                          ? QStringLiteral("撤销：") + uiText(project.undoLabel)
                                                          : QStringLiteral("没有可撤销的工程编辑（最多保留 10 步）"));
    setActionHelpContext(actions.get(ActionId::Redo), project.canRedo
                                                          ? QStringLiteral("重做：") + uiText(project.redoLabel)
                                                          : QStringLiteral("没有可重做的工程编辑"));

    state_->recentFiles->menuAction()->setEnabled(idle);
    const auto viewPlane = positioningPlane(project);
    const auto viewKind = positioningView(project);
    const QString viewName = viewKind == PositioningView::Section  ? QStringLiteral("剖视面")
                             : viewKind == PositioningView::Bottom ? QStringLiteral("底板面")
                                                                   : QStringLiteral("上视图");
    actions.get(ActionId::BottomView)->setEnabled(idle && viewPlane.has_value());
    actions.get(ActionId::BottomView)->setText(viewName);
    setActionHelpContext(actions.get(ActionId::BottomView),
                         !viewPlane ? QStringLiteral("当前没有对应的可用平面。")
                         : viewKind == PositioningView::Top
                             ? QStringLiteral("尚未重建模型，按工程 Z 轴俯视，保持缩放。")
                             : QStringLiteral("正对当前%1，保持缩放。").arg(viewName));
    actions.get(ActionId::BasePlane)->setEnabled(cloudAvailable || modelAvailable);
    const bool sectionEditable = idle && !transformActive && !state_->referenceTool->picking();
    const bool sectionAvailable =
        sectionEditable && project.cloud && (project.referencePlane || bottomSectionPlane(project));
    actions.get(ActionId::Section)->setEnabled(sectionAvailable || (sectionEditable && project.section.enabled));
    actions.get(ActionId::Section)->setChecked(project.section.enabled);
    state_->sectionTool->sync(project, sectionEditable);
    const bool hasCloud = project.cloud && !project.cloud->points.empty();
    const bool canReconstruct =
        idle && hasCloud && (project.estimate.has_value() || state_->parameters->options().type == "other");
    actions.get(ActionId::Reconstruct)->setEnabled(canReconstruct);
    state_->parameters->setEditable(idle && hasCloud);
    const bool currentModel =
        idle && project.model &&
        (!project.model->importedFormat.empty() || (!state_->parametersDirty && !state_->parameters->hasEdits()));
    actions.get(ActionId::ExportStep)->setEnabled(currentModel);
    actions.get(ActionId::ExportModel)->setEnabled(currentModel);
    actions.get(ActionId::ExportModelCloud)->setEnabled(currentModel);
    const bool initialized = state_->service.outputInitialized();
    const auto outputDirectory = state_->service.outputDirectory();
    auto *deliver = actions.get(ActionId::ExportAndExit);
    deliver->setEnabled(initialized && currentModel);
    QString reason;
    if (!outputDirectory)
    {
        reason = QStringLiteral("尚未设置输出目录，可用 --output_dir 或 project.set_output_dir 设置");
    }
    else if (!initialized)
    {
        reason = QStringLiteral("本次结果输出未初始化");
    }
    else if (!idle)
    {
        reason = QStringLiteral("请等待当前任务结束");
    }
    else if (!currentModel)
    {
        reason = QStringLiteral("请先生成最终模型；调整参数后需重新建模");
    }
    else
    {
        reason = QStringLiteral("完整导出当前模型，提交结果 JSON 后退出");
    }
    if (outputDirectory)
    {
        reason +=
            QStringLiteral("\n目标：") + QString::fromStdWString((*outputDirectory / "reserveModel.step").wstring());
    }
    setActionHelpContext(deliver, reason);
    actions.get(ActionId::ExportCloud)->setEnabled(idle && project.cloud && !project.cloud->points.empty());
    actions.get(ActionId::Deviation)
        ->setEnabled(currentModel && project.cloud && project.model->importedFormat.empty() &&
                     project.model->edgeTreatedParts.empty() && project.model->holeEditedParts.empty() &&
                     project.model->thicknessOverrides.empty() && project.model->contourExpandedParts.empty() &&
                     project.model->mirroredParts.empty() && project.model->mirrorCompletedParts.empty() &&
                     project.model->locallyReconstructedParts.empty() &&
                     std::none_of(project.model->plates.begin(), project.model->plates.end(),
                                  [](const auto &plate) { return !plate.outline.empty() || !plate.holes.empty(); }));
    const bool cancellable = !state_->activeTask.empty() && !state_->closing && !state_->cancellationRequested;
    actions.get(ActionId::CancelTask)->setEnabled(cancellable);
    actions.get(ActionId::StopReconstruct)->setEnabled(cancellable && state_->pending == State::Operation::Reconstruct);
    state_->referenceTool->sync(project, idle, cloudAvailable, modelAvailable);
    state_->transformTool->sync(project, idle, modelAvailable);
    state_->edgeTool->sync(project, idle);
    state_->holeTool->sync(project, idle);
    state_->thicknessTool->sync(project, idle);
    state_->contourTool->sync(project, idle);
    state_->mirrorTool->sync(project, idle);
    state_->generationTool->sync(project, idle);
    state_->steelTool->sync(project, idle);
    state_->crossTool->sync(project, idle);
    state_->capTool->sync(project, idle);
    state_->localTool->sync(project, idle);
    state_->plateEditTool->sync(project, idle);
    actions.get(ActionId::BuildByPoints)->setEnabled(idle && (hasCloud || project.model));
    actions.get(ActionId::ObstacleByPoints)->setEnabled(idle && hasCloud && project.model);
    actions.get(ActionId::LocalReconstruct)->setEnabled(modelAvailable && !state_->transformTool->active());
    for (const auto id : {ActionId::CrossPlate, ActionId::EndCapPlate, ActionId::ChannelSteel, ActionId::AngleSteel,
                          ActionId::OuterPlate, ActionId::BottomPlate, ActionId::MirrorComplete})
        actions.get(id)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::MirrorCompleteAll)
        ->setEnabled(modelAvailable && !state_->transformTool->active() && project.model->partIds.size() > 1 &&
                     !state_->viewport->selectedParts().empty());
    actions.get(ActionId::ExtendContour)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::PlateThickness)->setEnabled(modelAvailable && !state_->transformTool->active());
    for (const auto id : {ActionId::WeldHole, ActionId::BottomHole})
        actions.get(id)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::Fillet)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::DeleteModel)
        ->setEnabled(modelAvailable && actions.get(ActionId::SelectSolid)->isChecked() &&
                     !actions.get(ActionId::Measure)->isChecked() && !state_->referenceTool->picking() &&
                     !state_->viewport->selectedParts().empty());
    const bool editablePart = modelAvailable && actions.get(ActionId::SelectSolid)->isChecked() &&
                              !actions.get(ActionId::Measure)->isChecked() && !state_->referenceTool->picking() &&
                              !state_->viewport->selectedParts().empty();
    actions.get(ActionId::Move)->setEnabled(editablePart);
    actions.get(ActionId::Rotate)->setEnabled(editablePart);
    if (state_->transformTool->active())
    {
        state_->viewport->setCloudEditingEnabled(false);
        state_->viewport->setModelPickingEnabled(false);
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
                              ActionId::Select, ActionId::AddSelection, ActionId::RemoveSelection, ActionId::Measure,
                              ActionId::BasePlane, ActionId::DeleteModel, ActionId::CropCloud, ActionId::DeleteCloud,
                              ActionId::ShowModel})
            actions.get(id)->setEnabled(false);
    }
    else
        actions.get(ActionId::ShowModel)->setEnabled(true);
    if (state_->edgeTool->active())
    {
        for (const auto id :
             {ActionId::SelectPoint, ActionId::SelectFace, ActionId::SelectSolid, ActionId::Measure, ActionId::Move,
              ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel, ActionId::ShowModel, ActionId::ExportStep,
              ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit})
            actions.get(id)->setEnabled(false);
    }
    if (state_->holeTool->active() || state_->thicknessTool->active() ||
        (state_->contourTool->active() || state_->mirrorTool->active()))
    {
        for (const auto id :
             {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::Measure, ActionId::Move,
              ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel, ActionId::ShowModel, ActionId::ExportStep,
              ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit})
            actions.get(id)->setEnabled(false);
    }
    if (state_->generationTool->active() || state_->steelTool->active() ||
        (state_->crossTool->active() || state_->capTool->active()))
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
                              ActionId::Measure, ActionId::Move, ActionId::Rotate, ActionId::BasePlane,
                              ActionId::DeleteModel, ActionId::ShowModel, ActionId::ExportStep, ActionId::ExportModel,
                              ActionId::ExportModelCloud, ActionId::ExportAndExit})
            actions.get(id)->setEnabled(false);
    }
    if (state_->localTool->active() || state_->plateEditTool->active())
    {
        for (const auto id :
             {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::Measure, ActionId::Move,
              ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel, ActionId::ShowModel, ActionId::ExportStep,
              ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit})
            actions.get(id)->setEnabled(false);
    }
    if (state_->plateEditTool->active() && state_->plateEditTool->direct())
    {
        for (const auto id : {ActionId::AddSelection, ActionId::RemoveSelection})
            actions.get(id)->setEnabled(false);
    }
    if (state_->referenceTool->picking())
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
                              ActionId::CropCloud, ActionId::DeleteCloud})
            actions.get(id)->setEnabled(false);
    }
    state_->pointTool->sync(project, idle);
    if (state_->pointTool->active())
    {
        const auto pickMode = state_->pointTool->inputPickMode();
        actions
            .get(pickMode == ModelPickMode::Face   ? ActionId::SelectFace
                 : pickMode == ModelPickMode::Body ? ActionId::SelectSolid
                                                   : ActionId::SelectPoint)
            ->setChecked(true);
        for (const auto id :
             {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
              ActionId::Measure, ActionId::BasePlane, ActionId::DeleteModel, ActionId::CropCloud, ActionId::DeleteCloud,
              ActionId::Section, ActionId::ShowThickness, ActionId::ExportAndExit})
            actions.get(id)->setEnabled(false);
        state_->viewport->setThicknessEditingEnabled(false);
        for (const auto id : {ActionId::Select, ActionId::AddSelection, ActionId::RemoveSelection})
            actions.get(id)->setEnabled(idle && state_->pointTool->selectingObstaclePoints());
        if (pickMode == ModelPickMode::Face || state_->pointTool->showingPreview())
        {
            actions.get(ActionId::ShowModel)->setChecked(true);
        }
        actions.get(ActionId::ShowModel)
            ->setEnabled(idle && state_->pointTool->selectingObstaclePoints() && !state_->pointTool->showingPreview());
        actions.get(ActionId::ShowCloud)->setEnabled(false);
    }
    setActionHelpContext(actions.get(ActionId::BasePlane),
                         !idle                                 ? QStringLiteral("请等待当前任务完成。")
                         : !(cloudAvailable || modelAvailable) ? QStringLiteral("请先导入并显示点云或模型。")
                         : !actions.get(ActionId::BasePlane)->isEnabled()
                             ? QStringLiteral("请先退出当前添加孔、板件修改或其他编辑操作，再设置基准面。")
                             : QString());
}

void MainWindow::refreshDisplay(bool fitView)
{
    state_->edgeTool->cancel();
    state_->holeTool->cancel();
    state_->thicknessTool->cancel();
    state_->contourTool->cancel();
    state_->mirrorTool->cancel();
    state_->generationTool->cancel();
    state_->steelTool->cancel();
    state_->crossTool->cancel();
    if (!state_->capTool->keepActiveOnRefresh())
        state_->capTool->cancel();
    state_->localTool->cancel();
    state_->pointTool->cancel();
    state_->transformTool->cancel();
    state_->referenceTool->cancel();
    const auto project = state_->service.project();
    state_->displayedCloudId = project.cloudId;
    state_->displayedModelId = project.modelId;
    state_->displayedHistoryRestoreRevision = project.historyRestoreRevision;
    std::vector<CloudDisplayPoint> points;
    Vec3 origin = project.model ? project.model->frame.originMm : Vec3{};
    if (project.cloud)
    {
        origin = project.cloud->originMm;
        const auto &source = project.cloud->points;
        const Vec3 heightAxis = project.estimate ? project.estimate->frame.zAxis : Vec3{0, 0, 1};
        points = makeCloudDisplayPoints(*project.cloud, heightAxis);
        appendLog(QStringLiteral("点云显示 %1 / %2 个有效点，显示采样不参与建模精度判断。")
                      .arg(static_cast<qulonglong>(points.size()))
                      .arg(static_cast<qulonglong>(source.size())));
    }
    state_->viewport->setPointCloud(std::move(points), project.selection);
    if (project.mesh && project.model)
    {
        state_->viewport->setModel(makeModelDisplayParts(*project.mesh, *project.model, origin));
        state_->actions->get(ActionId::ShowModel)->setChecked(true);
        state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        state_->actions->get(ActionId::Select)->setChecked(true);
        state_->viewport->setCloudSelectionMode(CloudSelectionMode::Replace);
        state_->workspace->showPanel(QStringLiteral("dock.modelTree"));
    }
    else
    {
        state_->viewport->setModel({});
        state_->actions->get(ActionId::SelectPoint)->setChecked(true);
    }
    state_->viewport->setReferencePlane(project.referencePlane, project.section, origin,
                                        selectedSectionPlane(project, project.section.source));
    if (fitView)
    {
        if (const auto plane = positioningPlane(project))
            state_->viewport->showBottomView(plane->normal, plane->xAxis);
        state_->viewport->fitAll();
    }
    refreshProjectPanels();
}

void MainWindow::refreshProjectPanels()
{
    state_->inspector->setProject(state_->service.project());
}

void MainWindow::restoreCommittedParameters()
{
    const auto project = state_->service.project();
    state_->referenceTool->cancel();
    state_->viewport->setMeasurementEnabled(false);
    state_->actions->get(ActionId::Measure)->setChecked(false);
    state_->parameters->setOptions(project.committedRecognitionOptions.value_or(
        project.model && project.model->fromPointCloud ? project.model->recognitionOptions : PlateBuildOptions{}));
    state_->parametersDirty = !project.model;
}

void MainWindow::pollTask()
{
    if (state_->service.exitRequested() && state_->service.shutdownComplete())
    {
        state_->console->flushUiCommands();
        close();
        return;
    }
    if (state_->closing)
    {
        if (state_->service.shutdownComplete())
        {
            close();
        }
        return;
    }
    if (state_->activeTask.empty())
    {
        updateActions();
        return;
    }
    try
    {
        const auto task = state_->service.task(state_->activeTask);
        state_->progress->setValue(qRound(std::clamp(task.progress, 0.0, 1.0) * 1000.0));
        state_->status->setText(uiText(task.message));
        const QStringList states{QStringLiteral("排队"), QStringLiteral("运行中"), QStringLiteral("已完成"),
                                 QStringLiteral("失败"), QStringLiteral("已取消")};
        state_->taskDetails->setText(QStringLiteral("任务：%1\n状态：%2\n进度：%3%\n%4")
                                         .arg(uiText(task.operation), states[static_cast<int>(task.state)])
                                         .arg(task.progress * 100.0, 0, 'f', 1)
                                         .arg(uiText(task.message)));
        if (!task.finished())
        {
            return;
        }
        state_->console->flushUiCommands();
        const auto operation = state_->pending;
        const bool commandEchoed = operation != State::Operation::StartRecording;
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        if (task.state == TaskState::Succeeded)
        {
            const auto project = state_->service.project();
            if (!commandEchoed)
            {
                appendLog(QStringLiteral("完成：") + uiText(task.message));
            }
            if (operation == State::Operation::SelectCloud)
            {
                state_->viewport->setCloudSelection(project.selection, true);
                const auto count = project.selection ? project.selection->size() : 0;
                state_->status->setText(
                    QStringLiteral("已选择 %1 个点；拖边/角缩放，拖框内移动；右键截取或删除，Esc 清空").arg(count));
                refreshProjectPanels();
            }
            else if (operation == State::Operation::History)
            {
                restoreCommittedParameters();
                refreshDisplay(false);
            }
            else if (operation == State::Operation::EditThickness)
            {
                refreshDisplay(false);
                state_->viewport->setSelectedParts({state_->thicknessEditPart});
                state_->inspector->selectParts({state_->thicknessEditPart});
            }
            else if (operation == State::Operation::SelectMirrorAssembly)
            {
                if (state_->cancellationRequested)
                {
                    updateActions();
                    return;
                }
                if (project.revision != state_->mirrorSelectionRevision)
                    throw std::runtime_error("工程已变化，请重新选择工件。");
                std::vector<std::string> ids;
                for (const auto &id : std::get<rm::Array>(task.result.value))
                    ids.push_back(std::get<std::string>(id));
                state_->mirrorAssemblyParts = ids;
                state_->viewport->setSelectedParts(ids);
                state_->inspector->selectParts(ids);
                if (ids.size() < 2)
                    throw std::runtime_error("选中的部件没有相连板件，未执行镜像补全。");
                appendLog(QStringLiteral("全部镜像补全：已自动选中相连工件，共 %1 个部件。").arg(ids.size()));
                MirrorCompletion request;
                if (std::find(ids.begin(), ids.end(), "web") != ids.end())
                    request.planePartId = "web";
                else
                {
                    QStringList choices;
                    for (const auto &id : ids)
                        choices.push_back(uiText(id));
                    bool accepted = false;
                    const auto plane =
                        QInputDialog::getItem(this, QStringLiteral("全部镜像补全"),
                                              QStringLiteral("已选中相连工件，请选择其中的镜像参考底板（腹板）："),
                                              choices, 0, false, &accepted);
                    if (!accepted || plane.isEmpty())
                    {
                        updateActions();
                        return;
                    }
                    request.planePartId = plane.toStdString();
                }
                for (const auto &id : ids)
                    if (id != request.planePartId)
                        request.partIds.push_back(id);
                state_->activeTask = state_->service.submitMirrorCompletion(request, state_->mirrorSelectionRevision);
                state_->pending = State::Operation::MirrorCompleteAll;
                state_->cancellationRequested = false;
                state_->progress->setValue(0);
            }
            else if (operation == State::Operation::MirrorCompleteAll)
            {
                refreshDisplay(false);
                auto selected = state_->mirrorAssemblyParts;
                if (project.model)
                    for (const auto &[id, source] : project.model->mirroredParts)
                        if (std::find(state_->mirrorAssemblyParts.begin(), state_->mirrorAssemblyParts.end(), source) !=
                                state_->mirrorAssemblyParts.end() &&
                            std::find(selected.begin(), selected.end(), id) == selected.end())
                            selected.push_back(id);
                state_->viewport->setSelectedParts(selected);
                state_->inspector->selectParts(selected);
            }
            else if (operation == State::Operation::DeleteModel)
            {
                refreshDisplay(false);
            }
            else if (operation == State::Operation::EditCloud && project.cloudId == state_->cloudBeforeEdit)
            {
                state_->status->setText(QStringLiteral("点云未变化，保留当前模型和选区。"));
            }
            else if ((operation == State::Operation::Import || operation == State::Operation::EditCloud) &&
                     project.cloud)
            {
                if (operation == State::Operation::Import)
                {
                    state_->recentFiles->recordOpened(uiText(project.sourcePathUtf8));
                }
                state_->parameters->setOptions(project.committedRecognitionOptions.value_or(
                    project.model && project.model->fromPointCloud ? project.model->recognitionOptions
                                                                   : PlateBuildOptions{}));
                state_->parametersDirty = !project.model;
                if (operation == State::Operation::EditCloud)
                    state_->status->setText(QStringLiteral("当前点数：%1").arg(project.cloud->points.size()));
                refreshDisplay(operation == State::Operation::Import);
                if (!state_->closing)
                {
                    state_->workspace->showPanel(QStringLiteral("dock.reconstruction"));
                }
            }
            else if (operation == State::Operation::Import && project.model)
            {
                state_->recentFiles->recordOpened(uiText(project.sourcePathUtf8));
                state_->parametersDirty = false;
                state_->parameters->setOptions({});
                state_->actions->get(ActionId::ShowModel)->setChecked(true);
                refreshDisplay(true);
            }
            else if (operation == State::Operation::Reconstruct && project.model)
            {
                state_->parametersDirty = false;
                state_->parameters->acceptEdits();
                refreshDisplay();
            }
            else if (operation == State::Operation::Deviation && !state_->closing)
            {
                state_->workspace->showPanel(QStringLiteral("dock.commands"));
            }
        }
        else if (task.state == TaskState::Cancelled)
        {
            if (!commandEchoed)
                appendLog(QStringLiteral("任务已取消，保留原工程状态。"));
        }
        else
        {
            auto message = uiText(task.errorCode) + QStringLiteral(": ") + uiText(task.message);
            if (state_->startupPending)
            {
                if (operation == State::Operation::InitializeOutput)
                {
                    message.prepend(QStringLiteral("本次结果输出未初始化："));
                }
                if (!commandEchoed)
                    appendLog(message);
                state_->status->setText(message);
                state_->workspace->showPanel(QStringLiteral("dock.commands"));
            }
            else
            {
                reportError(message, !commandEchoed);
            }
        }
        if (operation == State::Operation::SelectCloud && task.state != TaskState::Succeeded)
        {
            state_->viewport->setCloudSelection(state_->service.project().selection);
        }
        if (operation == State::Operation::StartRecording && task.state != TaskState::Succeeded)
        {
            state_->startup = {};
            state_->startupPending = false;
            appendLog(QStringLiteral("启动录制失败，已停止后续启动操作。"));
        }
        updateActions();
        if (state_->startupPending)
        {
            continueStartup();
        }
    }
    catch (const std::exception &error)
    {
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        reportError(uiText(error.what()));
        updateActions();
    }
}

} // namespace reverse::workbench
