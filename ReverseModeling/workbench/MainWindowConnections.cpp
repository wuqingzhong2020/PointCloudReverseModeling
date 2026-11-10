/* 文件描述：连接文件、选择、视图及窗口动作；保持信号接收者和生命周期明确。 */

#include <exception>

#include <QAction>
#include <QLabel>
#include <QMenu>
#include <QProgressBar>

#include "commands/SectionPlane.h"
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
#include "workbench/RecentFilesMenu.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/UiConversions.h"
#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"

namespace reverse::workbench
{
void MainWindow::connectPanelClosing()
{
    using enum ActionId;
    state_->workspace->onPanelClosed = [this](const QString &id)
    {
        if (id != QStringLiteral("dock.operation"))
            return;
        state_->thicknessTool->cancel();
        state_->contourTool->cancel();
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
        state_->steelTool->cancel();
        state_->crossTool->cancel();
        state_->capTool->cancel();
        state_->localTool->cancel();
        state_->pointTool->cancel();
        state_->edgeTool->cancel();
        state_->holeTool->cancel();
        state_->transformTool->cancel();
        state_->referenceTool->cancel();
        if (!state_->plateEditTool->direct())
            state_->plateEditTool->cancel();
    };
}

void MainWindow::connectFileActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    connect(actions.get(Open), &QAction::triggered, this, [this] { importCloud(); });
    connect(state_->recentFiles, &QMenu::triggered, this,
            [this](QAction *action) { importCloud(action->data().toString()); });
    connect(actions.get(ExportStep), &QAction::triggered, this, [this] { exportStep(); });
    connect(actions.get(ExportModel), &QAction::triggered, this, [this] { exportCwrm(); });
    connect(actions.get(ExportModelCloud), &QAction::triggered, this, [this] { exportModelCloud(); });
    connect(actions.get(ExportAndExit), &QAction::triggered, this, [this] { exportAndExit(); });
    connect(actions.get(ExportCloud), &QAction::triggered, this, [this] { exportCloud(); });
}

void MainWindow::connectProjectActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    connect(actions.get(DeleteModel), &QAction::triggered, this,
            [this]
            {
                if (!state_->actions->get(ActionId::DeleteModel)->isEnabled())
                    return;
                const auto ids = state_->viewport->selectedParts();
                if (ids.empty())
                    return;
                try
                {
                    state_->activeTask =
                        state_->service.submitDeleteModelParts(ids, state_->service.project().revision);
                    state_->pending = State::Operation::DeleteModel;
                    state_->cancellationRequested = false;
                    state_->progress->setValue(0);
                    updateActions();
                }
                catch (const std::exception &error)
                {
                    appendLog(QString::fromUtf8(error.what()));
                }
            });
    connect(actions.get(Reconstruct), &QAction::triggered, this, [this] { reconstruct(); });
    connect(actions.get(Deviation), &QAction::triggered, this, [this] { evaluateDeviation(); });
    connect(actions.get(CancelTask), &QAction::triggered, this, [this] { cancelTask(); });
    connect(actions.get(StopReconstruct), &QAction::triggered, this,
            [this]
            {
                if (state_->pending == State::Operation::Reconstruct)
                {
                    cancelTask();
                }
            });
    connect(actions.get(Parameters), &QAction::triggered, this,
            [this] { state_->workspace->showPanel(QStringLiteral("dock.reconstruction")); });
}

