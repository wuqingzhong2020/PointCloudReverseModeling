/*
 * 文件描述：
 * 装配 SARibbon / ADS 桌面工作台，协调有效参数、现有异步命令、任务进度及 OCCT 显示。
 * 工具窗口仅管理 UI；未接入的操作表单不提交命令，关闭主窗口时等待后台任务安全退出。
 */
#include <algorithm>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <SARibbonBar.h>

#include "commands/CommandService.h"
#include "workbench/DockWorkspace.h"
#include "workbench/CommandConsoleWidget.h"
#include "workbench/MainWindow.h"
#include "workbench/ModelDisplayData.h"
#include "workbench/ModelInspector.h"
#include "workbench/OccViewport.h"
#include "workbench/PointCloudDisplayData.h"
#include "workbench/RecentFilesMenu.h"
#include "workbench/RibbonBuilder.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::filesystem::path nativePath(const QString &value)
{
#if defined(_WIN32)
    return std::filesystem::path(value.toStdWString());
#else
    const auto utf8 = value.toUtf8();
    return std::filesystem::path(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
#endif
}

QString vectorText(const Vec3 &value)
{
    return QStringLiteral("(%1, %2, %3)").arg(value.x, 0, 'g', 8).arg(value.y, 0, 'g', 8).arg(value.z, 0, 'g', 8);
}

CommandEnvironment commandEnvironment()
{
    CommandEnvironment result;
    result.runtimeDirectory = nativePath(QCoreApplication::applicationDirPath());
    result.logDirectory = nativePath(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)) / "log";
    for (auto path = result.runtimeDirectory; !path.empty() && path != path.parent_path(); path = path.parent_path())
    {
        if (path.filename() == "output")
        {
            result.logDirectory = path / "log";
            break;
        }
    }
    return result;
}

QLabel *label(const QString &value, QWidget *parent)
{
    auto *result = new QLabel(value, parent);
    result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
} // namespace

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
        Export,
        ExportCloud,
        Deviation,
        SelectCloud,
        EditCloud
    };
    CommandService service{commandEnvironment()};
    std::string activeTask;
    StartupRequest startup;
    bool startupPending{};
    Operation pending{Operation::None};
    std::string cloudBeforeEdit;
    bool closing{};
    bool cancellationRequested{};
    bool parametersDirty{true};
    std::optional<BeamParameters> appliedParameters;
    OccViewport *viewport{};
    WorkbenchActions *actions{};
    RecentFilesMenu *recentFiles{};
    DockWorkspace *workspace{};
    ReconstructionPanel *parameters{};
    OperationPanel *operation{};
    ModelInspector *inspector{};
    CommandConsoleWidget *console{};
    QLabel *taskDetails{};
    QLabel *status{};
    QProgressBar *progress{};
    QTimer *timer{};
};

