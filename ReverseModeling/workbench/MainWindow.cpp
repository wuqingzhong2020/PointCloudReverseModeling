/*
 * 文件描述：
 * 装配 SARibbon / ADS 桌面工作台，协调有效参数、现有异步命令、任务进度及 OCCT 显示。
 * 工具窗口仅管理 UI；未接入的操作表单不提交命令，关闭主窗口时等待后台任务安全退出。
 */

#include <exception>
#include <memory>
#include <string>
#include <utility>

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QColor>
#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QProgressBar>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <SARibbonBar.h>

#include "commands/ApplicationLog.h"
#include "workbench/CommandConsoleWidget.h"
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
#include "workbench/RibbonBuilder.h"
#include "workbench/SectionTool.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/UiConversions.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchPanels.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
CommandEnvironment commandEnvironment()
{
    CommandEnvironment result;
    result.runtimeDirectory = nativePath(QCoreApplication::applicationDirPath());
    if (!logging::directory().isEmpty())
    {
        result.logDirectory = nativePath(logging::directory());
        return result;
    }
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

// 原生 OCCT 窗口旁的提示条主动铺底，避免文字更新后保留旧字形。
class ViewportHint final : public QLabel
{
  public:
    using QLabel::QLabel;

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor(7, 23, 34));
        painter.end();
        QLabel::paintEvent(event);
    }
};
QLabel *label(const QString &value, QWidget *parent)
{
    auto *result = new QLabel(value, parent);
    result->setWordWrap(true);
    result->setTextFormat(Qt::PlainText);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
} // namespace

MainWindow::MainWindow(QWidget *parent, StartupRequest startup)
    : SARibbonMainWindow(parent,
                         SARibbonMainWindowStyleFlag::UseRibbonMenuBar | SARibbonMainWindowStyleFlag::UseNativeFrame),
      state_(std::make_unique<State>(commandEnvironment()))
{
    state_->startup = std::move(startup);
    state_->startupPending = true;
    initializeActions();
    initializeViewport();
    initializeInspector();
    initializeCoreTools();
    initializePlateInteraction();
    initializeReconstructionTools();
    connectThicknessEditing();
    connectEditEscape();
    initializeDockPanels();
    connectPanelClosing();
    initializeRibbonAndStatus();
    connectFileActions();
    connectProjectActions();
    connectCloudSelection();
    connectCloudContextMenu();
    connectViewActions();
    connectSteelActions();
    connectCreationActions();
    connectAdjustmentActions();
    connectHoleTransformActions();
    connectPanelControls();
    initializePolling();
}

void MainWindow::initializeActions()
{
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
}

void MainWindow::initializeViewport()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    auto *viewContainer = new QWidget(this);
    auto *viewLayout = new QVBoxLayout(viewContainer);
    viewLayout->setContentsMargins(0, 0, 0, 0);
    viewLayout->setSpacing(0);
    state_->viewport = new OccViewport(viewContainer);
    state_->viewport->setObjectName(QStringLiteral("viewport.occ"));
    state_->viewport->onError = [this](const QString &error) { appendLog(QStringLiteral("视图错误：") + error); };
    state_->workspace = new DockWorkspace(this, viewContainer);
    state_->sectionTool = new SectionTool(state_->service, viewContainer);
    viewLayout->addWidget(createViewToolbar(viewContainer, actions, state_->sectionTool));
    auto *platePrompt = new QFrame(viewContainer);
    platePrompt->setObjectName(QStringLiteral("plateInteractionPrompt"));
    platePrompt->setStyleSheet(QStringLiteral(
        "QFrame#plateInteractionPrompt { background: #0e2634; border: 1px solid #00a9db; border-radius: 4px; }"
        "QLabel { color: #e2f4ff; border: 0; }"));
    auto *promptLayout = new QHBoxLayout(platePrompt);
    promptLayout->setContentsMargins(12, 7, 10, 7);
    auto *promptText = new QLabel(platePrompt);
    promptText->setWordWrap(true);
    promptText->setTextFormat(Qt::PlainText);
    promptLayout->addWidget(promptText, 1);
    auto *cancelPlate = new QToolButton(platePrompt);
    cancelPlate->setText(QStringLiteral("取消 (Esc)"));
    promptLayout->addWidget(cancelPlate);
    platePrompt->hide();
    viewLayout->addWidget(platePrompt);
    viewLayout->addWidget(state_->viewport, 1);
    auto *hint = new ViewportHint(
        QStringLiteral(
            "左键拾取 · Shift 点击追加/取消 · 点模式拖框选择点云 · 右键旋转 · 中键平移 · 滚轮缩放 · Esc 清空"),
        viewContainer);
    hint->setProperty("muted", true);
    hint->setAutoFillBackground(true);
    hint->setAttribute(Qt::WA_OpaquePaintEvent);
    hint->setStyleSheet(QStringLiteral("background-color: #071722; color: #a9c8da;"));
    hint->setWordWrap(true);
    hint->setContentsMargins(12, 5, 6, 5);
    viewLayout->addWidget(hint);
    state_->platePrompt = platePrompt;
    state_->platePromptText = promptText;
    state_->cancelPlateButton = cancelPlate;
    state_->viewportHint = hint;
}

