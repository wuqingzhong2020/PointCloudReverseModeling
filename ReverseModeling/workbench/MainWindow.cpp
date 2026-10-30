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
#include <QDateTime>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <SARibbonBar.h>

#include "commands/CommandService.h"
#include "workbench/DockWorkspace.h"
#include "workbench/MainWindow.h"
#include "workbench/ModelDisplayData.h"
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
        Import,
        Reconstruct,
        Export,
        Deviation
    };
    CommandService service;
    std::string activeTask;
    Operation pending{Operation::None};
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
    QPlainTextEdit *log{};
    QTreeWidget *tree{};
    QTableWidget *properties{};
    QLabel *taskDetails{};
    QLabel *status{};
    QProgressBar *progress{};
    QTimer *timer{};
};

MainWindow::MainWindow(QWidget *parent)
    : SARibbonMainWindow(parent,
                         SARibbonMainWindowStyleFlag::UseRibbonMenuBar | SARibbonMainWindowStyleFlag::UseNativeFrame),
      state_(std::make_unique<State>())
{
    using enum ActionId;
    setObjectName(QStringLiteral("ReverseModeler.MainWindow"));
    setStyleSheet(qApp->styleSheet());
    setWindowTitle(QStringLiteral("ReverseModeler · 点云辅助逆向建模"));
    setWindowIcon(WorkbenchTheme::icon(QStringLiteral("cube")));
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
    auto *hint = label(QStringLiteral("右键旋转 · 中键平移 · 滚轮缩放 · 双击适合视图"), viewContainer);
    hint->setProperty("muted", true);
    hint->setContentsMargins(12, 5, 6, 5);
    viewLayout->addWidget(hint);
    state_->tree = new QTreeWidget(this);
    state_->tree->setObjectName(QStringLiteral("panel.modelTree"));
    state_->tree->setHeaderLabels({QStringLiteral("工程对象"), QStringLiteral("状态")});
    state_->tree->setMinimumWidth(250);
    state_->workspace->addPanel(QStringLiteral("dock.modelTree"), QStringLiteral("模型树"), state_->tree,
                                DockPosition::Left);
    state_->properties = new QTableWidget(0, 2, this);
    state_->properties->setObjectName(QStringLiteral("panel.properties"));
    state_->properties->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    state_->properties->horizontalHeader()->setStretchLastSection(true);
    state_->properties->verticalHeader()->hide();
    state_->properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    state_->properties->setMinimumWidth(250);
    state_->workspace->addPanel(QStringLiteral("dock.properties"), QStringLiteral("属性"), state_->properties,
                                DockPosition::Right);
    state_->parameters = new ReconstructionPanel(this);
    state_->workspace->addPanel(QStringLiteral("dock.reconstruction"), QStringLiteral("重建参数"), state_->parameters,
                                DockPosition::Left);
    state_->operation = new OperationPanel(this);
    state_->workspace->addPanel(QStringLiteral("dock.operation"), QStringLiteral("操作面板"), state_->operation,
                                DockPosition::Left);
    auto *logPanel = new QWidget(this);
    auto *logLayout = new QVBoxLayout(logPanel);
    logLayout->setContentsMargins(0, 0, 0, 0);
    auto *logTools = new QToolBar(logPanel);
    logTools->setMinimumHeight(36);
    auto *copy = logTools->addAction(QStringLiteral("复制所选"));
    auto *clear = logTools->addAction(QStringLiteral("清空日志"));
    state_->log = new QPlainTextEdit(logPanel);
    state_->log->setObjectName(QStringLiteral("panel.log"));
    state_->log->setReadOnly(true);
    state_->log->setMaximumBlockCount(2000);
    copy->setEnabled(false);
    connect(copy, &QAction::triggered, state_->log, &QPlainTextEdit::copy);
    connect(clear, &QAction::triggered, state_->log, &QPlainTextEdit::clear);
    connect(state_->log, &QPlainTextEdit::copyAvailable, copy, &QAction::setEnabled);
    logLayout->addWidget(logTools);
    logLayout->addWidget(state_->log, 1);
    state_->workspace->addPanel(QStringLiteral("dock.log"), QStringLiteral("日志窗口"), logPanel, DockPosition::Bottom);
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
    connect(actions.get(CloudHeight), &QAction::toggled, state_->viewport, &OccViewport::setCloudHeightColors);
    connect(actions.get(ShowModel), &QAction::toggled, state_->viewport, &OccViewport::setModelVisible);
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
    appendLog(QStringLiteral("工作台就绪。点云直接按 mm 导入；窗口菜单可显示日志和参数面板。"));
    refreshProjectPanels();
    updateActions();
    QTimer::singleShot(0, this,
                       [this]
                       {
                           state_->workspace->restoreLayout();
                       });
}

