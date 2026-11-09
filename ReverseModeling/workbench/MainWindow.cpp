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
#include <QFrame>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QProgressBar>
#include <QPainter>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QStatusBar>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <SARibbonBar.h>

#include "commands/CommandService.h"
#include "commands/SectionPlane.h"
#include "workbench/DockWorkspace.h"
#include "workbench/CommandConsoleWidget.h"
#include "workbench/MainWindow.h"
#include "workbench/PlateEditTool.h"
#include "workbench/PlateEditHelp.h"
#include "commands/PlateEditCommands.h"
#include <QTreeWidget>
#include "workbench/ModelDisplayData.h"
#include "workbench/ModelInspector.h"
#include "workbench/OccViewport.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/SectionTool.h"
#include "workbench/ModelTransformTool.h"
#include "workbench/EdgeTreatmentTool.h"
#include "workbench/HoleTool.h"
#include "workbench/PlateAdjustmentTool.h"
#include "workbench/LocalReconstructionTool.h"
#include "workbench/PlateGenerationTool.h"
#include "workbench/PointCloudDisplayData.h"
#include "workbench/RecentFilesMenu.h"
#include "workbench/RibbonBuilder.h"
#include "workbench/WorkbenchActionHelp.h"
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

// 原生 OCCT 窗口旁的提示条主动铺底，避免文字更新后保留旧字形。
class ViewportHint final : public QLabel
{
  public:
    using QLabel::QLabel;
  protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.fillRect(rect(), QColor("#071722"));
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
    CommandService service{commandEnvironment()};
    std::string activeTask;
    StartupRequest startup;
    bool startupPending{};
    Operation pending{Operation::None};
    std::string cloudBeforeEdit;
    bool closing{};
    bool cancellationRequested{};
    bool parametersDirty{true};
    OccViewport *viewport{};
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
    PlateEditTool *plateEditTool{};
    PlateGenerationTool *generationTool{};
    std::string displayedCloudId, displayedModelId;
    std::uint64_t displayedHistoryRestoreRevision{};
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
    auto *hint =
        new ViewportHint(QStringLiteral("左键拾取 · Shift 点击追加/取消 · 点模式拖框选择点云 · 右键旋转 · 中键平移 · 滚轮缩放 · Esc 清空"), viewContainer);
    hint->setProperty("muted", true);
    hint->setAutoFillBackground(true);
    hint->setAttribute(Qt::WA_OpaquePaintEvent);
    hint->setStyleSheet(QStringLiteral("background-color: #071722; color: #a9c8da;"));
    hint->setWordWrap(true);
    hint->setContentsMargins(12, 5, 6, 5);
    viewLayout->addWidget(hint);
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
            state_->localTool->selectionChanged(state_->viewport->selectedObjects());
            state_->plateEditTool->selectionChanged(state_->viewport->selectedObjects());
        }
        // 普通树多选保留 QItemSelectionModel 的锚点，只有模式切换清掉选择时才恢复。
        if (switching && !ids.empty()) state_->inspector->selectParts(ids);
        updateActions();
    };
    state_->inspector->onPartClicked = [this](const std::string &id) { state_->viewport->focusPart(id); };
    state_->viewport->onModelObjectsSelected = [this](const std::vector<ModelSelection> &selections)
    {
        state_->inspector->selectObjects(selections);
        if (state_->plateEditTool) state_->plateEditTool->selectionChanged(selections);
        if (state_->edgeTool) state_->edgeTool->selectionChanged(selections);
        if (state_->holeTool) state_->holeTool->selectionChanged(selections);
        if (state_->thicknessTool) state_->thicknessTool->selectionChanged(selections);
        if (state_->contourTool) state_->contourTool->selectionChanged(selections);
        if (state_->mirrorTool) state_->mirrorTool->selectionChanged(selections);
        if (state_->generationTool) state_->generationTool->selectionChanged(selections);
        if (state_->localTool) state_->localTool->selectionChanged(selections);
        if (!selections.empty())
        {
            state_->workspace->showPanel(QStringLiteral("dock.modelTree"));
            state_->workspace->showPanel(QStringLiteral("dock.properties"));
        }
        updateActions();
    };
    state_->parameters = new ReconstructionPanel(this);
    state_->operation = new OperationPanel(actions, this);
    state_->referenceTool = new ReferencePlaneTool(state_->service, *state_->viewport, state_->operation);
    state_->referenceTool->hide();
    state_->sectionTool->onChanged = [this] { updateActions(); };
    connect(state_->sectionTool, &QMenu::aboutToShow, this, [this] { state_->viewport->setSectionPreviewVisible(true); });
    connect(state_->sectionTool, &QMenu::aboutToHide, this, [this] { state_->viewport->setSectionPreviewVisible(false); });
    state_->transformTool = new ModelTransformTool(state_->service, *state_->viewport, state_->operation);
    state_->transformTool->hide();
    state_->transformTool->onChanged = [this] { updateActions(); };
    state_->transformTool->onCommitted = [this] { refreshDisplay(false); };
    state_->edgeTool = new EdgeTreatmentTool(state_->service, *state_->viewport, state_->operation);
    state_->edgeTool->hide();
    state_->edgeTool->onChanged = [this] { updateActions(); };
    state_->edgeTool->onCommitted = [this] { refreshDisplay(false); };
    state_->holeTool = new HoleTool(state_->service, *state_->viewport, state_->operation);
    state_->holeTool->hide();
    state_->holeTool->onChanged = [this] { updateActions(); };
    state_->holeTool->onCommitted = [this] { refreshDisplay(false); };
    state_->thicknessTool = new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Thickness, state_->operation);
    state_->thicknessTool->hide();
    state_->thicknessTool->onChanged = [this] { updateActions(); };
    state_->thicknessTool->onCommitted = [this] { refreshDisplay(false); };
    state_->contourTool = new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Contour, state_->operation);
    state_->mirrorTool = new PlateAdjustmentTool(state_->service, *state_->viewport, PlateAdjustmentKind::Mirror, state_->operation);
    state_->contourTool->hide();
    state_->mirrorTool->hide();
    state_->contourTool->onChanged = [this] { updateActions(); };
    state_->mirrorTool->onChanged = [this] { updateActions(); };
    state_->contourTool->onCommitted = [this] { refreshDisplay(false); };
    state_->mirrorTool->onCommitted = [this] { refreshDisplay(false); };
    state_->plateEditTool = new PlateEditTool(state_->service,*state_->viewport,state_->operation);
    state_->plateEditTool->hide();
    auto *plateEscape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    plateEscape->setEnabled(false);
    connect(plateEscape, &QShortcut::activated, this, [this] { state_->plateEditTool->cancel(); });
    connect(cancelPlate, &QToolButton::clicked, this, [this] { state_->plateEditTool->cancel(); });
    state_->plateEditTool->onChanged = [this, plateEscape, platePrompt, hint] {
        const bool editing = state_->plateEditTool->active() && state_->plateEditTool->direct();
        plateEscape->setEnabled(editing);
        platePrompt->setVisible(editing);
        hint->setVisible(!editing);
        updateActions();
    };
    state_->plateEditTool->onCommitted=[this]{refreshDisplay(false);};
    state_->plateEditTool->onHint = [this, hint, promptText](const QString &message) {
        hint->setText(message);
        promptText->setText(message);
        if (state_->status)
            state_->status->setText(message);
    };
    state_->viewport->onPlateCrop=[this](const auto &rectangle){state_->plateEditTool->crop(rectangle);};
    state_->inspector->onContextMenu=[this](const QPoint &point){modelContextMenu(point);};
    state_->localTool = new LocalReconstructionTool(state_->service, *state_->viewport, state_->operation);
    state_->localTool->hide();
    state_->localTool->onChanged = [this] { updateActions(); };
    state_->localTool->onCommitted = [this] { refreshDisplay(false); };
    state_->generationTool = new PlateGenerationTool(state_->service, *state_->viewport, state_->operation);
    state_->generationTool->hide();
    state_->generationTool->onChanged = [this] { updateActions(); };
    state_->generationTool->onCommitted = [this] { refreshDisplay(false); };
    state_->viewport->onThicknessLabelEditRequested = [this](const std::string &id)
    {
        if (!state_->activeTask.empty() || state_->closing || !state_->service.isIdle())
        {
            return false;
        }
        const auto project = state_->service.project();
        if (!project.model || std::find(project.model->partIds.begin(), project.model->partIds.end(), id) ==
                                  project.model->partIds.end())
        {
            return false;
        }
        state_->thicknessEditRevision = project.revision;
        state_->thicknessEditPart = id;
        state_->thicknessTool->cancel();
        state_->contourTool->cancel();
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
        state_->localTool->cancel();
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
            appendLog(text(error.what()));
        }
    };
    state_->viewport->hasEditContextMenu = [this] { return state_->plateEditTool->active() || state_->localTool->active() || state_->edgeTool->active() || state_->holeTool->active() || state_->thicknessTool->active() || (state_->contourTool->active() || state_->mirrorTool->active()) || state_->generationTool->active(); };
    state_->viewport->onEditEscape = [this] {
        if (state_->plateEditTool->active()) {state_->plateEditTool->cancel();return true;}
        if (state_->generationTool->active()) { state_->generationTool->cancel(); return true; }
        if (state_->contourTool->active()) { state_->contourTool->cancel(); return true; }
        if (state_->localTool->active()) { state_->localTool->cancel(); return true; }
        if (state_->mirrorTool->active()) { state_->mirrorTool->cancel(); return true; }
        if (state_->thicknessTool->active()) { state_->thicknessTool->cancel(); return true; }
        if (state_->holeTool->active()) { state_->holeTool->cancel(); return true; }
        if (!state_->edgeTool->active()) return false;
        state_->edgeTool->cancel();return true;
    };
    state_->referenceTool->onChanged = [this] { refreshProjectPanels(); };
    auto *console = new CommandConsoleWidget(state_->service, actions, this);
    state_->console = console;
    console->onShowRequested = [this] { state_->workspace->showPanel(QStringLiteral("dock.commands")); };
    // 先建立中央“上方三维视图 + 下方命令”，再在最外层左右加入工具页和模型树/属性。
    // 左侧工具页贯穿全高，命令窗口只占中央列；中央与右侧均上下平分。
    state_->workspace->addPanel(QStringLiteral("dock.commands"), QStringLiteral("命令窗口"), console, DockPosition::Bottom);
    state_->workspace->addPanel(QStringLiteral("dock.reconstruction"), QStringLiteral("重建参数"), state_->parameters,
                                DockPosition::Left);
    state_->workspace->addPanel(QStringLiteral("dock.operation"), QStringLiteral("操作面板"), state_->operation,
                                DockPosition::Left);
    state_->workspace->addPanel(QStringLiteral("dock.modelTree"), QStringLiteral("模型树"), state_->inspector->tree(),
                                DockPosition::Right);
    state_->workspace->addPanel(QStringLiteral("dock.properties"), QStringLiteral("属性"), state_->inspector->properties(),
                                DockPosition::Right, QStringLiteral("dock.modelTree"));
    console->onProjectChanged = [this]()
    {
        const auto project = state_->service.project();
        const bool geometryChanged = state_->displayedCloudId != project.cloudId || state_->displayedModelId != project.modelId;
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
        const bool inPlaceEdit = project.model &&
            (!project.model->partTransforms.empty() || !project.model->thicknessOverrides.empty()) &&
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
    connect(actions.get(ExportModel), &QAction::triggered, this, [this] { exportCwrm(); });
    connect(actions.get(ExportModelCloud), &QAction::triggered, this, [this] { exportModelCloud(); });
    connect(actions.get(ExportAndExit), &QAction::triggered, this, [this] { exportAndExit(); });
    connect(actions.get(ExportCloud), &QAction::triggered, this, [this] { exportCloud(); });
    connect(actions.get(DeleteModel), &QAction::triggered, this, [this] {
        if (!state_->actions->get(ActionId::DeleteModel)->isEnabled()) return;
        const auto ids = state_->viewport->selectedParts();
        if (ids.empty()) return;
        try
        {
            state_->activeTask = state_->service.submitDeleteModelParts(ids, state_->service.project().revision);
            state_->pending = State::Operation::DeleteModel;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error) { appendLog(QString::fromUtf8(error.what())); }
    });
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
    state_->viewport->onCloudSelection = [this](const CloudSelectionRect &rectangle, CloudSelectionMode mode, bool editLast)
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
            reportError(text(error.what()));
        }
    };
    state_->viewport->onCloudContextMenu = [this](const QPoint &position)
    {
        if (state_->plateEditTool->active()) {
            QMenu menu(this);menu.addAction(QStringLiteral("退出板件编辑"),this,[this]{state_->plateEditTool->cancel();});menu.exec(position);return;
        }
        if (state_->actions->get(ActionId::SelectSolid)->isChecked() && !state_->actions->get(ActionId::Measure)->isChecked() &&
            !state_->referenceTool->picking() && !state_->transformTool->active() && !state_->generationTool->active() &&
            !state_->contourTool->active() && !state_->localTool->active() && !state_->mirrorTool->active() &&
            !state_->thicknessTool->active() && !state_->holeTool->active() && !state_->edgeTool->active()) {modelContextMenu(position);return;}
        QMenu menu(this);
        if (state_->generationTool->active())
            menu.addAction(QStringLiteral("退出生成板件"), this, [this] { state_->generationTool->cancel(); });
        if (state_->contourTool->active())
            menu.addAction(QStringLiteral("退出外扩板件轮廓"), this, [this] { state_->contourTool->cancel(); });
        if (state_->localTool->active())
            menu.addAction(QStringLiteral("退出局部重建"), this, [this] { state_->localTool->cancel(); });
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

    connect(actions.get(Measure), &QAction::toggled, state_->viewport, &OccViewport::setMeasurementEnabled);
    state_->viewport->onMeasurementModeChanged = [this, hint](bool enabled)
    {
        state_->actions->get(ActionId::Measure)->setChecked(enabled);
        hint->setText(enabled
            ? QStringLiteral("测量：依次左键选两点，第三次点击开始下一组 · 右键菜单 / Esc 退出 · 单位 mm")
            : QStringLiteral("左键拾取 · Shift 点击追加/取消 · 点模式拖框选择点云 · 右键旋转 · 中键平移 · 滚轮缩放 · Esc 清空"));
    };
    state_->viewport->onMeasurementCompleted = [this](const PointMeasurementResult &result)
    {
        appendLog(QStringLiteral("测量：距离 %1 mm；X %2 mm；Y %3 mm；Z %4 mm（工程坐标轴）。")
            .arg(result.distanceMm, 0, 'f', 2).arg(result.componentsMm.x, 0, 'f', 2)
            .arg(result.componentsMm.y, 0, 'f', 2).arg(result.componentsMm.z, 0, 'f', 2));
    };

    connect(actions.get(CloudHeight), &QAction::toggled, state_->viewport, &OccViewport::setCloudHeightColors);
    connect(actions.get(ShowModel), &QAction::toggled, state_->viewport, &OccViewport::setModelVisible);
    connect(actions.get(ShowModel), &QAction::toggled, this, [this] { updateActions(); });
    for (const auto id : {ModelSolid, ModelWire})
    {
        connect(actions.get(id), &QAction::triggered, this, [this, id] {
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
        connect(actions.get(id), &QAction::toggled, this, [this](bool checked)
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
    for (const auto id : {OuterPlate, BottomPlate})
    {
        connect(actions.get(id), &QAction::triggered, this, [this, id] {
            state_->thicknessTool->cancel();
            state_->contourTool->cancel();
            state_->mirrorTool->cancel();
            state_->holeTool->cancel();
            state_->edgeTool->cancel();
            state_->transformTool->cancel();
            state_->referenceTool->cancel();
            state_->generationTool->cancel();
            state_->localTool->cancel();
        state_->plateEditTool->cancel();
            state_->actions->get(ActionId::Measure)->setChecked(false);
            state_->actions->get(id == OuterPlate ? ActionId::SelectFace : ActionId::SelectSolid)->setChecked(true);
            state_->operation->showCustom(state_->actions->get(id)->text(), state_->generationTool, id);
            state_->workspace->showPanel(QStringLiteral("dock.operation"));
            state_->generationTool->begin(id == OuterPlate ? PlateGenerationKind::Cover : PlateGenerationKind::Base);
        });
    }
    connect(actions.get(LocalReconstruct), &QAction::triggered, this, [this] {
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
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
    connect(actions.get(ExtendContour), &QAction::triggered, this, [this] {
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
        state_->localTool->cancel();
        state_->plateEditTool->cancel();
        state_->thicknessTool->cancel();
        state_->holeTool->cancel();
        state_->edgeTool->cancel();
        state_->transformTool->cancel();
        state_->referenceTool->cancel();
        state_->actions->get(ActionId::Measure)->setChecked(false);
        state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        state_->operation->showCustom(QStringLiteral("外扩板件轮廓"), state_->contourTool, ActionId::ExtendContour);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
        state_->contourTool->begin();
    });
    connect(actions.get(MirrorComplete), &QAction::triggered, this, [this] {
        state_->generationTool->cancel();
        state_->localTool->cancel();
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
    connect(actions.get(MirrorCompleteAll), &QAction::triggered, this, [this] {
        if (!state_->service.isIdle() || !state_->activeTask.empty()) return;
        state_->generationTool->cancel();
        state_->localTool->cancel();
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
        if (!project.model || project.model->partIds.size() < 2) return;
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
            state_->activeTask = state_->service.submitConnectedPartsInspection(selected.front(), project.revision);
            state_->pending = State::Operation::SelectMirrorAssembly;
            state_->cancellationRequested = false;
            state_->progress->setValue(0);
            updateActions();
        }
        catch (const std::exception &error) { reportError(text(error.what())); }
    });
    connect(actions.get(PlateThickness), &QAction::triggered, this, [this] {
        state_->contourTool->cancel();
        state_->mirrorTool->cancel();
        state_->generationTool->cancel();
        state_->localTool->cancel();
        state_->plateEditTool->cancel();
        state_->holeTool->cancel();
        state_->edgeTool->cancel();
        state_->transformTool->cancel();
        state_->referenceTool->cancel();
        state_->actions->get(ActionId::Measure)->setChecked(false);
        state_->actions->get(ActionId::SelectSolid)->setChecked(true);
        state_->operation->showCustom(QStringLiteral("修改板厚"), state_->thicknessTool, ActionId::PlateThickness);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
        state_->thicknessTool->begin();
    });
    for (const auto id : {WeldHole, BottomHole})
    {
        connect(actions.get(id), &QAction::triggered, this, [this,id] {
            state_->transformTool->cancel(); state_->referenceTool->cancel(); state_->edgeTool->cancel();
            state_->actions->get(ActionId::Measure)->setChecked(false);
            state_->actions->get(ActionId::SelectSolid)->setChecked(true);
            state_->operation->showCustom(QStringLiteral("添加孔"), state_->holeTool);
            state_->workspace->showPanel(QStringLiteral("dock.operation"));
            if(id==WeldHole && state_->viewport->selectedParts().size()>1) beginPlateEdit("holes");
            else state_->holeTool->begin(id == WeldHole ? PlateHoleKind::Weld : PlateHoleKind::Bottom);
        });
    }
    connect(actions.get(Fillet), &QAction::triggered, this, [this] {
        state_->holeTool->cancel();
        state_->transformTool->cancel();state_->referenceTool->cancel();
        state_->actions->get(ActionId::Measure)->setChecked(false);
        state_->actions->get(ActionId::SelectEdge)->setChecked(true);
        state_->operation->showCustom(QStringLiteral("圆角 / 倒角"),state_->edgeTool);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
        state_->edgeTool->begin();
    });
    for (const auto id : {Move, Rotate})
    {
        connect(actions.get(id), &QAction::triggered, this, [this,id] {
            state_->referenceTool->cancel();
            state_->operation->showCustom(state_->actions->get(id)->text(),state_->transformTool);
            state_->workspace->showPanel(QStringLiteral("dock.operation"));
            state_->transformTool->begin(id == ActionId::Move ? ModelTransformKind::Move : ModelTransformKind::Rotate);
        });
    }
    connect(actions.get(BasePlane), &QAction::triggered, this, [this] {
        state_->operation->showCustom(QStringLiteral("设置基准面"), state_->referenceTool);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
        state_->referenceTool->begin();
        updateActions();
    });
    connect(actions.get(Section), &QAction::triggered, this, [this](bool enabled) {
        state_->sectionTool->setSectionEnabled(enabled);
    });
    connect(state_->parameters, &ReconstructionPanel::edited, this,
            [this]
            {
                updateActions();
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
    const bool transformActive = state_->transformTool->active() || state_->edgeTool->showingPreview() || state_->holeTool->showingPreview() || state_->thicknessTool->showingPreview() || state_->contourTool->showingPreview() || state_->mirrorTool->showingPreview() || state_->generationTool->showingPreview() || (state_->localTool->showingPreview() || state_->plateEditTool->showingPreview());
    const bool cloudEditable = cloudAvailable && !transformActive && actions.get(ActionId::SelectPoint)->isChecked();
    const bool modelAvailable = idle && project.model && actions.get(ActionId::ShowModel)->isChecked();
    actions.get(ActionId::Measure)->setEnabled(cloudAvailable || modelAvailable);
    if (!(cloudAvailable || modelAvailable))
    {
        actions.get(ActionId::Measure)->setChecked(false);
    }
    if (state_->edgeTool->active() && !actions.get(ActionId::SelectEdge)->isChecked())
        actions.get(ActionId::SelectEdge)->setChecked(true);
    if ((state_->holeTool->active() || state_->thicknessTool->active() || (state_->contourTool->active() || state_->mirrorTool->active())) && !actions.get(ActionId::SelectSolid)->isChecked())
        actions.get(ActionId::SelectSolid)->setChecked(true);
    if (state_->localTool->active() && !actions.get(ActionId::SelectSolid)->isChecked())
        actions.get(ActionId::SelectSolid)->setChecked(true);
    if (state_->generationTool->active())
        actions.get(state_->generationTool->pickMode() == ModelPickMode::Face ? ActionId::SelectFace : ActionId::SelectSolid)->setChecked(true);
    state_->viewport->setCloudEditingEnabled(cloudEditable);
    state_->viewport->setModelPickMode(actions.get(ActionId::SelectPoint)->isChecked() ? ModelPickMode::Vertex
        : actions.get(ActionId::SelectEdge)->isChecked() ? ModelPickMode::Edge
        : actions.get(ActionId::SelectFace)->isChecked() ? ModelPickMode::Face : ModelPickMode::Body);
    state_->viewport->setModelPickingEnabled(modelAvailable && !transformActive);
    state_->viewport->setThicknessEditingEnabled(modelAvailable && !transformActive && !state_->plateEditTool->active() &&
        !state_->referenceTool->picking() && !actions.get(ActionId::Measure)->isChecked() &&
        actions.get(ActionId::ShowThickness)->isChecked());
    state_->inspector->tree()->setEnabled(!state_->plateEditTool->cropping() && !transformActive && ((!state_->thicknessTool->active() && !(state_->contourTool->active() || state_->mirrorTool->active())) || idle));
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
    setActionHelpContext(actions.get(ActionId::Undo), project.canUndo ? QStringLiteral("撤销：") + text(project.undoLabel)
        : QStringLiteral("没有可撤销的工程编辑（最多保留 10 步）"));
    setActionHelpContext(actions.get(ActionId::Redo), project.canRedo ? QStringLiteral("重做：") + text(project.redoLabel)
        : QStringLiteral("没有可重做的工程编辑"));

    state_->recentFiles->menuAction()->setEnabled(idle);
    const auto viewPlane = positioningPlane(project);
    const auto viewKind = positioningView(project);
    const QString viewName = viewKind == PositioningView::Section ? QStringLiteral("剖视面")
        : viewKind == PositioningView::Bottom ? QStringLiteral("底板面") : QStringLiteral("上视图");
    actions.get(ActionId::BottomView)->setEnabled(idle && viewPlane.has_value());
    actions.get(ActionId::BottomView)->setText(viewName);
    setActionHelpContext(actions.get(ActionId::BottomView), !viewPlane
        ? QStringLiteral("当前没有对应的可用平面。")
        : viewKind == PositioningView::Top ? QStringLiteral("尚未重建模型，按工程 Z 轴俯视，保持缩放。")
        : QStringLiteral("正对当前%1，保持缩放。").arg(viewName));
    actions.get(ActionId::BasePlane)->setEnabled(cloudAvailable || modelAvailable);
    const bool sectionEditable = idle && !transformActive && !state_->referenceTool->picking();
    const bool sectionAvailable = sectionEditable && project.cloud &&
        (project.referencePlane || bottomSectionPlane(project));
    actions.get(ActionId::Section)->setEnabled(sectionAvailable || (sectionEditable && project.section.enabled));
    actions.get(ActionId::Section)->setChecked(project.section.enabled);
    state_->sectionTool->sync(project, sectionEditable);
    const bool hasCloud = project.cloud && !project.cloud->points.empty();
    const bool canReconstruct = idle && hasCloud &&
        (project.estimate.has_value() || state_->parameters->options().type == "other");
    actions.get(ActionId::Reconstruct)->setEnabled(canReconstruct);
    state_->parameters->setEditable(idle && hasCloud);
    const bool currentModel = idle && project.model && (!project.model->importedFormat.empty() ||
        (!state_->parametersDirty && !state_->parameters->hasEdits()));
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
        reason += QStringLiteral("\n目标：") +
                  QString::fromStdWString((*outputDirectory / "reserveModel.step").wstring());
    }
    setActionHelpContext(deliver, reason);
    actions.get(ActionId::ExportCloud)->setEnabled(idle && project.cloud && !project.cloud->points.empty());
    actions.get(ActionId::Deviation)->setEnabled(currentModel && project.cloud && project.model->importedFormat.empty() && project.model->edgeTreatedParts.empty() && project.model->holeEditedParts.empty() && project.model->thicknessOverrides.empty() && project.model->contourExpandedParts.empty() && project.model->mirroredParts.empty() && project.model->mirrorCompletedParts.empty() && project.model->locallyReconstructedParts.empty() &&
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
    state_->localTool->sync(project, idle);
    state_->plateEditTool->sync(project,idle);
    actions.get(ActionId::LocalReconstruct)->setEnabled(modelAvailable && !state_->transformTool->active());
    for (const auto id : {ActionId::OuterPlate, ActionId::BottomPlate, ActionId::MirrorComplete})
        actions.get(id)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::MirrorCompleteAll)->setEnabled(modelAvailable && !state_->transformTool->active() &&
        project.model->partIds.size() > 1 && !state_->viewport->selectedParts().empty());
    actions.get(ActionId::ExtendContour)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::PlateThickness)->setEnabled(modelAvailable && !state_->transformTool->active());
    for (const auto id : {ActionId::WeldHole, ActionId::BottomHole})
        actions.get(id)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::Fillet)->setEnabled(modelAvailable && !state_->transformTool->active());
    actions.get(ActionId::DeleteModel)->setEnabled(modelAvailable && actions.get(ActionId::SelectSolid)->isChecked() &&
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
            ActionId::Select, ActionId::AddSelection, ActionId::RemoveSelection, ActionId::Measure, ActionId::BasePlane,
            ActionId::DeleteModel, ActionId::CropCloud, ActionId::DeleteCloud, ActionId::ShowModel})
            actions.get(id)->setEnabled(false);
    }
    else actions.get(ActionId::ShowModel)->setEnabled(true);
    if (state_->edgeTool->active())
    {
        for (const auto id : {ActionId::SelectPoint,ActionId::SelectFace,ActionId::SelectSolid,
            ActionId::Measure,ActionId::Move,ActionId::Rotate,ActionId::BasePlane,ActionId::DeleteModel,
            ActionId::ShowModel,ActionId::ExportStep,ActionId::ExportModel,ActionId::ExportModelCloud,ActionId::ExportAndExit}) actions.get(id)->setEnabled(false);
    }
    if (state_->holeTool->active() || state_->thicknessTool->active() || (state_->contourTool->active() || state_->mirrorTool->active()))
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace,
            ActionId::Measure, ActionId::Move, ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel,
            ActionId::ShowModel, ActionId::ExportStep, ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit}) actions.get(id)->setEnabled(false);
    }
    if (state_->generationTool->active())
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
            ActionId::Measure, ActionId::Move, ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel,
            ActionId::ShowModel, ActionId::ExportStep, ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit}) actions.get(id)->setEnabled(false);
    }
    if (state_->localTool->active() || state_->plateEditTool->active())
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace,
            ActionId::Measure, ActionId::Move, ActionId::Rotate, ActionId::BasePlane, ActionId::DeleteModel,
            ActionId::ShowModel, ActionId::ExportStep, ActionId::ExportModel, ActionId::ExportModelCloud, ActionId::ExportAndExit}) actions.get(id)->setEnabled(false);
    }
    if (state_->plateEditTool->active() && state_->plateEditTool->direct())
    {
        for (const auto id : {ActionId::AddSelection, ActionId::RemoveSelection})
            actions.get(id)->setEnabled(false);
    }
    if (state_->referenceTool->picking())
    {
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid,
                              ActionId::CropCloud, ActionId::DeleteCloud}) actions.get(id)->setEnabled(false);
    }
    setActionHelpContext(actions.get(ActionId::BasePlane), !idle
        ? QStringLiteral("请等待当前任务完成。")
        : !(cloudAvailable || modelAvailable) ? QStringLiteral("请先导入并显示点云或模型。")
        : !actions.get(ActionId::BasePlane)->isEnabled()
            ? QStringLiteral("请先退出当前添加孔、板件修改或其他编辑操作，再设置基准面。")
            : QString());
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
        state_->activeTask = state_->service.submitImport(nativePath(path));
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
        state_->activeTask = state_->service.submitRecognizedReconstruct(options,state_->service.project().revision);
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