MainWindow::MainWindow(QWidget *parent, StartupRequest startup)
    : SARibbonMainWindow(parent,
                         SARibbonMainWindowStyleFlag::UseRibbonMenuBar | SARibbonMainWindowStyleFlag::UseNativeFrame),
      state_(std::make_unique<State>())
{
    state_->startup = std::move(startup);
    state_->startupPending = true;
    using enum ActionId;
    setObjectName(QStringLiteral("ReverseModeler.MainWindow"));
    setStyleSheet(qApp->styleSheet());
    setWindowTitle(QStringLiteral("ReverseModeler · 点云辅助逆向建模"));
    resize(1500, 920);
    state_->actions = new WorkbenchActions(this);
    auto &actions = *state_->actions;
    for (int i = 0; i < static_cast<int>(Count); ++i)
    {
        addAction(actions.get(static_cast<ActionId>(i)));
    }
    auto *viewContainer = new QWidget(this);
    auto *viewLayout = new QVBoxLayout(viewContainer);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(0);
    state_->viewport = new OccViewport(viewContainer);
    state_->viewport->setObjectName(QStringLiteral("viewport.occ"));
    state_->viewport->onError = [this](const QString &error)
    {
        appendLog(QStringLiteral("视图错误：") + error);
    };
    state_->workspace = new DockWorkspace(this, viewContainer);
    viewLayout->addWidget(createViewToolbar(viewContainer, actions));
    viewLayout->addWidget(state_->viewport, 1);
    auto *hint =
        label(QStringLiteral("左键选部件 / 框选点云 · 右键拖动旋转 · 中键平移 · 滚轮缩放 · Esc 清空选择"), viewContainer);
    hint->setProperty("muted", true);
    hint->setContentsMargins(12, 5, 6, 5);
    viewLayout->addWidget(hint);
    state_->inspector = new ModelInspector(this);
    state_->inspector->onPartSelected = [this](const std::string &id)
    {
        if (!id.empty())
        {
            state_->actions->get(ActionId::ShowModel)->setChecked(true);
            state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        }
        state_->viewport->setSelectedPart(id);
    };
    state_->viewport->onModelPartSelected = [this](const std::string &id)
    {
        state_->inspector->selectPart(id);
        if (!id.empty())
        {
            state_->workspace->showPanel(QStringLiteral("dock.modelTree"));
            state_->workspace->showPanel(QStringLiteral("dock.properties"));
        }
    };
    state_->parameters = new ReconstructionPanel(this);
    state_->operation = new OperationPanel(this);
    auto *console = new CommandConsoleWidget(state_->service, actions, this);
    state_->console = console;
    console->onShowRequested = [this] { state_->workspace->showPanel(QStringLiteral("dock.commands")); };
    // 先建立“上方工作区 + 底部命令”，再在工作区左侧加工具页、最外层右侧加模型树/属性。
    // 这样命令窗口横跨左侧工具页和三维视图，而右侧栏始终占满工作区高度。
    state_->workspace->addPanel(QStringLiteral("dock.commands"), QStringLiteral("命令窗口"), console, DockPosition::Bottom);
    state_->workspace->addPanel(QStringLiteral("dock.reconstruction"), QStringLiteral("重建参数"), state_->parameters,
                                DockPosition::Left);
    state_->workspace->addPanel(QStringLiteral("dock.operation"), QStringLiteral("操作面板"), state_->operation,
                                DockPosition::Left);
    state_->workspace->addPanel(QStringLiteral("dock.modelTree"), QStringLiteral("模型树"), state_->inspector->tree(),
                                DockPosition::Right);
    state_->workspace->addPanel(QStringLiteral("dock.properties"), QStringLiteral("属性"), state_->inspector->properties(),
                                DockPosition::Right, QStringLiteral("dock.modelTree"));
    console->onProjectChanged = [this, lastCloud = std::string{}, lastModel = std::string{}]() mutable
    {
        const auto project = state_->service.project();
        state_->parameters->setRecognition(project.recognition);
        state_->parameters->setSourceText(text(project.sourcePathUtf8));
        state_->parameters->setModelText(project.model
            ? QStringLiteral("命令已更新模型，当前版本 %1").arg(project.revision)
            : QStringLiteral("尚无模型。"));
        state_->parametersDirty = !project.model;
        state_->appliedParameters.reset();
        state_->actions->get(ActionId::ShowCloud)->setChecked(static_cast<bool>(project.cloud));
        state_->actions->get(ActionId::ShowModel)->setChecked(static_cast<bool>(project.model));
        const bool geometryChanged = lastCloud != project.cloudId || lastModel != project.modelId;
        lastCloud = project.cloudId;
        lastModel = project.modelId;
        refreshDisplay(geometryChanged);
        refreshProjectPanels();
        updateActions();
    };
    state_->taskDetails = label(QStringLiteral("当前没有任务。"), this);
    state_->taskDetails->setObjectName(QStringLiteral("panel.tasks"));
    state_->taskDetails->setContentsMargins(14, 12, 14, 12);
    state_->taskDetails->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    state_->workspace->addPanel(QStringLiteral("dock.tasks"), QStringLiteral("任务信息"), state_->taskDetails,
                                DockPosition::Bottom);
    state_->workspace->initializeLayout();
    state_->recentFiles = new RecentFilesMenu(this);
    buildRibbon(ribbonBar(), actions, state_->workspace->windowMenu(), state_->recentFiles);
    state_->status = new QLabel(QStringLiteral("就绪"), this);
    state_->progress = new QProgressBar(this);
    state_->progress->setRange(0, 1000);
    state_->progress->setFixedWidth(180);
    state_->progress->setValue(0);
    auto *cancel = new QToolButton(this);
    cancel->setDefaultAction(actions.get(CancelTask));
    cancel->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    statusBar()->addWidget(state_->status, 1);
    statusBar()->addPermanentWidget(state_->progress);
    statusBar()->addPermanentWidget(cancel);
    connect(actions.get(Open), &QAction::triggered, this,
            [this]
            {
                importCloud();
            });
    connect(state_->recentFiles, &QMenu::triggered, this,
            [this](QAction *action)
            {
                importCloud(action->data().toString());
            });
    connect(actions.get(ExportStep), &QAction::triggered, this,
            [this]
            {
                exportStep();
            });
    connect(actions.get(ExportAndExit), &QAction::triggered, this, [this] { exportAndExit(); });
    connect(actions.get(ExportCloud), &QAction::triggered, this, [this] { exportCloud(); });
    connect(actions.get(Reconstruct), &QAction::triggered, this,
            [this]
            {
                reconstruct();
            });
    connect(actions.get(Deviation), &QAction::triggered, this,
            [this]
            {
                evaluateDeviation();
            });
    connect(actions.get(CancelTask), &QAction::triggered, this,
            [this]
            {
                cancelTask();
            });
    connect(actions.get(StopReconstruct), &QAction::triggered, this,
            [this]
            {
                if (state_->pending == State::Operation::Reconstruct)
                {
                    cancelTask();
                }
            });
    connect(actions.get(Parameters), &QAction::triggered, this,
            [this]
            {
                state_->workspace->showPanel(QStringLiteral("dock.reconstruction"));
            });
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
    state_->viewport->onCloudSelection = [this](const CloudSelectionRect &rectangle, CloudSelectionMode mode)
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
            state_->activeTask = state_->service.submitCloudSelection(rectangle, mode, project.revision);
            state_->pending = State::Operation::SelectCloud;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error)
        {
            reportError(text(error.what()));
        }
    };
    state_->viewport->onCloudContextMenu = [this](const QPoint &position)
    {
        QMenu menu(this);
        menu.addAction(state_->actions->get(ActionId::CropCloud));
        menu.addAction(state_->actions->get(ActionId::DeleteCloud));
        menu.exec(position);
    };

    connect(actions.get(CloudHeight), &QAction::toggled, state_->viewport, &OccViewport::setCloudHeightColors);
    connect(actions.get(ShowModel), &QAction::toggled, state_->viewport, &OccViewport::setModelVisible);
    connect(actions.get(ShowModel), &QAction::toggled, this, [this] { updateActions(); });
    for (const auto id : {SelectPoint, SelectSolid})
    {
        connect(actions.get(id), &QAction::toggled, this, [this, id](bool checked)
        {
            if (checked && id == ActionId::SelectPoint)
            {
                state_->viewport->setSelectedPart({});
                state_->inspector->selectPart({});
            }
            updateActions();
        });
    }
    connect(actions.get(ShowThickness), &QAction::toggled, state_->viewport, &OccViewport::setThicknessVisible);
    connect(actions.get(FitAll), &QAction::triggered, state_->viewport, &OccViewport::fitAll);
    connect(actions.get(BottomView), &QAction::triggered, this,
            [this]
            {
                const auto project = state_->service.project();
                if (project.model)
                {
                    const auto &frame = project.model->frame;
                    state_->viewport->showBottomView(frame.yAxis, frame.xAxis);
                }
                else if (project.recognition)
                {
                    const auto &frame = project.recognition->frame;
                    state_->viewport->showBottomView(frame.yAxis, frame.xAxis);
                }
            });
    for (const auto id : {BuildSelection, Obstacle, PlateThickness, ExtendContour, Channel, Angle, OuterPlate,
                          BottomPlate, WeldHole, BottomHole, Fillet, Move, Rotate, Measure, BasePlane})
    {
        connect(actions.get(id), &QAction::triggered, this,
                [this, id]
                {
                    state_->operation->showOperation(id, state_->actions->get(id)->text());
                    state_->workspace->showPanel(QStringLiteral("dock.operation"));
                });
    }
    connect(state_->parameters, &ReconstructionPanel::edited, this,
            [this]
            {
                updateActions();
            });
    connect(state_->parameters, &ReconstructionPanel::applyRequested, this,
            [this]
            {
                applyParameters();
            });
    connect(state_->parameters, &ReconstructionPanel::restoreRequested, this,
            [this]
            {
                restoreRecognition();
            });
    connect(state_->parameters, &ReconstructionPanel::closeRequested, this,
            [this]
            {
                state_->workspace->hidePanel(QStringLiteral("dock.reconstruction"));
            });
    connect(state_->operation, &OperationPanel::closeRequested, this,
            [this]
            {
                state_->workspace->hidePanel(QStringLiteral("dock.operation"));
            });
    state_->timer = new QTimer(this);
    state_->timer->setInterval(100);
    connect(state_->timer, &QTimer::timeout, this,
            [this]
            {
                pollTask();
            });
    state_->timer->start();
    appendLog(QStringLiteral("工作台就绪。点云按 mm 导入；命令及运行日志统一显示在本窗口。"));
    refreshProjectPanels();
    updateActions();
    QTimer::singleShot(0, this,
                       [this]
                       {
                           state_->workspace->restoreLayout();
                           continueStartup();
                       });
}