MainWindow::~MainWindow() = default;

void MainWindow::appendLog(const QString &message)
{
    state_->log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + message);
}

void MainWindow::reportError(const QString &message)
{
    state_->status->setText(QStringLiteral("操作失败"));
    appendLog(QStringLiteral("错误：") + message);
    if (!state_->closing)
    {
        state_->workspace->showPanel(QStringLiteral("dock.log"));
        QMessageBox::warning(this, QStringLiteral("ReverseModeler"), message);
    }
}

void MainWindow::updateActions()
{
    const bool idle = state_->activeTask.empty() && !state_->closing;
    const auto project = state_->service.project();
    auto &actions = *state_->actions;
    actions.get(ActionId::Open)->setEnabled(idle);
    state_->recentFiles->menuAction()->setEnabled(idle);
    actions.get(ActionId::BottomView)->setEnabled(idle && (project.model || project.recognition.has_value()));
    actions.get(ActionId::Reconstruct)->setEnabled(idle && project.cloud &&
        (state_->parameters->usePointCloud() ? project.recognition.has_value() : project.estimate.has_value()));
    state_->parameters->setEditable(idle && project.cloud && project.estimate.has_value(),
                                    idle && project.recognition.has_value());
    const bool currentModel = idle && project.model && !state_->parametersDirty && !state_->parameters->hasEdits();
    actions.get(ActionId::ExportStep)->setEnabled(currentModel);
    actions.get(ActionId::Deviation)->setEnabled(currentModel);
    const bool cancellable = !state_->activeTask.empty() && !state_->closing && !state_->cancellationRequested;
    actions.get(ActionId::CancelTask)->setEnabled(cancellable);
    actions.get(ActionId::StopReconstruct)->setEnabled(cancellable && state_->pending == State::Operation::Reconstruct);
}

void MainWindow::importCloud()
{
    const auto path =
        QFileDialog::getOpenFileName(this, QStringLiteral("打开已有点云文件"), {}, QStringLiteral("PCD 点云 (*.pcd)"));
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
        appendLog(QStringLiteral("开始导入（单位 mm）：") + path);
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
            appendLog(state_->parameters->mirrorSymmetry()
                ? QStringLiteral("按点云逐板识别结果建模，并按显式选项镜像补建另一侧。")
                : QStringLiteral("按点云逐板识别结果建模；不补建不可见的镜像侧。"));
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
        appendLog(QStringLiteral("开始手动参数建模；本模式参数不是点云识别结果。"));
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
        appendLog(QStringLiteral("导出并独立回读验证：") + path);
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
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
        appendLog(QStringLiteral("开始计算当前有限板件表面偏差。"));
        updateActions();
    }
    catch (const std::exception &error)
    {
        reportError(text(error.what()));
    }
}

void MainWindow::refreshDisplay()
{
    const auto project = state_->service.project();
    std::vector<CloudDisplayPoint> points;
    Vec3 origin;
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
    state_->viewport->setPointCloud(std::move(points));
    if (project.mesh && project.model)
    {
        state_->viewport->setModel(makeModelDisplayParts(*project.mesh, *project.model, origin));
    }
    else
    {
        state_->viewport->setModel({});
    }
    state_->viewport->fitAll();
    refreshProjectPanels();
}

void MainWindow::refreshProjectPanels()
{
    const auto project = state_->service.project();
    state_->tree->clear();
    auto *root = new QTreeWidgetItem(state_->tree, {QStringLiteral("当前工程"), QStringLiteral("内存")});
    new QTreeWidgetItem(root,
                        {QStringLiteral("点云"), project.cloud ? QStringLiteral("已导入") : QStringLiteral("无")});
    new QTreeWidgetItem(
        root, {QStringLiteral("板件模型"), project.model ? QStringLiteral("已生成") : QStringLiteral("无")});
    state_->tree->expandAll();
    state_->tree->resizeColumnToContents(0);
    const QList<QPair<QString, QString>> properties = {
        {QStringLiteral("工程版本"), QString::number(project.revision)},
        {QStringLiteral("来源"), project.cloud ? text(project.sourcePathUtf8) : QStringLiteral("尚未导入")},
        {QStringLiteral("有效点数"),
         project.cloud ? QString::number(project.cloud->points.size()) : QStringLiteral("0")},
        {QStringLiteral("实体数"), project.model ? QString::number(project.model->solidCount) : QStringLiteral("0")},
        {QStringLiteral("未定厚观测面"), project.model ? QString::number(project.model->surfaceCount) : QStringLiteral("0")},
        {QStringLiteral("对象属性"), QStringLiteral("对象选择与编辑待接入")}};
    state_->properties->setRowCount(static_cast<int>(properties.size()));
    for (qsizetype i = 0; i < properties.size(); ++i)
    {
        state_->properties->setItem(static_cast<int>(i), 0, new QTableWidgetItem(properties[i].first));
        state_->properties->setItem(static_cast<int>(i), 1, new QTableWidgetItem(properties[i].second));
    }
}