void MainWindow::connectCloudSelection()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    connect(actions.get(ShowCloud), &QAction::toggled, state_->viewport, &OccViewport::setCloudVisible);
    connect(actions.get(ShowCloud), &QAction::toggled, this, [this] { updateActions(); });
    for (const auto id : {CropCloud, DeleteCloud, Undo, Redo, Reset})
    {
        connect(actions.get(id), &QAction::triggered, this, [this, id] { editCloud(id); });
    }
    for (const auto id : {Select, AddSelection, RemoveSelection})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    state_->viewport->setCloudSelectionMode(id == AddSelection      ? CloudSelectionMode::Add
                                                            : id == RemoveSelection ? CloudSelectionMode::Remove
                                                                                    : CloudSelectionMode::Replace);
                });
    }
    // 删除/历史快捷键仅在视图区有焦点时生效，避免影响参数与日志中的文本编辑。
    actions.get(DeleteCloud)->setShortcut(QKeySequence::Delete);
    actions.get(Undo)->setShortcut(QKeySequence::Undo);
    actions.get(Redo)->setShortcut(QKeySequence::Redo);
    for (const auto id : {DeleteCloud, Undo, Redo})
    {
        actions.get(id)->setShortcutContext(Qt::WidgetShortcut);
        state_->viewport->addAction(actions.get(id));
    }
    state_->viewport->onCloudSelection =
        [this](const CloudSelectionRect &rectangle, CloudSelectionMode mode, bool editLast)
    {
        if (!state_->activeTask.empty() || state_->closing)
        {
            return;
        }
        const auto project = state_->service.project();
        if (mode == CloudSelectionMode::Clear && (!project.selection || project.selection->empty()))
        {
            return;
        }
        try
        {
            state_->activeTask = state_->service.submitCloudSelection(rectangle, mode, project.revision, editLast);
            state_->pending = State::Operation::SelectCloud;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error)
        {
            reportError(uiText(error.what()));
        }
    };
}

void MainWindow::connectCloudContextMenu()
{
    using enum ActionId;
    state_->viewport->onCloudContextMenu = [this](const QPoint &position)
    {
        if (state_->pointTool->active())
        {
            QMenu menu(this);
            menu.addAction(QStringLiteral("退出选点建模"), this, [this] { state_->pointTool->cancel(); });
            menu.exec(position);
            return;
        }
        if (state_->plateEditTool->active())
        {
            QMenu menu(this);
            menu.addAction(QStringLiteral("退出板件编辑"), this, [this] { state_->plateEditTool->cancel(); });
            menu.exec(position);
            return;
        }
        if (state_->actions->get(ActionId::SelectSolid)->isChecked() &&
            !state_->actions->get(ActionId::Measure)->isChecked() && !state_->referenceTool->picking() &&
            !state_->transformTool->active() && !state_->generationTool->active() && !state_->steelTool->active() &&
            !(state_->crossTool->active() || state_->capTool->active()) && !state_->contourTool->active() &&
            !state_->localTool->active() && !state_->mirrorTool->active() && !state_->thicknessTool->active() &&
            !state_->holeTool->active() && !state_->edgeTool->active())
        {
            modelContextMenu(position);
            return;
        }
        QMenu menu(this);
        if (state_->capTool->active())
            menu.addAction(QStringLiteral("退出封板优化"), this, [this] { state_->capTool->cancel(); });
        else if (state_->crossTool->active())
            menu.addAction(QStringLiteral("退出交叉板优化"), this,
                           [this]
                           {
                               state_->crossTool->cancel();
                               state_->capTool->cancel();
                           });
        if (state_->steelTool->active())
            menu.addAction(QStringLiteral("退出型钢调整"), this,
                           [this]
                           {
                               state_->steelTool->cancel();
                               state_->crossTool->cancel();
                               state_->capTool->cancel();
                           });
        if (state_->generationTool->active())
            menu.addAction(QStringLiteral("退出生成板件"), this, [this] { state_->generationTool->cancel(); });
        if (state_->contourTool->active())
            menu.addAction(QStringLiteral("退出外扩板件轮廓"), this, [this] { state_->contourTool->cancel(); });
        if (state_->localTool->active())
            menu.addAction(QStringLiteral("退出局部重建"), this,
                           [this]
                           {
                               state_->localTool->cancel();
                               state_->pointTool->cancel();
                           });
        if (state_->mirrorTool->active())
            menu.addAction(QStringLiteral("退出镜像补全"), this, [this] { state_->mirrorTool->cancel(); });
        if (state_->thicknessTool->active())
            menu.addAction(QStringLiteral("退出修改板厚"), this, [this] { state_->thicknessTool->cancel(); });
        if (state_->holeTool->active())
            menu.addAction(QStringLiteral("退出添加孔"), this, [this] { state_->holeTool->cancel(); });
        if (state_->edgeTool->active())
            menu.addAction(QStringLiteral("退出圆角 / 倒角"), this, [this] { state_->edgeTool->cancel(); });
        if (state_->transformTool->active())
        {
            menu.addAction(QStringLiteral("取消移动 / 旋转"), this, [this] { state_->transformTool->cancel(); });
            menu.exec(position);
            return;
        }
        if (state_->referenceTool->picking())
        {
            menu.addAction(QStringLiteral("退出基准面设置"), this, [this] { state_->referenceTool->cancel(); });
            menu.exec(position);
            return;
        }
        auto *measure = state_->actions->get(ActionId::Measure);
        if (measure->isChecked())
        {
            menu.addAction(measure->icon(), QStringLiteral("退出测量"), this,
                           [measure] { measure->setChecked(false); });
            menu.addSeparator();
        }
        menu.addAction(state_->actions->get(ActionId::CropCloud));
        menu.addAction(state_->actions->get(ActionId::DeleteCloud));
        menu.exec(position);
    };
}