MainWindow::~MainWindow() = default;

void MainWindow::continueStartup()
{
    try
    {
        if (state_->startup.recordFile)
        {
            const auto path = *state_->startup.recordFile;
            state_->startup.recordFile.reset();
            state_->pending = State::Operation::StartRecording;
            state_->activeTask = state_->service.submitStartRecording(path, true);
            state_->workspace->showPanel(QStringLiteral("dock.commands"));
        }
        else if (state_->startup.outputDirectory)
        {
            const auto path = *state_->startup.outputDirectory;
            state_->startup.outputDirectory.reset();
            state_->activeTask = state_->service.submitInitializeOutput(path);
            state_->pending = State::Operation::InitializeOutput;
        }
        else if (state_->startup.inputFile)
        {
            const auto path = *state_->startup.inputFile;
            state_->startup.inputFile.reset();
            state_->activeTask = state_->service.submitStartupImport(path);
            state_->pending = State::Operation::Import;
        }
        else
        {
            state_->startupPending = false;
            updateActions();
            state_->console->executeStartup(state_->startup);
            state_->startup = {};
            return;
        }
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        if (state_->pending == State::Operation::StartRecording)
        {
            state_->startup = {};
            state_->startupPending = false;
        }
        appendLog(QStringLiteral("启动任务失败：") + text(error.what()));
        state_->workspace->showPanel(QStringLiteral("dock.commands"));
        QTimer::singleShot(0, this, [this] { continueStartup(); });
    }
}