void MainWindow::pollTask()
{
    if (state_->activeTask.empty())
    {
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
        const auto operation = state_->pending;
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        if (task.state == TaskState::Succeeded)
        {
            const auto project = state_->service.project();
            appendLog(QStringLiteral("完成：") + text(task.message));
            if (operation == State::Operation::Import && project.cloud)
            {
                state_->recentFiles->recordOpened(text(project.sourcePathUtf8));
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
                    QStringLiteral("%1\n原始点 %2；无效点 %3\n1 单位 = %4 mm\n显示原点 %5 mm")
                        .arg(text(project.sourcePathUtf8))
                        .arg(static_cast<qulonglong>(cloud.sourcePointCount))
                        .arg(static_cast<qulonglong>(cloud.invalidPointCount))
                        .arg(cloud.sourceScaleToMm)
                        .arg(vectorText(cloud.originMm)));
                if (project.estimate)
                {
                    const auto &estimate = *project.estimate;
                    state_->parameters->setFrameText(
                        QStringLiteral("PCA 原始框架（交换前），须核对：\n中心 %1 mm\n梁长轴 %2\n宽度轴 %3\n高度轴 %4")
                            .arg(vectorText(estimate.frame.originMm), vectorText(estimate.frame.xAxis),
                                 vectorText(estimate.frame.yAxis), vectorText(estimate.frame.zAxis)));
                    state_->parameters->setModelText(QStringLiteral("点云已导入；请核对逐板识别结果，默认仅构建观测侧。"));
                    for (const auto &warning : estimate.warnings)
                    {
                        appendLog(QStringLiteral("初值提示：") + text(warning));
                    }
                }
                else
                {
                    state_->parameters->setFrameText(QStringLiteral("当前点云不能提供可靠的三维 PCA 梁框架。"));
                    state_->parameters->setModelText(QStringLiteral("点云可查看，当前辅助建模需要可用的坐标框架。"));
                }
                for (const auto &warning : project.warnings)
                {
                    appendLog(QStringLiteral("导入提示：") + text(warning));
                }
                refreshDisplay();
                if (!state_->closing)
                {
                    state_->workspace->showPanel(QStringLiteral("dock.reconstruction"));
                }
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
                for (const auto &warning : project.model->warnings)
                {
                    appendLog(text(warning));
                }
                refreshDisplay();
            }
            else if (operation == State::Operation::Export && project.lastExport)
            {
                appendLog(QStringLiteral("STEP 回读：%1 个实体，%2 字节；文件：%3；另有 %4 个观测面")
                              .arg(static_cast<qulonglong>(project.lastExport->solidCount))
                              .arg(static_cast<qulonglong>(project.lastExport->fileBytes))
                              .arg(text(project.exportedPathUtf8))
                              .arg(project.lastExport->surfaceCount));
            }
            else if (operation == State::Operation::Deviation && project.deviation)
            {
                const auto &result = *project.deviation;
                appendLog(QStringLiteral("偏差（mm）：均值 %1，RMS %2，P95 %3，最大 %4；%5 点。仅当前矩形板件表面。")
                              .arg(result.meanMm, 0, 'g', 8)
                              .arg(result.rmsMm, 0, 'g', 8)
                              .arg(result.p95Mm, 0, 'g', 8)
                              .arg(result.maximumMm, 0, 'g', 8)
                              .arg(static_cast<qulonglong>(result.pointCount)));
                if (!state_->closing)
                {
                    state_->workspace->showPanel(QStringLiteral("dock.log"));
                }
            }
        }
        else if (task.state == TaskState::Cancelled)
        {
            appendLog(QStringLiteral("任务已取消，保留原工程状态。"));
        }
        else
        {
            reportError(text(task.errorCode) + QStringLiteral(": ") + text(task.message));
        }
        updateActions();
    }
    catch (const std::exception &error)
    {
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        reportError(text(error.what()));
        updateActions();
    }
    if (state_->closing && state_->activeTask.empty())
    {
        QTimer::singleShot(0, this,
                           [this]
                           {
                               close();
                           });
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!state_->activeTask.empty())
    {
        state_->closing = true;
        (void)state_->service.cancel(state_->activeTask);
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