void MainWindow::initializeInspector()
{
    using enum ActionId;
    state_->inspector = new ModelInspector(this);
    state_->inspector->onPartsSelected = [this](const std::vector<std::string> &ids)
    {
        const bool switching = !state_->actions->get(ActionId::SelectSolid)->isChecked() ||
                               !state_->actions->get(ActionId::ShowModel)->isChecked();
        if (!ids.empty())
        {
            state_->actions->get(ActionId::ShowModel)->setChecked(true);
            state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        }
        state_->viewport->setSelectedParts(ids);
        if (state_->thicknessTool)
        {
            state_->thicknessTool->selectionChanged(state_->viewport->selectedObjects());
            state_->contourTool->selectionChanged(state_->viewport->selectedObjects());
            state_->mirrorTool->selectionChanged(state_->viewport->selectedObjects());
            state_->generationTool->selectionChanged(state_->viewport->selectedObjects());
            state_->steelTool->selectionChanged(state_->viewport->selectedObjects());
            state_->crossTool->selectionChanged(state_->viewport->selectedObjects());
            state_->capTool->selectionChanged(state_->viewport->selectedObjects());
            state_->localTool->selectionChanged(state_->viewport->selectedObjects());
            state_->plateEditTool->selectionChanged(state_->viewport->selectedObjects());
        }
        // 普通树多选保留 QItemSelectionModel 的锚点，只有模式切换清掉选择时才恢复。
        if (switching && !ids.empty())
            state_->inspector->selectParts(ids);
        updateActions();
    };
    state_->inspector->onPartClicked = [this](const std::string &id) { state_->viewport->focusPart(id); };
    state_->viewport->onModelObjectsSelected = [this](const std::vector<ModelSelection> &selections)
    {
        state_->inspector->selectObjects(selections);
        if (state_->plateEditTool)
            state_->plateEditTool->selectionChanged(selections);
        if (state_->edgeTool)
            state_->edgeTool->selectionChanged(selections);
        if (state_->holeTool)
            state_->holeTool->selectionChanged(selections);
        if (state_->thicknessTool)
            state_->thicknessTool->selectionChanged(selections);
        if (state_->contourTool)
            state_->contourTool->selectionChanged(selections);
        if (state_->mirrorTool)
            state_->mirrorTool->selectionChanged(selections);
        if (state_->generationTool)
            state_->generationTool->selectionChanged(selections);
        if (state_->steelTool)
            state_->steelTool->selectionChanged(selections);
        if (state_->crossTool)
            state_->crossTool->selectionChanged(selections);
        if (state_->capTool)
            state_->capTool->selectionChanged(selections);
        if (state_->localTool)
            state_->localTool->selectionChanged(selections);
        // 选择只同步树和附属属性，自动隐藏窗口由用户通过边缘标签展开。
        updateActions();
    };
}