void MainWindow::appendLog(const QString &message)
{
    if (state_->console) state_->console->appendLog(message);
}

void MainWindow::reportError(const QString &message, bool writeLog)
{
    state_->status->setText(QStringLiteral("操作失败"));
    if (writeLog) appendLog(QStringLiteral("错误：") + message);
    if (!state_->closing)
    {
        state_->workspace->showPanel(QStringLiteral("dock.commands"));
        QMessageBox::warning(this, QStringLiteral("ReverseModeler"), message);
    }
}

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
    const bool cloudAvailable =
        idle && project.cloud && !project.cloud->points.empty() && actions.get(ActionId::ShowCloud)->isChecked();
    const bool cloudEditable = cloudAvailable && actions.get(ActionId::SelectPoint)->isChecked();
    const bool modelAvailable = idle && project.model && actions.get(ActionId::ShowModel)->isChecked();
    state_->viewport->setCloudEditingEnabled(cloudEditable);
    state_->viewport->setModelPickingEnabled(modelAvailable && actions.get(ActionId::SelectSolid)->isChecked());
    actions.get(ActionId::Select)->setEnabled(cloudEditable || modelAvailable);
    actions.get(ActionId::SelectPoint)->setEnabled(cloudAvailable);
    actions.get(ActionId::SelectSolid)->setEnabled(modelAvailable);
    for (const auto id : {ActionId::AddSelection, ActionId::RemoveSelection})
    {
        actions.get(id)->setEnabled(cloudEditable);
    }
    const bool selected = cloudEditable && project.selection && !project.selection->empty();
    actions.get(ActionId::CropCloud)->setEnabled(selected);
    actions.get(ActionId::DeleteCloud)->setEnabled(selected);
    actions.get(ActionId::Undo)->setEnabled(idle && project.canUndoCloud);
    actions.get(ActionId::Redo)->setEnabled(idle && project.canRedoCloud);
    actions.get(ActionId::Reset)->setEnabled(idle && project.canResetCloud);

    state_->recentFiles->menuAction()->setEnabled(idle);
    actions.get(ActionId::BottomView)->setEnabled(idle && ((project.model && project.model->importedFormat.empty()) || project.recognition.has_value()));
    actions.get(ActionId::Reconstruct)->setEnabled(idle && project.cloud &&
        (state_->parameters->usePointCloud() ? project.recognition.has_value() : project.estimate.has_value()));
    state_->parameters->setEditable(idle && project.cloud && project.estimate.has_value(),
                                    idle && project.recognition.has_value());
    const bool currentModel = idle && project.model && (!project.model->importedFormat.empty() ||
        (!state_->parametersDirty && !state_->parameters->hasEdits()));
    actions.get(ActionId::ExportStep)->setEnabled(currentModel);
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
        reason += QStringLiteral("\n目标：") +
                  QString::fromStdWString((*outputDirectory / "reserveModel.step").wstring());
    }
    deliver->setToolTip(reason);
    actions.get(ActionId::ExportCloud)->setEnabled(idle && project.cloud && !project.cloud->points.empty());
    actions.get(ActionId::Deviation)->setEnabled(currentModel && project.cloud && project.model->importedFormat.empty());
    const bool cancellable = !state_->activeTask.empty() && !state_->closing && !state_->cancellationRequested;
    actions.get(ActionId::CancelTask)->setEnabled(cancellable);
    actions.get(ActionId::StopReconstruct)->setEnabled(cancellable && state_->pending == State::Operation::Reconstruct);
}

