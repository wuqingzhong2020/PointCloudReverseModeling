/*
 * 文件描述：
 * 实现点云导入、参考参数填充、一键辅助建模、STEP 导出及偏差结果界面。
 * 通过可复用命令服务轮询任务状态，所有窗口和 AIS 更新仅在 GUI 线程执行。
 */
#include "reverse/workbench/MainWindow.h"
#include "reverse/workbench/OccViewport.h"
#include "reverse/CommandService.h"

#include <QAction>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <stdexcept>
#include <string_view>

namespace reverse::workbench {
namespace {

QString text(std::string_view value) {
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

std::filesystem::path nativePath(const QString& value) {
#if defined(_WIN32)
    return std::filesystem::path(value.toStdWString());
#else
    const auto utf8 = value.toUtf8();
    return std::filesystem::path(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
#endif
}

QString vectorText(const Vec3& value) {
    return QStringLiteral("(%1, %2, %3)").arg(value.x, 0, 'g', 8).arg(value.y, 0, 'g', 8).arg(value.z, 0, 'g', 8);
}

QDoubleSpinBox* dimension(QWidget* parent) {
    auto* spin = new QDoubleSpinBox(parent);
    spin->setRange(0.0, 1000000000.0);
    spin->setDecimals(4);
    spin->setSuffix(QStringLiteral(" mm"));
    spin->setKeyboardTracking(false);
    return spin;
}

} // namespace

struct MainWindow::State {
    enum class Operation { None, Import, Reconstruct, Export, Deviation };
    CommandService service;
    std::string activeTask;
    Operation pending{Operation::None};
    bool closing{};
    bool parametersDirty{true};
    OccViewport* viewport{};
    QPushButton* importButton{};
    QPushButton* reconstructButton{};
    QPushButton* restoreReferenceButton{};
    QPushButton* exportButton{};
    QPushButton* deviationButton{};
    QPushButton* cancelButton{};
    QComboBox* unit{};
    QDoubleSpinBox* length{};
    QDoubleSpinBox* width{};
    QDoubleSpinBox* height{};
    QDoubleSpinBox* webThickness{};
    QDoubleSpinBox* flangeThickness{};
    QDoubleSpinBox* ribThickness{};
    QLineEdit* ribStations{};
    QCheckBox* swapCrossSection{};
    QGroupBox* parameters{};
    QLabel* source{};
    QLabel* frame{};
    QLabel* referenceHint{};
    QLabel* modelState{};
    QLabel* status{};
    QProgressBar* progress{};
    QPlainTextEdit* log{};
    QTimer* timer{};
};

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), state_(std::make_unique<State>()) {
    setWindowTitle(QStringLiteral("ReverseModeling · 点云辅助逆向建模"));
    resize(1380, 900);

    auto* tools = addToolBar(QStringLiteral("工程操作"));
    tools->setMovable(false);
    state_->unit = new QComboBox(tools);
    state_->unit->addItem(QStringLiteral("请选择文件单位"), -1);
    state_->unit->addItem(QStringLiteral("毫米 mm"), static_cast<int>(LengthUnit::Millimeter));
    state_->unit->addItem(QStringLiteral("厘米 cm"), static_cast<int>(LengthUnit::Centimeter));
    state_->unit->addItem(QStringLiteral("米 m"), static_cast<int>(LengthUnit::Meter));
    tools->addWidget(new QLabel(QStringLiteral(" 输入单位："), tools));
    tools->addWidget(state_->unit);
    state_->importButton = new QPushButton(QStringLiteral("打开 PCD"), tools);
    state_->exportButton = new QPushButton(QStringLiteral("导出 STEP"), tools);
    state_->deviationButton = new QPushButton(QStringLiteral("计算偏差"), tools);
    state_->cancelButton = new QPushButton(QStringLiteral("取消任务"), tools);
    for (auto* button : {state_->importButton, state_->exportButton, state_->deviationButton, state_->cancelButton})
        tools->addWidget(button);
    tools->addSeparator();
    auto* fit = tools->addAction(QStringLiteral("适合视图"));
    auto* showCloud = new QCheckBox(QStringLiteral("点云"), tools);
    auto* showModel = new QCheckBox(QStringLiteral("模型"), tools);
    showCloud->setChecked(true);
    showModel->setChecked(true);
    tools->addWidget(showCloud);
    tools->addWidget(showModel);

    auto* splitter = new QSplitter(this);
    auto* scroll = new QScrollArea(splitter);
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(330);
    auto* panel = new QWidget(scroll);
    auto* panelLayout = new QVBoxLayout(panel);
    auto* introduction = new QLabel(QStringLiteral(
        "已有点云文件 → 参考参数 → 一键构建 → STEP\n"
        "导入后自动填入可编辑的参考值，可直接生成初始模型；板厚与肋板位置不是点云测量结果。"), panel);
    introduction->setWordWrap(true);
    panelLayout->addWidget(introduction);
    state_->source = new QLabel(QStringLiteral("尚未导入点云。"), panel);
    state_->source->setTextFormat(Qt::PlainText);
    state_->source->setWordWrap(true);
    state_->source->setTextInteractionFlags(Qt::TextSelectableByMouse);
    panelLayout->addWidget(state_->source);
    state_->parameters = new QGroupBox(QStringLiteral("建模参数（内部单位 mm）"), panel);
    auto* form = new QFormLayout(state_->parameters);
    state_->length = dimension(state_->parameters);
    state_->width = dimension(state_->parameters);
    state_->height = dimension(state_->parameters);
    state_->webThickness = dimension(state_->parameters);
    state_->flangeThickness = dimension(state_->parameters);
    state_->ribThickness = dimension(state_->parameters);
    form->addRow(QStringLiteral("梁长"), state_->length);
    form->addRow(QStringLiteral("翼板宽度"), state_->width);
    form->addRow(QStringLiteral("截面高度"), state_->height);
    form->addRow(QStringLiteral("腹板厚度"), state_->webThickness);
    form->addRow(QStringLiteral("翼板厚度"), state_->flangeThickness);
    form->addRow(QStringLiteral("肋板厚度"), state_->ribThickness);
    state_->ribStations = new QLineEdit(state_->parameters);
    state_->ribStations->setPlaceholderText(QStringLiteral("可选，例如 300, 800"));
    state_->ribStations->setToolTip(QStringLiteral("距梁起点的距离，单位 mm；每站在腹板两侧各生成一块肋板。留空表示无肋板。"));
    form->addRow(QStringLiteral("肋板位置"), state_->ribStations);
    state_->swapCrossSection = new QCheckBox(QStringLiteral("交换截面宽度/高度方向"), state_->parameters);
    form->addRow(state_->swapCrossSection);
    state_->frame = new QLabel(QStringLiteral("导入后显示 PCA 估计坐标框架。"), state_->parameters);
    state_->frame->setWordWrap(true);
    state_->frame->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(state_->frame);
    state_->referenceHint = new QLabel(QStringLiteral("导入后填入参考初始值，生成模型前可直接修改。"), state_->parameters);
    state_->referenceHint->setWordWrap(true);
    form->addRow(state_->referenceHint);
    state_->restoreReferenceButton = new QPushButton(QStringLiteral("恢复参考值"), state_->parameters);
    state_->restoreReferenceButton->setToolTip(QStringLiteral("将当前编辑值恢复为本次点云的参考参数；一键构建不会自动覆盖您的编辑。"));
    form->addRow(state_->restoreReferenceButton);
    state_->reconstructButton = new QPushButton(QStringLiteral("一键构建"), state_->parameters);
    state_->reconstructButton->setToolTip(QStringLiteral("直接使用当前参数生成初始 H 型钢/肋板模型，建模结果仍需核对。"));
    form->addRow(state_->reconstructButton);
    panelLayout->addWidget(state_->parameters);
    state_->modelState = new QLabel(QStringLiteral("尚无模型。"), panel);
    state_->modelState->setWordWrap(true);
    panelLayout->addWidget(state_->modelState);
    panelLayout->addStretch();
    scroll->setWidget(panel);

    auto* workspace = new QWidget(splitter);
    auto* workspaceLayout = new QVBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    state_->viewport = new OccViewport(workspace);
    state_->viewport->onError = [this](const QString& error) { appendLog(QStringLiteral("视图错误：") + error); };
    workspaceLayout->addWidget(state_->viewport, 1);
    auto* controlsHint = new QLabel(QStringLiteral("右键旋转 · 中键平移 · 滚轮缩放 · 双击适合视图　|　显示采样不参与建模精度判断"), workspace);
    workspaceLayout->addWidget(controlsHint);
    state_->log = new QPlainTextEdit(workspace);
    state_->log->setReadOnly(true);
    state_->log->setMaximumBlockCount(2000);
    state_->log->setMaximumHeight(170);
    workspaceLayout->addWidget(state_->log);
    splitter->setStretchFactor(0, 0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({350, 1030});
    setCentralWidget(splitter);

    state_->progress = new QProgressBar(this);
    state_->progress->setRange(0, 1000);
    state_->progress->setFixedWidth(220);
    state_->progress->setFormat(QStringLiteral("%p%"));
    state_->status = new QLabel(QStringLiteral("就绪"), this);
    statusBar()->addWidget(state_->status, 1);
    statusBar()->addPermanentWidget(state_->progress);

    connect(state_->importButton, &QPushButton::clicked, this, [this] { importCloud(); });
    connect(state_->reconstructButton, &QPushButton::clicked, this, [this] { reconstruct(); });
    connect(state_->restoreReferenceButton, &QPushButton::clicked, this, [this] { restoreReferenceParameters(); });
    connect(state_->exportButton, &QPushButton::clicked, this, [this] { exportStep(); });
    connect(state_->deviationButton, &QPushButton::clicked, this, [this] { evaluateDeviation(); });
    connect(state_->cancelButton, &QPushButton::clicked, this, [this] {
        if (!state_->activeTask.empty() && state_->service.cancel(state_->activeTask)) {
            state_->cancelButton->setEnabled(false);
            appendLog(QStringLiteral("已请求取消；等待当前处理阶段安全结束。"));
        }
    });
    connect(fit, &QAction::triggered, this, [this] { state_->viewport->fitAll(); });
    connect(showCloud, &QCheckBox::toggled, state_->viewport, &OccViewport::setCloudVisible);
    connect(showModel, &QCheckBox::toggled, state_->viewport, &OccViewport::setModelVisible);
    connect(state_->unit, &QComboBox::currentIndexChanged, this, [this] { updateActions(); });

    const auto markEdited = [this] {
        state_->parametersDirty = true;
        state_->referenceHint->setText(QStringLiteral("当前参数已编辑；一键构建将使用这些值，不会恢复默认值。"));
        if (state_->service.project().model)
            state_->modelState->setText(QStringLiteral("参数已修改，视图仍显示上一次模型。请点击一键构建更新模型后再导出。"));
        updateActions();
    };
    for (auto* spin : {state_->length, state_->width, state_->height, state_->webThickness,
                      state_->flangeThickness, state_->ribThickness})
        connect(spin, &QDoubleSpinBox::valueChanged, this, markEdited);
    connect(state_->ribStations, &QLineEdit::textChanged, this, markEdited);
    connect(state_->swapCrossSection, &QCheckBox::toggled, this, [this, markEdited] {
        const auto oldWidth = state_->width->value();
        state_->width->setValue(state_->height->value());
        state_->height->setValue(oldWidth);
        markEdited();
    });

    state_->timer = new QTimer(this);
    state_->timer->setInterval(100);
    connect(state_->timer, &QTimer::timeout, this, [this] { pollTask(); });
    state_->timer->start();
    appendLog(QStringLiteral("请先明确选择 PCD 文件的长度单位。软件仅处理已有文件，不提供扫描功能。"));
    updateActions();
}

MainWindow::~MainWindow() = default;

void MainWindow::appendLog(const QString& message) {
    state_->log->appendPlainText(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) + message);
}

void MainWindow::reportError(const QString& message) {
    state_->status->setText(QStringLiteral("操作失败"));
    appendLog(QStringLiteral("错误：") + message);
    if (!state_->closing) QMessageBox::warning(this, QStringLiteral("ReverseModeling"), message);
}

void MainWindow::updateActions() {
    const bool busy = !state_->activeTask.empty();
    const auto project = state_->service.project();
    state_->unit->setEnabled(!busy && !state_->closing);
    state_->importButton->setEnabled(!busy && !state_->closing && state_->unit->currentData().toInt() >= 0);
    state_->parameters->setEnabled(!busy && !state_->closing && project.cloud && project.estimate.has_value());
    state_->reconstructButton->setEnabled(!busy && !state_->closing && project.cloud && project.estimate.has_value());
    state_->restoreReferenceButton->setEnabled(!busy && !state_->closing && project.suggestedParameters.has_value());
    state_->exportButton->setEnabled(!busy && !state_->closing && project.model && !state_->parametersDirty);
    state_->deviationButton->setEnabled(!busy && !state_->closing && project.model && !state_->parametersDirty);
    state_->cancelButton->setEnabled(busy && !state_->closing);
}

void MainWindow::importCloud() {
    if (state_->unit->currentData().toInt() < 0) return;
    const auto path = QFileDialog::getOpenFileName(this, QStringLiteral("导入已有点云文件"), {}, QStringLiteral("PCD 点云 (*.pcd)"));
    if (path.isEmpty()) return;
    try {
        state_->activeTask = state_->service.submitImport(nativePath(path), static_cast<LengthUnit>(state_->unit->currentData().toInt()));
        state_->pending = State::Operation::Import;
        state_->progress->setValue(0);
        appendLog(QStringLiteral("开始导入：") + path);
        updateActions();
    } catch (const std::exception& error) { reportError(text(error.what())); }
}

void MainWindow::restoreReferenceParameters() {
    const auto project = state_->service.project();
    if (!project.suggestedParameters) return;
    const auto& suggestion = *project.suggestedParameters;
    const auto& parameters = suggestion.parameters;
    // 由服务生成完整参考参数；设置交换标记时不再触发 UI 的二次宽高交换。
    const QSignalBlocker blockLength(state_->length), blockWidth(state_->width), blockHeight(state_->height),
        blockWeb(state_->webThickness), blockFlange(state_->flangeThickness), blockRib(state_->ribThickness),
        blockStations(state_->ribStations), blockSwap(state_->swapCrossSection);
    state_->length->setValue(parameters.length);
    state_->width->setValue(parameters.width);
    state_->height->setValue(parameters.height);
    state_->webThickness->setValue(parameters.webThickness);
    state_->flangeThickness->setValue(parameters.flangeThickness);
    state_->ribThickness->setValue(parameters.ribThickness);
    state_->swapCrossSection->setChecked(parameters.swapCrossSection);
    QStringList stations;
    for (const auto station : parameters.ribStations) stations.push_back(QString::number(station, 'f', 4));
    state_->ribStations->setText(stations.join(QStringLiteral(", ")));
    state_->parametersDirty = true;
    state_->referenceHint->setText(QStringLiteral("已填入参考初始值，可直接一键构建或先修改；参考值不代表测量结果。"));
    if (project.model)
        state_->modelState->setText(QStringLiteral("已恢复参考参数，视图仍显示上一次模型。请点击一键构建更新模型后再导出。"));
    appendLog(QStringLiteral("已应用参考参数：腹板 %1 mm，翼板 %2 mm，肋板 %3 mm；肋板位置 %4 mm。")
        .arg(parameters.webThickness).arg(parameters.flangeThickness).arg(parameters.ribThickness).arg(stations.join(QStringLiteral(", "))));
    for (const auto& note : suggestion.notes) appendLog(QStringLiteral("参考说明：") + text(note));
    updateActions();
}

void MainWindow::reconstruct() {
    try {
        BeamParameters parameters;
        parameters.length = state_->length->value();
        parameters.width = state_->width->value();
        parameters.height = state_->height->value();
        parameters.webThickness = state_->webThickness->value();
        parameters.flangeThickness = state_->flangeThickness->value();
        parameters.ribThickness = state_->ribThickness->value();
        parameters.swapCrossSection = state_->swapCrossSection->isChecked();
        for (const auto& value : state_->ribStations->text().split(QRegularExpression(QStringLiteral("[,，;；\\s]+")), Qt::SkipEmptyParts)) {
            bool ok = false;
            const auto station = value.toDouble(&ok);
            if (!ok || !std::isfinite(station)) throw std::invalid_argument("Rib stations must be finite millimeter values separated by commas or spaces.");
            parameters.ribStations.push_back(station);
        }
        state_->activeTask = state_->service.submitReconstruct(parameters, state_->service.project().revision);
        state_->pending = State::Operation::Reconstruct;
        state_->progress->setValue(0);
        appendLog(QStringLiteral("一键构建：使用当前面板参数生成初始模型；参考板厚与肋板位置不代表自动测量结果。"));
        updateActions();
    } catch (const std::exception& error) { reportError(text(error.what())); }
}

void MainWindow::exportStep() {
    auto path = QFileDialog::getSaveFileName(this, QStringLiteral("导出并回读验证 STEP（请选择新文件名）"), {}, QStringLiteral("STEP 模型 (*.step *.stp)"));
    if (path.isEmpty()) return;
    if (!path.endsWith(QStringLiteral(".step"), Qt::CaseInsensitive) && !path.endsWith(QStringLiteral(".stp"), Qt::CaseInsensitive))
        path += QStringLiteral(".step");
    try {
        state_->activeTask = state_->service.submitExport(nativePath(path), state_->service.project().revision);
        state_->pending = State::Operation::Export;
        state_->progress->setValue(0);
        appendLog(QStringLiteral("导出并独立回读验证：") + path);
        updateActions();
    } catch (const std::exception& error) { reportError(text(error.what())); }
}

void MainWindow::evaluateDeviation() {
    try {
        state_->activeTask = state_->service.submitDeviation(state_->service.project().revision);
        state_->pending = State::Operation::Deviation;
        state_->progress->setValue(0);
        appendLog(QStringLiteral("计算原始有效点到当前有限矩形板件表面的偏差。"));
        updateActions();
    } catch (const std::exception& error) { reportError(text(error.what())); }
}

void MainWindow::refreshDisplay() {
    const auto project = state_->service.project();
    std::vector<OccViewport::Vertex> points;
    Vec3 origin;
    if (project.cloud) {
        origin = project.cloud->originMm;
        constexpr std::size_t maxDisplayPoints = 200000;
        const auto& source = project.cloud->points;
        const auto stride = std::max<std::size_t>(1, (source.size() + maxDisplayPoints - 1) / maxDisplayPoints);
        points.reserve(std::min(source.size(), maxDisplayPoints));
        for (std::size_t i = 0; i < source.size(); i += stride) {
            const auto& p = source[i].position;
            points.push_back({p.x, p.y, p.z});
        }
        appendLog(QStringLiteral("点云显示 %1 / %2 个有效点；完整点云保留在服务中。").arg(static_cast<qulonglong>(points.size())).arg(static_cast<qulonglong>(source.size())));
    }
    state_->viewport->setPointCloud(points);
    std::vector<OccViewport::Vertex> vertices;
    if (project.mesh) {
        vertices.reserve(project.mesh->vertices.size());
        for (const auto& p : project.mesh->vertices) vertices.push_back({p.x - origin.x, p.y - origin.y, p.z - origin.z});
        state_->viewport->setMesh(vertices, project.mesh->triangles);
    } else {
        state_->viewport->setMesh({}, {});
    }
    state_->viewport->fitAll();
}

void MainWindow::pollTask() {
    if (state_->activeTask.empty()) return;
    try {
        const auto task = state_->service.task(state_->activeTask);
        state_->progress->setValue(qRound(std::clamp(task.progress, 0.0, 1.0) * 1000.0));
        state_->status->setText(text(task.message));
        if (!task.finished()) return;
        const auto operation = state_->pending;
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        if (task.state == TaskState::Succeeded) {
            const auto project = state_->service.project();
            appendLog(QStringLiteral("完成：") + text(task.message));
            if (operation == State::Operation::Import && project.cloud) {
                const auto& cloud = *project.cloud;
                const QSignalBlocker blockSwap(state_->swapCrossSection);
                state_->swapCrossSection->setChecked(false);
                state_->length->setValue(project.estimate ? project.estimate->length : 0.0);
                state_->width->setValue(project.estimate ? project.estimate->width : 0.0);
                state_->height->setValue(project.estimate ? project.estimate->height : 0.0);
                state_->webThickness->setValue(0.0);
                state_->flangeThickness->setValue(0.0);
                state_->ribThickness->setValue(0.0);
                state_->ribStations->clear();
                state_->parametersDirty = true;
                state_->referenceHint->setText(QStringLiteral("当前点云没有可用的参考参数，请根据实际情况填写尺寸和板厚。"));
                if (project.suggestedParameters) restoreReferenceParameters();
                state_->source->setText(QStringLiteral("%1\n原始点 %2；无效点 %3\n输入比例：1 单位 = %4 mm\n显示原点：%5 mm")
                    .arg(text(project.sourcePathUtf8)).arg(static_cast<qulonglong>(cloud.sourcePointCount))
                    .arg(static_cast<qulonglong>(cloud.invalidPointCount)).arg(cloud.sourceScaleToMm).arg(vectorText(cloud.originMm)));
                if (project.estimate) {
                    const auto& estimate = *project.estimate;
                    state_->frame->setText(QStringLiteral("PCA 原始框架（交换前），须核对：\n中心 %1 mm\n梁长轴 %2\n宽度轴 %3\n高度轴 %4")
                        .arg(vectorText(estimate.frame.originMm), vectorText(estimate.frame.xAxis), vectorText(estimate.frame.yAxis), vectorText(estimate.frame.zAxis)));
                    state_->modelState->setText(project.suggestedParameters
                        ? QStringLiteral("已填入参考参数，点击一键构建即可生成初始模型；参数可继续修改。")
                        : QStringLiteral("已导入新点云，尚未生成模型。请填写尺寸与板厚后点击一键构建。"));
                    for (const auto& warning : estimate.warnings) appendLog(QStringLiteral("初值提示：") + text(warning));
                } else {
                    state_->frame->setText(QStringLiteral("当前点云不能提供可靠的三维 PCA 梁框架。"));
                    state_->modelState->setText(QStringLiteral("点云已导入，可查看。当前桌面辅助 H 型钢建模需要可用的坐标框架，暂未启用。"));
                    appendLog(QStringLiteral("导入成功，但没有可用的梁框架初值；点云显示仍可使用。"));
                }
                for (const auto& warning : project.warnings) appendLog(QStringLiteral("导入提示：") + text(warning));
                refreshDisplay();
            } else if (operation == State::Operation::Reconstruct && project.model) {
                state_->parametersDirty = false;
                state_->modelState->setText(QStringLiteral("初始模型版本 %1\n%2 个板件实体；体积 %3 mm³\n已按当前参数生成，可修改后再次构建；尺寸精度仍需核对。")
                    .arg(static_cast<qulonglong>(project.revision)).arg(static_cast<qulonglong>(project.model->solidCount)).arg(project.model->volumeMm3, 0, 'g', 10));
                refreshDisplay();
            } else if (operation == State::Operation::Export && project.lastExport) {
                appendLog(QStringLiteral("STEP 回读：%1 个实体，%2 字节；文件：%3")
                    .arg(static_cast<qulonglong>(project.lastExport->solidCount)).arg(static_cast<qulonglong>(project.lastExport->fileBytes)).arg(text(project.exportedPathUtf8)));
            } else if (operation == State::Operation::Deviation && project.deviation) {
                const auto& d = *project.deviation;
                appendLog(QStringLiteral("偏差（mm）：均值 %1，RMS %2，P95 %3，最大 %4；%5 点。仅当前矩形板件表面，不能代替整体精度验收。")
                    .arg(d.meanMm, 0, 'g', 8).arg(d.rmsMm, 0, 'g', 8).arg(d.p95Mm, 0, 'g', 8).arg(d.maximumMm, 0, 'g', 8).arg(static_cast<qulonglong>(d.pointCount)));
            }
        } else if (task.state == TaskState::Cancelled) {
            appendLog(QStringLiteral("任务已取消，保留原工程状态。"));
        } else {
            reportError(text(task.errorCode) + QStringLiteral(": ") + text(task.message));
        }
        updateActions();
    } catch (const std::exception& error) {
        state_->activeTask.clear();
        state_->pending = State::Operation::None;
        reportError(text(error.what()));
        updateActions();
    }
    if (state_->closing && state_->activeTask.empty()) QTimer::singleShot(0, this, [this] { close(); });
}

void MainWindow::closeEvent(QCloseEvent* event) {
    if (!state_->activeTask.empty()) {
        state_->closing = true;
        (void)state_->service.cancel(state_->activeTask);
        state_->status->setText(QStringLiteral("正在取消任务并等待安全退出…"));
        updateActions();
        event->ignore();
        return;
    }
    state_->timer->stop();
    event->accept();
}

} // namespace reverse::workbench