void MainWindow::initializeDockPanels()
{
    using enum ActionId;
    auto &actions = *state_->actions;
    auto *console = new CommandConsoleWidget(state_->service, actions, this);
    state_->console = console;
    console->onShowRequested = [this] { state_->workspace->showPanel(QStringLiteral("dock.commands")); };
    // VS 式工具窗口：右侧一组工具标签，底部命令/任务；模型树附带下方属性信息。
    state_->workspace->addPanel(QStringLiteral("dock.commands"), QStringLiteral("命令窗口"), console,
                                DockPosition::Bottom);
    auto *inspectorPanel = new QSplitter(Qt::Vertical, this);
    inspectorPanel->setObjectName(QStringLiteral("panel.modelInspector"));
    inspectorPanel->setChildrenCollapsible(false);
    inspectorPanel->addWidget(state_->inspector->tree());
    inspectorPanel->addWidget(state_->inspector->properties());
    inspectorPanel->setStretchFactor(0, 2);
    inspectorPanel->setStretchFactor(1, 1);
    inspectorPanel->setSizes({400, 200});
    state_->workspace->addPanel(QStringLiteral("dock.modelTree"), QStringLiteral("模型树"), inspectorPanel,
                                DockPosition::Right);
    state_->workspace->addPanel(QStringLiteral("dock.reconstruction"), QStringLiteral("重建参数"), state_->parameters,
                                DockPosition::Right);
    state_->workspace->addPanel(QStringLiteral("dock.operation"), QStringLiteral("操作面板"), state_->operation,
                                DockPosition::Right);
    console->onProjectChanged = [this]()
    {
        const auto project = state_->service.project();
        const bool geometryChanged =
            state_->displayedCloudId != project.cloudId || state_->displayedModelId != project.modelId;
        const bool historyRestored = project.historyRestoreRevision > state_->displayedHistoryRestoreRevision;
        if (!geometryChanged && !historyRestored)
        {
            state_->viewport->setCloudSelection(project.selection, state_->pending == State::Operation::SelectCloud);
            refreshProjectPanels();
            updateActions();
            return;
        }
        restoreCommittedParameters();
        state_->actions->get(ActionId::ShowCloud)->setChecked(static_cast<bool>(project.cloud));
        state_->actions->get(ActionId::ShowModel)->setChecked(static_cast<bool>(project.model));
        const bool inPlaceEdit =
            project.model && (!project.model->partTransforms.empty() || !project.model->thicknessOverrides.empty()) &&
            state_->displayedCloudId == project.cloudId;
        refreshDisplay(!historyRestored && !inPlaceEdit);
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
}

void MainWindow::initializeRibbonAndStatus()
{
    using enum ActionId;
    auto &actions = *state_->actions;
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
}

void MainWindow::initializePolling()
{
    using enum ActionId;
    state_->timer = new QTimer(this);
    state_->timer->setInterval(100);
    connect(state_->timer, &QTimer::timeout, this, [this] { pollTask(); });
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
        appendLog(QStringLiteral("启动任务失败：") + uiText(error.what()));
        state_->workspace->showPanel(QStringLiteral("dock.commands"));
        QTimer::singleShot(0, this, [this] { continueStartup(); });
    }
}

void MainWindow::appendLog(const QString &message)
{
    logging::write(QtInfoMsg, "workbench", message);
    if (state_->console)
        state_->console->appendLog(message);
}

void MainWindow::reportError(const QString &message, bool writeLog)
{
    state_->status->setText(QStringLiteral("操作失败"));
    if (writeLog)
        appendLog(QStringLiteral("错误：") + message);
    if (!state_->closing)
    {
        state_->workspace->showPanel(QStringLiteral("dock.commands"));
        QMessageBox::warning(this, QStringLiteral("ReverseModeler"), message);
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    state_->edgeTool->cancel();
    state_->holeTool->cancel();
    state_->thicknessTool->cancel();
    state_->contourTool->cancel();
    state_->mirrorTool->cancel();
    state_->generationTool->cancel();
    state_->steelTool->cancel();
    state_->crossTool->cancel();
    state_->capTool->cancel();
    state_->localTool->cancel();
    state_->pointTool->cancel();
    state_->transformTool->cancel();
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