void MainWindow::importCloud()
{
    const auto path =
        QFileDialog::getOpenFileName(this, QStringLiteral("打开点云或模型"),
            QSettings().value(QStringLiteral("files/lastImportDirectory"),
                QStringLiteral("D:/projectWQZ/other-resource/cloud")).toString(),
            QStringLiteral("支持的文件 (*.pcd *.step *.stp *.cwrm);;PCD 点云 (*.pcd);;STEP 模型 (*.step *.stp);;柏楚模型 (*.cwrm)"));
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
        state_->activeTask = state_->service.submitImport(nativePath(path), LengthUnit::Millimeter);
        state_->pending = State::Operation::Import;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
    }
}

bool MainWindow::applyParameters()
{
    try
    {
        const auto parameters = state_->parameters->values();
        state_->appliedParameters = parameters;
        state_->parameters->acceptEdits();
        state_->parametersDirty = true;
        state_->parameters->setHint(QStringLiteral("参数已应用。请开始重建以更新模型；板厚与肋板位置仍需核对。"));
        if (state_->service.project().model)
        {
            state_->parameters->setModelText(
                QStringLiteral("视图仍显示上一次模型，重建成功后才能导出新参数对应的结果。"));
        }
        appendLog(QStringLiteral("已应用建模参数。"));
        updateActions();
        return true;
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
        return false;
    }
}

void MainWindow::restoreRecognition()
{
    const auto project = state_->service.project();
    state_->parameters->setRecognition(project.recognition);
    state_->appliedParameters.reset();
    state_->parametersDirty = true;
    if (project.model)
    {
        state_->parameters->setModelText(QStringLiteral("已恢复点云识别结果并关闭镜像，请重新生成模型。"));
    }
    appendLog(QStringLiteral("已恢复本次点云识别结果，未填入经验板厚。"));
    updateActions();
}

void MainWindow::reconstruct()
{
    if (state_->parameters->usePointCloud())
    {
        try
        {
            state_->activeTask = state_->service.submitRecognizedReconstruct(
                state_->parameters->mirrorSymmetry(), state_->service.project().revision,
                state_->parameters->endOverhangToleranceMm());
            state_->pending = State::Operation::Reconstruct;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error)
        {
            reportError(text(error.what()));
        }
        return;
    }
    if (state_->parameters->hasEdits())
    {
        QMessageBox question(QMessageBox::Question, QStringLiteral("应用参数"),
                             QStringLiteral("参数面板有尚未应用的编辑。应用这些参数并开始重建？"),
                             QMessageBox::NoButton, this);
        auto *apply = question.addButton(QStringLiteral("应用并重建"), QMessageBox::AcceptRole);
        question.addButton(QStringLiteral("取消"), QMessageBox::RejectRole);
        question.exec();
        if (question.clickedButton() != apply || !applyParameters())
        {
            return;
        }
    }
    if (!state_->appliedParameters && !applyParameters())
    {
        return;
    }
    try
    {
        state_->activeTask =
            state_->service.submitReconstruct(*state_->appliedParameters, state_->service.project().revision);
        state_->pending = State::Operation::Reconstruct;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
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
        reportError(text(error.what()));
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
        reportError(text(error.what()));
    }
}

