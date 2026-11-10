/* 文件描述：工作台文件操作和显式命令提交，集中处理输入与错误提示。 */

#include <exception>
#include <string>
#include <string_view>

#include <QAction>
#include <QFileDialog>
#include <QInputDialog>
#include <QMenu>
#include <QProgressBar>
#include <QSettings>

#include "commands/PlateEditCommands.h"
#include "workbench/CrossPlateTool.h"
#include "workbench/DockWorkspace.h"
#include "workbench/EdgeTreatmentTool.h"
#include "workbench/EndCapTool.h"
#include "workbench/HoleTool.h"
#include "workbench/LocalReconstructionTool.h"
#include "workbench/MainWindowState.h"
#include "workbench/ModelTransformTool.h"
#include "workbench/OccViewport.h"
#include "workbench/PlateAdjustmentTool.h"
#include "workbench/PlateEditHelp.h"
#include "workbench/PlateEditTool.h"
#include "workbench/PlateGenerationTool.h"
#include "workbench/PointModelingTool.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/UiConversions.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"

namespace reverse::workbench
{
void MainWindow::importCloud()
{
    const auto path = QFileDialog::getOpenFileName(
        this, QStringLiteral("打开点云或模型"),
        QSettings()
            .value(QStringLiteral("files/lastImportDirectory"), QStringLiteral("D:/projectWQZ/other-resource/cloud"))
            .toString(),
        QStringLiteral(
            "支持的文件 (*.pcd *.step *.stp *.cwrm);;PCD 点云 (*.pcd);;STEP 模型 (*.step *.stp);;CWRM 模型 (*.cwrm)"));
    importCloud(path);
}

void MainWindow::importCloud(const QString &path)
{
    if (path.isEmpty() || !state_->activeTask.empty() || state_->closing)
    {
        return;
    }
    try
    {
        state_->activeTask = state_->service.submitImport(nativePath(path));
        state_->pending = State::Operation::Import;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::restoreRecognition()
{
    PlateBuildOptions defaults;
    defaults.type = state_->parameters->options().type;
    state_->parameters->setOptions(defaults);
    state_->parametersDirty = true;
    appendLog(QStringLiteral("已恢复默认识别参数，请重新生成模型。"));
    updateActions();
}

void MainWindow::reconstruct()
{
    try
    {
        const auto options = state_->parameters->options();
        state_->activeTask = state_->service.submitRecognizedReconstruct(options, state_->service.project().revision);
        state_->pending = State::Operation::Reconstruct;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::cancelTask()
{
    if (!state_->activeTask.empty() && state_->service.cancel(state_->activeTask))
    {
        state_->cancellationRequested = true;
        appendLog(QStringLiteral("已请求取消，等待当前阶段安全结束。"));
        updateActions();
    }
}

void MainWindow::exportStep()
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出并回读验证 STEP（请选择新文件名）"), {},
                                             QStringLiteral("STEP 模型 (*.step *.stp)"));
    if (path.isEmpty())
    {
        return;
    }
    if (!path.endsWith(QStringLiteral(".step"), Qt::CaseInsensitive) &&
        !path.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive))
    {
        path += QStringLiteral(".step");
    }
    try
    {
        state_->activeTask = state_->service.submitExport(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::Export;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::exportCwrm()
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CWRM 模型（请选择新文件名）"), {},
                                             QStringLiteral("CWRM 模型 (*.cwrm)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".cwrm"), Qt::CaseInsensitive))
        path += QStringLiteral(".cwrm");
    try
    {
        state_->activeTask = state_->service.submitExportCwrm(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::Export;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}
void MainWindow::exportModelCloud()
{
    bool accepted = false;
    const double spacing = QInputDialog::getDouble(
        this, QStringLiteral("模型表面采样"), QStringLiteral("采样间距（mm），导出的是模型表面点，不是原始扫描点云："),
        2.0, 0.01, 100000.0, 2, &accepted);
    if (!accepted)
        return;
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("模型采样导出（请选择新文件名）"), {},
                                             QStringLiteral("PCD 点云 (*.pcd)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".pcd"), Qt::CaseInsensitive))
        path += QStringLiteral(".pcd");
    try
    {
        state_->activeTask =
            state_->service.submitExportModelCloud(nativePath(path), spacing, state_->service.project().revision);
        state_->pending = State::Operation::ExportCloud;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::exportAndExit()
{
    // 当前产品没有可取消的工程保存询问；点击本按钮即确认当前完整模型。
    updateActions();
    if (!state_->actions->get(ActionId::ExportAndExit)->isEnabled())
    {
        return;
    }
    try
    {
        state_->activeTask = state_->service.submitExportAndExit(state_->service.project().revision);
        state_->pending = State::Operation::ExportAndExit;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::exportCloud()
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出点云（请选择新文件名）"), {},
                                             QStringLiteral("PCD 点云 (*.pcd)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".pcd"), Qt::CaseInsensitive))
        path += QStringLiteral(".pcd");
    try
    {
        state_->activeTask = state_->service.submitExportCloud(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::ExportCloud;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::evaluateDeviation()
{
    try
    {
        state_->activeTask = state_->service.submitDeviation(state_->service.project().revision);
        state_->pending = State::Operation::Deviation;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::editCloud(ActionId action)
{
    if (!state_->activeTask.empty() || state_->closing)
    {
        return;
    }
    const auto project = state_->service.project();
    try
    {
        state_->cloudBeforeEdit = project.cloudId;
        if (action == ActionId::CropCloud || action == ActionId::DeleteCloud)
        {
            state_->activeTask = state_->service.submitCloudEdit(
                action == ActionId::CropCloud ? CloudEditMode::KeepSelection : CloudEditMode::DeleteSelection,
                project.revision);
        }
        else
        {
            state_->referenceTool->cancel();
            state_->viewport->setMeasurementEnabled(false);
            state_->actions->get(ActionId::Measure)->setChecked(false);
            state_->activeTask =
                state_->service.submitProjectHistory(action == ActionId::Undo   ? CloudHistoryAction::Undo
                                                     : action == ActionId::Redo ? CloudHistoryAction::Redo
                                                                                : CloudHistoryAction::Reset,
                                                     project.revision);
        }
        state_->pending = action == ActionId::CropCloud || action == ActionId::DeleteCloud ? State::Operation::EditCloud
                                                                                           : State::Operation::History;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(uiText(error.what()));
    }
}

void MainWindow::beginPlateEdit(const std::string &operation, bool all)
{
    if (!state_->service.isIdle() || state_->closing)
        return;
    state_->plateEditTool->sync(state_->service.project(), true);
    const auto ids = state_->viewport->selectedParts();
    state_->plateEditTool->cancel();
    state_->localTool->cancel();
    state_->pointTool->cancel();
    state_->generationTool->cancel();
    state_->steelTool->cancel();
    state_->crossTool->cancel();
    state_->capTool->cancel();
    state_->contourTool->cancel();
    state_->mirrorTool->cancel();
    state_->thicknessTool->cancel();
    state_->holeTool->cancel();
    state_->edgeTool->cancel();
    state_->transformTool->cancel();
    state_->referenceTool->cancel();
    state_->actions->get(ActionId::Measure)->setChecked(false);
    state_->actions->get(ActionId::SelectSolid)->setChecked(true);
    state_->actions->get(ActionId::Select)->setChecked(true);
    state_->viewport->setCloudSelectionMode(CloudSelectionMode::Replace);
    if (operation == "holes")
    {
        state_->operation->showCustom(uiText(plateEditTitle(operation)), state_->plateEditTool);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
    }
    else
        state_->operation->showOperation(ActionId::Count, QStringLiteral("操作"));
    state_->plateEditTool->begin(operation, ids, all);
    state_->viewport->setFocus();
}
void MainWindow::modelContextMenu(const QPoint &position)
{
    if (state_->plateEditTool->active())
    {
        QMenu menu(this);
        menu.addAction(QStringLiteral("退出板件编辑"), this, [this] { state_->plateEditTool->cancel(); });
        menu.exec(position);
        return;
    }
    QMenu menu(this);
    const auto project = state_->service.project();
    const auto ids = state_->viewport->selectedParts();
    auto *title = menu.addAction(QStringLiteral("已选 %1 个部件").arg(ids.size()));
    title->setEnabled(false);
    const bool ready =
        state_->service.isIdle() && project.model && state_->actions->get(ActionId::SelectSolid)->isChecked();
    const auto add = [&](ActionId id)
    {
        auto *source = state_->actions->get(id);
        auto *action = menu.addAction(source->icon(), source->text());
        action->setEnabled(ready && !ids.empty() && source->isEnabled());
        connect(action, &QAction::triggered, source, &QAction::trigger);
    };
    const auto operation = [&](const char *op, bool all = false)
    {
        const bool batch = std::string_view(op) == "holes";
        auto *a = menu.addAction(all ? QStringLiteral("修复全部板件孔洞")
                                     : uiText(plateEditTitle(op)) + (batch ? QStringLiteral("…") : QString()));
        a->setEnabled(ready && (all || (batch ? !ids.empty() : ids.size() == 1)));
        if (!batch)
            addPlateEditHelp(menu, *a, op, all);
        connect(a, &QAction::triggered, this, [this, op, all] { beginPlateEdit(op, all); });
    };
    add(ActionId::DeleteModel);
    menu.addSeparator();
    for (const auto *op : {"extend", "trim", "merge", "split", "crop"})
        operation(op);
    menu.addSeparator();
    operation("repair");
    operation("repair", true);
    operation("holes");
    menu.addSeparator();
    add(ActionId::MirrorComplete);
    add(ActionId::Move);
    add(ActionId::LocalReconstruct);
    menu.exec(position);
}

} // namespace reverse::workbench
