/* 文件描述：按剩余点云预览和提交局部重建，错误统一输出命令窗口，不阻塞 GUI。 */
#include <algorithm>
#include <set>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "geometry/Geometry.h"
#include "workbench/LocalReconstructionTool.h"
#include "workbench/DockWorkspace.h"

namespace reverse::workbench
{
LocalReconstructionTool::LocalReconstructionTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    auto *layout = new QVBoxLayout(this);
    m_selection = new QLabel(this);
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);
    auto *form = new QFormLayout;
    m_form = form;
    m_type = new QComboBox(this);
    m_type->setObjectName("localRebuildType");
    m_type->addItem(QStringLiteral("平板（保持原板面）"), "plate");
    m_type->addItem(QStringLiteral("水平板（基准面方向）"), "horizontal");
    m_type->addItem(QStringLiteral("包围盒"), "box");
    m_contour = new QComboBox(this);
    m_contour->setObjectName("localRebuildContour");
    const struct { const char *label; const char *value; } contours[] = {
        {"矩形", "rectangle"}, {"圆形", "circle"}, {"倒角矩形", "chamfer_rectangle"},
        {"1/4孔角矩形", "quarter_hole_rectangle"}, {"圆角矩形", "rounded_rectangle"}};
    for (const auto &contour : contours)
        m_contour->addItem(QString::fromUtf8(contour.label), contour.value);
    m_corner = new QDoubleSpinBox(this);
    m_corner->setRange(0.1, 100000);
    m_corner->setDecimals(1);
    m_corner->setValue(5);
    m_corner->setSuffix(" mm");
    form->addRow(QStringLiteral("重建为"), m_type);
    form->addRow(QStringLiteral("轮廓"), m_contour);
    m_cornerLabel = new QLabel(QStringLiteral("倒角退距"), this);
    form->addRow(m_cornerLabel, m_corner);
    layout->addLayout(form);
    m_mirrorBottom = new QCheckBox(QStringLiteral("镜像重建竖直板下边"), this);
    m_mirrorBottom->setObjectName("localRebuildMirrorBottom");
    m_mirrorBottom->setChecked(true);
    m_mirrorBottom->setToolTip(QStringLiteral("沿用原板底边补齐俯视扫描不可见的下部，并镜像上角；关闭后按可见点范围重建，仅处理上角。水平板不受影响。"));
    layout->addWidget(m_mirrorBottom);
    connect(m_mirrorBottom, &QCheckBox::toggled, this, [this] { invalidate(); });
    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    layout->addWidget(m_info);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), this);
    layout->addWidget(m_original);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    layout->addStretch();
    auto *buttons = new QHBoxLayout;
    m_previewButton = new QPushButton(QStringLiteral("预览"), this);
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    auto *exit = new QPushButton(QStringLiteral("退出"), this);
    for (auto *b : {m_previewButton, m_apply, exit})
        buttons->addWidget(b);
    layout->addLayout(buttons);
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    connect(m_original, &QCheckBox::toggled, this, [this](bool original) {
        if (!m_loading && m_preview)
            showModel(original);
    });
    for (auto *combo : {m_type, m_contour})
        connect(combo, &QComboBox::currentIndexChanged, this, [this] {
            invalidate();
            updateInputs(m_service.isIdle());
        });
    connect(m_corner, &QDoubleSpinBox::valueChanged, this, [this] { invalidate(); });
    connect(m_corner->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this, [this] { preview(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    updateInputs(false);
}
void LocalReconstructionTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
void LocalReconstructionTool::begin()
{
    cancel();
    m_active = true;
    m_revision = m_service.project().revision;
    selectionChanged(m_viewport.selectedObjects());
    updateInputs(m_service.isIdle());
    if (onChanged)
        onChanged();
}
void LocalReconstructionTool::selectionChanged(const std::vector<ModelSelection> &selection)
{
    if (!m_active || m_loading || !m_service.isIdle())
        return;
    std::set<std::string> ids;
    for (const auto &s : selection)
        if (s.kind == ModelPickMode::Body)
            ids.insert(s.partId);
    std::vector<std::string> parts(ids.begin(), ids.end());
    if (parts != m_request.partIds)
    {
        invalidate();
        m_request.partIds = std::move(parts);
    }
    const auto project = m_service.project();
    if (project.model && (!project.cloud || std::any_of(m_request.partIds.begin(), m_request.partIds.end(), [&](const auto &id) {
            return project.model->obstacleParts.contains(id) || project.model->partTransforms.contains(id) ||
                   std::find(project.model->reconstructableParts.begin(), project.model->reconstructableParts.end(), id) == project.model->reconstructableParts.end();
        })))
        m_type->setCurrentIndex(m_type->findData("box"));
    m_selection->setText(m_request.partIds.empty() ? QStringLiteral("请选择需要重建的模型部件")
                                                   : QStringLiteral("已选择 %1 个部件").arg(m_request.partIds.size()));
    updateInputs(true);
}
void LocalReconstructionTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
        return;
    const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    m_loading = true;
    m_viewport.setModelPickingEnabled(true);
    m_viewport.setModel(original || !m_preview
                            ? makeModelDisplayParts(*project.mesh, *project.model, origin)
                            : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin));
    if (project.revision == m_revision)
        m_viewport.setSelectedParts(m_request.partIds);
    m_viewport.setModelPickingEnabled(!m_showing);
    m_loading = false;
}
void LocalReconstructionTool::invalidate()
{
    if (m_loading)
        return;
    if (m_showing)
    {
        m_showing = false;
        showModel(true);
    }
    m_preview.reset();
    m_loading = true;
    m_original->setChecked(false);
    m_loading = false;
    m_apply->setEnabled(false);
    m_original->setEnabled(false);
    m_hint->setText(QStringLiteral("选择部件和规则后预览；应用前不修改模型。"));
    if (onChanged)
        onChanged();
}
void LocalReconstructionTool::updateInputs(bool idle)
{
    const bool ready = m_active && idle && m_task.empty() && m_applyTask.empty();
    m_type->setEnabled(ready);
    const bool box = m_type->currentData() == "box";
    const auto project = m_service.project();
    const bool plateReady = project.model && project.cloud && !project.cloud->points.empty() &&
        std::all_of(m_request.partIds.begin(), m_request.partIds.end(), [&](const auto &id) {
            return !project.model->obstacleParts.contains(id) && !project.model->partTransforms.contains(id) &&
                std::find(project.model->reconstructableParts.begin(), project.model->reconstructableParts.end(), id) != project.model->reconstructableParts.end();
        });
    if (auto *items = qobject_cast<QStandardItemModel *>(m_type->model()))
        for (const auto value : {"plate", "horizontal"}) items->item(m_type->findData(value))->setEnabled(plateReady);
    m_form->setRowVisible(m_contour, !box);
    m_form->setRowVisible(m_corner, !box);
    m_contour->setEnabled(ready && !box);
    m_info->setText(box
        ? QStringLiteral("按所选模型当前占用范围生成盒状障碍物，保留原包络余量；无需原始点云或板厚。\n"
                         "Shift 可多选，每个对象单独生成包围盒。只替换所选对象，支持撤销；结果不显示板厚、不参与工件镜像。")
        : QStringLiteral("Shift 点击选择多个部件。使用原部件关联的剩余点云，先删掉多余点再重建。\n"
                         "只替换选中部件；其开孔、圆角等修改会被新轮廓替代。未选部件保持不变。\n"
                         "平板保持原有板厚；仅拟合外轮廓，不重新识别内孔。水平板沿基准面法向（无基准面时为世界 Z）。"));
    const auto contour = m_contour->currentData().toString();
    const bool quarterHole = contour == "quarter_hole_rectangle";
    const bool rounded = contour == "rounded_rectangle";
    m_cornerLabel->setText(quarterHole ? QStringLiteral("缺口半径")
                                     : rounded ? QStringLiteral("圆角半径") : QStringLiteral("倒角退距"));
    m_corner->setToolTip(quarterHole ? QStringLiteral("角部向内切除 1/4 圆孔，圆心位于原矩形角点；半径须小于短边一半。")
                                   : rounded ? QStringLiteral("角部用与相邻直边相切的圆弧连接；半径须小于短边一半。")
                                             : QStringLiteral("各角的倒角退距，须小于短边一半。"));
    m_corner->setEnabled(ready && (contour == "chamfer_rectangle" || quarterHole || rounded) && m_type->currentData() != "box");
    const bool cornerRectangle = contour == "chamfer_rectangle" || quarterHole || rounded;
    m_mirrorBottom->setVisible(cornerRectangle && !box);
    m_mirrorBottom->setEnabled(ready && cornerRectangle && m_type->currentData() == "plate");
    m_previewButton->setEnabled(ready && !m_request.partIds.empty());
    m_apply->setEnabled(ready && static_cast<bool>(m_preview));
    m_original->setEnabled(ready && static_cast<bool>(m_preview));
}
void LocalReconstructionTool::preview()
{
    if (!m_active || !m_service.isIdle() || m_request.partIds.empty())
        return;
    invalidate();
    m_request.type = m_type->currentData().toString().toStdString();
    m_request.contour = m_contour->currentData().toString().toStdString();
    m_request.cornerMm = m_corner->value();
    m_request.mirrorBottom = m_mirrorBottom->isChecked();
    const auto project = m_service.project();
    m_request.up = project.referencePlane ? project.referencePlane->normal : Vec3{0, 0, 1};
    try
    {
        m_task = m_service.submitLocalReconstructionPreview(m_request, m_revision);
        m_hint->setText(m_request.type == "box" ? QStringLiteral("正在生成所选模型的占用空间包围盒…")
                                                : QStringLiteral("正在根据剩余点云计算预览…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure("局部重建预览", error.what());
    }
    updateInputs(false);
    if (onChanged)
        onChanged();
}
void LocalReconstructionTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle())
        return;
    try
    {
        m_applyTask = m_service.submitLocalReconstruction(m_request, m_revision);
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure("局部重建", error.what());
    }
    updateInputs(false);
    if (onChanged)
        onChanged();
}
void LocalReconstructionTool::cancel()
{
    m_active = false;
    if (!m_task.empty())
        (void)m_service.cancel(m_task);
    invalidate();
    updateInputs(false);
}
void LocalReconstructionTool::sync(const ProjectSnapshot &project, bool idle)
{
    if (m_loading)
        return;
    if (m_active && project.revision != m_revision && m_applyTask.empty())
        cancel();
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            auto model = m_service.takeEdgePreview(m_task);
            m_task.clear();
            if (m_active && project.revision == m_revision)
            {
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
                if (task.state == TaskState::Succeeded && model)
                {
                    m_preview = std::move(model);
                    m_showing = true;
                    showModel(false);
                }
            }
        }
    }
    if (!m_applyTask.empty())
    {
        const auto task = m_service.task(m_applyTask);
        if (task.finished())
        {
            m_applyTask.clear();
            cancel();
            if (task.result.committed && onCommitted)
                onCommitted();
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    updateInputs(idle);
}
} // namespace reverse::workbench