void MainWindow::exportCwrm()
{
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出 CWRM 模型（请选择新文件名）"), {},
        QStringLiteral("CWRM 模型 (*.cwrm)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".cwrm"), Qt::CaseInsensitive)) path += QStringLiteral(".cwrm");
    try
    {
        state_->activeTask = state_->service.submitExportCwrm(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::Export;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error) { reportError(text(error.what())); }
}
void MainWindow::exportModelCloud()
{
    bool accepted = false;
    const double spacing = QInputDialog::getDouble(this, QStringLiteral("模型表面采样"),
        QStringLiteral("采样间距（mm），导出的是模型表面点，不是原始扫描点云："), 2.0, 0.01, 100000.0, 2, &accepted);
    if (!accepted) return;
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("模型采样导出（请选择新文件名）"), {},
        QStringLiteral("PCD 点云 (*.pcd)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".pcd"), Qt::CaseInsensitive)) path += QStringLiteral(".pcd");
    try
    {
        state_->activeTask = state_->service.submitExportModelCloud(nativePath(path), spacing, state_->service.project().revision);
        state_->pending = State::Operation::ExportCloud;
        state_->cancellationRequested = false;
        state_->progress->setValue(0);
        updateActions();
    }
    catch (const std::exception &error) { reportError(text(error.what())); }
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
            state_->referenceTool->cancel();
            state_->viewport->setMeasurementEnabled(false);
            state_->actions->get(ActionId::Measure)->setChecked(false);
            state_->activeTask =
                state_->service.submitProjectHistory(action == ActionId::Undo   ? CloudHistoryAction::Undo
                                                   : action == ActionId::Redo ? CloudHistoryAction::Redo
                                                                              : CloudHistoryAction::Reset,
                                                   project.revision);
        }
        state_->pending = action == ActionId::CropCloud || action == ActionId::DeleteCloud
            ? State::Operation::EditCloud : State::Operation::History;
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
    state_->edgeTool->cancel();
    state_->holeTool->cancel();
    state_->thicknessTool->cancel();
    state_->contourTool->cancel();
    state_->mirrorTool->cancel();
    state_->generationTool->cancel();
    state_->localTool->cancel();
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
        state_->workspace->showPanel(QStringLiteral("dock.properties"));
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
                state_->viewport->setCloudSelection(project.selection, true);
                const auto count = project.selection ? project.selection->size() : 0;
                state_->status->setText(QStringLiteral("已选择 %1 个点；拖边/角缩放，拖框内移动；右键截取或删除，Esc 清空").arg(count));
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
                if (state_->cancellationRequested) { updateActions(); return; }
                if (project.revision != state_->mirrorSelectionRevision)
                    throw std::runtime_error("工程已变化，请重新选择工件。");
                std::vector<std::string> ids;
                for (const auto &id : std::get<rm::Array>(task.result.value)) ids.push_back(std::get<std::string>(id));
                state_->mirrorAssemblyParts = ids;
                state_->viewport->setSelectedParts(ids);
                state_->inspector->selectParts(ids);
                if (ids.size() < 2)
                    throw std::runtime_error("选中的部件没有相连板件，未执行镜像补全。");
                appendLog(QStringLiteral("全部镜像补全：已自动选中相连工件，共 %1 个部件。").arg(ids.size()));
                MirrorCompletion request;
                if (std::find(ids.begin(), ids.end(), "web") != ids.end()) request.planePartId = "web";
                else
                {
                    QStringList choices;
                    for (const auto &id : ids) choices.push_back(text(id));
                    bool accepted = false;
                    const auto plane = QInputDialog::getItem(this, QStringLiteral("全部镜像补全"),
                        QStringLiteral("已选中相连工件，请选择其中的镜像参考底板（腹板）："), choices, 0, false, &accepted);
                    if (!accepted || plane.isEmpty()) { updateActions(); return; }
                    request.planePartId = plane.toStdString();
                }
                for (const auto &id : ids) if (id != request.planePartId) request.partIds.push_back(id);
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
                                state_->mirrorAssemblyParts.end() && std::find(selected.begin(), selected.end(), id) == selected.end())
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
                    state_->recentFiles->recordOpened(text(project.sourcePathUtf8));
                }
                state_->parameters->setOptions(project.committedRecognitionOptions.value_or(
                    project.model && project.model->fromPointCloud ? project.model->recognitionOptions : PlateBuildOptions{}));
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
                state_->recentFiles->recordOpened(text(project.sourcePathUtf8));
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
        reportError(text(error.what()));
        updateActions();
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
    state_->localTool->cancel();
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
void MainWindow::beginPlateEdit(const std::string &operation,bool all)
{
    if(!state_->service.isIdle()||state_->closing) return;
    state_->plateEditTool->sync(state_->service.project(), true);
    const auto ids=state_->viewport->selectedParts();
    state_->plateEditTool->cancel();state_->localTool->cancel();state_->generationTool->cancel();
    state_->contourTool->cancel();state_->mirrorTool->cancel();state_->thicknessTool->cancel();
    state_->holeTool->cancel();state_->edgeTool->cancel();state_->transformTool->cancel();state_->referenceTool->cancel();
    state_->actions->get(ActionId::Measure)->setChecked(false);state_->actions->get(ActionId::SelectSolid)->setChecked(true);
    state_->actions->get(ActionId::Select)->setChecked(true);
    state_->viewport->setCloudSelectionMode(CloudSelectionMode::Replace);
    if (operation == "holes")
    {
        state_->operation->showCustom(text(plateEditTitle(operation)),state_->plateEditTool);
        state_->workspace->showPanel(QStringLiteral("dock.operation"));
    }
    else
        state_->operation->showOperation(ActionId::Count, QStringLiteral("操作"));
    state_->plateEditTool->begin(operation,ids,all);
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
    QMenu menu(this);const auto project=state_->service.project();const auto ids=state_->viewport->selectedParts();
    auto *title=menu.addAction(QStringLiteral("已选 %1 个部件").arg(ids.size()));title->setEnabled(false);
    const bool ready=state_->service.isIdle()&&project.model&&state_->actions->get(ActionId::SelectSolid)->isChecked();
    const auto add=[&](ActionId id) {auto *source=state_->actions->get(id);auto *action=menu.addAction(source->icon(),source->text());
        action->setEnabled(ready&&!ids.empty()&&source->isEnabled());connect(action,&QAction::triggered,source,&QAction::trigger);};
    const auto operation=[&](const char *op,bool all=false) {
        const bool batch = std::string_view(op) == "holes";
        auto *a = menu.addAction(all ? QStringLiteral("修复全部板件孔洞") : text(plateEditTitle(op)) +
            (batch ? QStringLiteral("…") : QString()));
        a->setEnabled(ready && (all || (batch ? !ids.empty() : ids.size() == 1)));
        if (!batch)
            addPlateEditHelp(menu, *a, op, all);
        connect(a,&QAction::triggered,this,[this,op,all]{beginPlateEdit(op,all);});
    };
    add(ActionId::DeleteModel);menu.addSeparator();
    for(const auto *op:{"extend","trim","merge","split","crop"}) operation(op);
    menu.addSeparator();operation("repair");operation("repair",true);operation("holes");
    menu.addSeparator();add(ActionId::MirrorComplete);add(ActionId::Move);add(ActionId::LocalReconstruct);
    menu.exec(position);
}

} // namespace reverse::workbench