void MainWindow::exportCloud()
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出点云（请选择新文件名）"), {},
        QStringLiteral("PCD 点云 (*.pcd)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pcd"), Qt::CaseInsensitive)) path += QStringLiteral(".pcd");
    try
    {
        state_->activeTask = state_->service.submitExportCloud(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::ExportCloud;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception& error) { reportError(text(error.what())); }
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
        reportError(text(error.what()));
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
            state_->activeTask =
                state_->service.submitCloudHistory(action == ActionId::Undo   ? CloudHistoryAction::Undo
                                                   : action == ActionId::Redo ? CloudHistoryAction::Redo
                                                                              : CloudHistoryAction::Reset,
                                                   project.revision);
        }
        state_->pending = State::Operation::EditCloud;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
    }
}

void MainWindow::refreshDisplay(bool fitView)
{
    const auto project = state_->service.project();
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
        state_->workspace->showPanel(QStringLiteral("dock.properties"));
    }
    else
    {
        state_->viewport->setModel({});
        state_->actions->get(ActionId::SelectPoint)->setChecked(true);
    }
    if (fitView)
    {
        state_->viewport->fitAll();
    }
    refreshProjectPanels();
}

void MainWindow::refreshProjectPanels()
{
    state_->inspector->setProject(state_->service.project());
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
        state_->status->setText(text(task.message));
        const QStringList states{QStringLiteral("排队"), QStringLiteral("运行中"), QStringLiteral("已完成"),
                                 QStringLiteral("失败"), QStringLiteral("已取消")};
        state_->taskDetails->setText(QStringLiteral("任务：%1\n状态：%2\n进度：%3%\n%4")
                                         .arg(text(task.operation), states[static_cast<int>(task.state)])
                                         .arg(task.progress * 100.0, 0, 'f', 1)
                                         .arg(text(task.message)));
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
                appendLog(QStringLiteral("完成：") + text(task.message));
            }
            if (operation == State::Operation::SelectCloud)
            {
                state_->viewport->setCloudSelection(project.selection);
                const auto count = project.selection ? project.selection->size() : 0;
                state_->status->setText(QStringLiteral("已选择 %1 个点；右键截取或删除，Esc 清空选区").arg(count));
                refreshProjectPanels();
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
                    state_->recentFiles->recordOpened(text(project.sourcePathUtf8));
                }
                const auto &cloud = *project.cloud;
                state_->appliedParameters.reset();
                state_->parametersDirty = true;
                BeamParameters parameters;
                parameters.length = project.estimate ? project.estimate->length : 0.0;
                parameters.width = project.estimate ? project.estimate->width : 0.0;
                parameters.height = project.estimate ? project.estimate->height : 0.0;
                state_->parameters->setValues(parameters);
                state_->parameters->setRecognition(project.recognition);
                state_->parameters->setSourceText(
                    QStringLiteral("%1\n原始点 %2；无效点 %3；当前点 %6\n1 单位 = %4 mm\n显示原点 %5 mm")
                        .arg(text(project.sourcePathUtf8))
                        .arg(static_cast<qulonglong>(cloud.sourcePointCount))
                        .arg(static_cast<qulonglong>(cloud.invalidPointCount))
                        .arg(cloud.sourceScaleToMm)
                        .arg(vectorText(cloud.originMm))
                        .arg(static_cast<qulonglong>(cloud.points.size())));
                if (project.estimate)
                {
                    const auto &estimate = *project.estimate;
                    state_->parameters->setFrameText(
                        QStringLiteral("PCA 原始框架（交换前），须核对：\n中心 %1 mm\n梁长轴 %2\n宽度轴 %3\n高度轴 %4")
                            .arg(vectorText(estimate.frame.originMm), vectorText(estimate.frame.xAxis),
                                 vectorText(estimate.frame.yAxis), vectorText(estimate.frame.zAxis)));
                    state_->parameters->setModelText(QStringLiteral("点云已导入；请核对逐板识别结果，默认仅构建观测侧。"));
                }
                else
                {
                    state_->parameters->setFrameText(QStringLiteral("当前点云不能提供可靠的三维 PCA 梁框架。"));
                    state_->parameters->setModelText(QStringLiteral("点云可查看，当前辅助建模需要可用的坐标框架。"));
                }
                if (operation == State::Operation::EditCloud)
                {
                    state_->parameters->setModelText(
                        project.model ? QStringLiteral("已恢复编辑前的模型。")
                                      : QStringLiteral("点云已更新，旧模型已清除；请检查识别结果后重新建模。"));
                    if (project.model)
                    {
                        state_->parameters->setModelValues(*project.model);
                        state_->appliedParameters = project.model->parameters;
                        state_->parametersDirty = false;
                    }
                    state_->status->setText(QStringLiteral("当前点数：%1").arg(cloud.points.size()));
                }
                refreshDisplay(operation == State::Operation::Import);
                if (!state_->closing)
                {
                    state_->workspace->showPanel(QStringLiteral("dock.reconstruction"));
                }
            }
            else if (operation == State::Operation::Import && project.model)
            {
                state_->recentFiles->recordOpened(text(project.sourcePathUtf8));
                state_->appliedParameters.reset();
                state_->parametersDirty = false;
                state_->parameters->setValues({});
                state_->parameters->setRecognition(std::nullopt);
                state_->parameters->acceptEdits();
                state_->parameters->setSourceText(text(project.sourcePathUtf8));
                state_->parameters->setFrameText(QStringLiteral("导入模型；显示及导出单位 mm。"));
                const auto description = QStringLiteral("已导入 %1 模型：%2 个实体、%3 个独立曲面，可导出 STEP。")
                    .arg(text(project.model->importedFormat).toUpper())
                    .arg(project.model->solidCount).arg(project.model->surfaceCount);
                state_->parameters->setModelText(description);
                state_->actions->get(ActionId::ShowModel)->setChecked(true);
                refreshDisplay(true);
            }
            else if (operation == State::Operation::Reconstruct && project.model)
            {
                state_->parametersDirty = false;
                state_->parameters->acceptEdits();
                state_->parameters->setModelText(
                    QStringLiteral("模型版本 %1\n%2 个板件实体，%3 个未识别厚度的观测面\n%4")
                        .arg(static_cast<qulonglong>(project.revision))
                        .arg(static_cast<qulonglong>(project.model->solidCount))
                        .arg(static_cast<qulonglong>(project.model->surfaceCount))
                        .arg(project.model->fromPointCloud ? QStringLiteral("标签区分识别厚度与底板最大值估算厚度。")
                            : QStringLiteral("手动参数模型，标签不是点云识别厚度。")));
                refreshDisplay();
            }
            else if (operation == State::Operation::Deviation && !state_->closing)
            {
                state_->workspace->showPanel(QStringLiteral("dock.commands"));
            }
        }
        else if (task.state == TaskState::Cancelled)
        {
            if (!commandEchoed) appendLog(QStringLiteral("任务已取消，保留原工程状态。"));
        }
        else
        {
            auto message = text(task.errorCode) + QStringLiteral(": ") + text(task.message);
            if (state_->startupPending)
            {
                if (operation == State::Operation::InitializeOutput)
                {
                    message.prepend(QStringLiteral("本次结果输出未初始化："));
                }
                if (!commandEchoed) appendLog(message);
                state_->status->setText(message);
                state_->workspace->showPanel(QStringLiteral("dock.commands"));
            }
            else
            {
                reportError(message, !commandEchoed);
            }
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
        reportError(text(error.what()));
        updateActions();
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    state_->service.requestShutdown();
    if (!state_->service.shutdownComplete())
    {
        state_->closing = true;
        state_->status->setText(QStringLiteral("正在取消任务并等待安全退出…"));
        updateActions();
        event->ignore();
        return;
    }
    state_->timer->stop();
    state_->workspace->saveLayout();
    event->accept();
}
} // namespace reverse::workbench