void MainWindow::connectViewActions()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    auto *hint = state_->viewportHint;
    connect(actions.get(Measure), &QAction::toggled, state_->viewport, &OccViewport::setMeasurementEnabled);
    state_->viewport->onMeasurementModeChanged = [this, hint](bool enabled)
    {
        state_->actions->get(ActionId::Measure)->setChecked(enabled);
        hint->setText(
            enabled
                ? QStringLiteral("测量：依次左键选两点，第三次点击开始下一组 · 右键菜单 / Esc 退出 · 单位 mm")
                : QStringLiteral(
                      "左键拾取 · Shift 点击追加/取消 · 点模式拖框选择点云 · 右键旋转 · 中键平移 · 滚轮缩放 · Esc 清空"));
    };
    state_->viewport->onMeasurementCompleted = [this](const PointMeasurementResult &result)
    {
        appendLog(QStringLiteral("测量：距离 %1 mm；X %2 mm；Y %3 mm；Z %4 mm（工程坐标轴）。")
                      .arg(result.distanceMm, 0, 'f', 2)
                      .arg(result.componentsMm.x, 0, 'f', 2)
                      .arg(result.componentsMm.y, 0, 'f', 2)
                      .arg(result.componentsMm.z, 0, 'f', 2));
    };

    connect(actions.get(CloudHeight), &QAction::toggled, state_->viewport, &OccViewport::setCloudHeightColors);
    connect(actions.get(ShowModel), &QAction::toggled, state_->viewport, &OccViewport::setModelVisible);
    connect(actions.get(ShowModel), &QAction::toggled, this, [this] { updateActions(); });
    for (const auto id : {ModelSolid, ModelWire})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    const bool wireframe = id == ActionId::ModelWire;
                    auto *action = state_->actions->get(ActionId::ShowModel);
                    state_->viewport->setModelWireframe(wireframe);
                    action->setText(wireframe ? QStringLiteral("线框") : QStringLiteral("实体"));
                    setActionHelp(action,
                                  wireframe ? QStringLiteral("显示或隐藏模型线框，透过表面查看真实拓扑边。")
                                            : QStringLiteral("显示或隐藏模型实体表面。"),
                                  QStringLiteral("点击图标/文字切换显隐，右侧箭头选择实体或线框。"),
                                  QStringLiteral("只改变显示，不影响几何和导出；部分编辑期间锁定显示。"));
                    action->setChecked(true);
                    updateActions();
                });
    }
    for (const auto id : {SelectPoint, SelectEdge, SelectFace, SelectSolid})
    {
        connect(actions.get(id), &QAction::toggled, this,
                [this](bool checked)
                {
                    if (checked)
                    {
                        state_->viewport->setSelectedPart({});
                        state_->inspector->selectPart({});
                    }
                    updateActions();
                });
    }
    connect(actions.get(ShowThickness), &QAction::toggled, state_->viewport, &OccViewport::setThicknessVisible);
    connect(actions.get(BottomView), &QAction::triggered, this,
            [this]
            {
                const auto project = state_->service.project();
                if (const auto plane = positioningPlane(project))
                    state_->viewport->showBottomView(plane->normal, plane->xAxis);
            });
}

void MainWindow::connectPanelControls()
{
    using enum ActionId;
    connect(state_->parameters, &ReconstructionPanel::edited, this, [this] { updateActions(); });
    connect(state_->parameters, &ReconstructionPanel::restoreRequested, this, [this] { restoreRecognition(); });
    connect(state_->parameters, &ReconstructionPanel::closeRequested, this,
            [this] { state_->workspace->hidePanel(QStringLiteral("dock.reconstruction")); });
    connect(state_->operation, &OperationPanel::closeRequested, this,
            [this] { state_->workspace->hidePanel(QStringLiteral("dock.operation")); });
}

} // namespace reverse::workbench
