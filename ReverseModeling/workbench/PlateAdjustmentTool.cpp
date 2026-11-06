/* 文件描述：回车只触发预览；切换参数使预览失效，确认后通过统一命令提交实体修改。 */
#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QScopedValueRollback>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "geometry/Geometry.h"
#include "workbench/PlateAdjustmentTool.h"

namespace reverse::workbench
{
PlateAdjustmentTool::PlateAdjustmentTool(CommandService &service, OccViewport &viewport, PlateAdjustmentKind kind,
                                         QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport), m_kind(kind)
{
    auto *layout = new QVBoxLayout(this);
    m_selection = new QLabel(this);
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);
    auto *form = new QFormLayout;
    m_current = new QLabel(QStringLiteral("尚未选择板件"), this);
    m_current->setWordWrap(true);
    form->addRow(QStringLiteral("当前板厚"), m_current);
    m_size = new QDoubleSpinBox(this);
    m_size->setObjectName(kind == PlateAdjustmentKind::Contour ? QStringLiteral("plateContour.distance")
                                                               : QStringLiteral("plateThickness.size"));
    m_size->setDecimals(kind == PlateAdjustmentKind::Thickness ? 1 : 3);
    m_size->setRange(kind == PlateAdjustmentKind::Thickness ? 0.1 : 0.001, 1e6);
    m_size->setValue(10);
    m_size->setSuffix(QStringLiteral(" mm"));
    form->addRow(kind == PlateAdjustmentKind::Contour ? QStringLiteral("外扩距离") : QStringLiteral("目标板厚"),
                 m_size);
    m_plane = new QComboBox(this);
    m_plane->setObjectName(QStringLiteral("mirrorCompletion.planePart"));
    m_plane->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_plane->setMinimumContentsLength(12);
    form->addRow(QStringLiteral("镜像面"), m_plane);
    form->setRowVisible(m_plane, kind == PlateAdjustmentKind::Mirror);
    form->setRowVisible(m_current, kind != PlateAdjustmentKind::Mirror);
    form->setRowVisible(m_size, kind != PlateAdjustmentKind::Mirror);
    connect(m_plane, &QComboBox::currentIndexChanged, this, [this] {
        if (m_loading) return;
        invalidate();
        m_request.planePartId = m_plane->currentData().toString().toStdString();
    });
    layout->addLayout(form);
    auto *explanation =
        new QLabel((kind == PlateAdjustmentKind::Mirror
                        ? QStringLiteral("以指定底板（腹板）的中面补齐另一侧。保留原有几何；相接部分合并，"
                                         "分离部分新增。板厚、内孔及原有圆角随镜像保留。\n"
                                         "导入模型请选择实际底板；不要把底板选入待补全列表。\n")
                        : kind == PlateAdjustmentKind::Contour
                        ? QStringLiteral("外边界在板面内向外等距偏移，保持板厚与封闭内孔。开放缺口随外轮廓偏移。\n"
                                         "再次应用会继续外扩；要改上次距离，请先撤销。\n")
                        : QStringLiteral("保持板件中面不动，向两侧等量增减厚度，保留轮廓及贯穿孔。\n")) +
                       QStringLiteral("可选：体；Shift 点击追加或取消，支持多个板件统一修改。\n"
                                      "回车或“预览”查看效果；点击“应用”才修改实体并录制命令。"),
                   this);
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), this);
    m_original->setEnabled(false);
    layout->addWidget(m_original);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    layout->addStretch();
    auto *row = new QHBoxLayout;
    m_previewButton = new QPushButton(QStringLiteral("预览"), this);
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    m_apply->setProperty("primary", true);
    auto *exit = new QPushButton(QStringLiteral("退出"), this);
    for (auto *button : {m_previewButton, m_apply, exit})
    {
        button->setAutoDefault(false);
        row->addWidget(button);
    }
    layout->addLayout(row);
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    connect(m_size->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this,
            [this]
            {
                m_size->interpretText();
                preview();
            });
    connect(m_size, &QDoubleSpinBox::valueChanged, this,
            [this]
            {
                if (!m_loading)
                {
                    invalidate();
                }
            });
    connect(m_original, &QCheckBox::toggled, this,
            [this](bool original)
            {
                if (!m_loading && m_preview)
                {
                    showModel(original);
                }
            });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    m_apply->setEnabled(false);
}
void PlateAdjustmentTool::begin()
{
    cancel();
    if (!m_task.empty() || !m_applyTask.empty() || !m_inspectTask.empty() || !m_service.isIdle())
    {
        return;
    }
    const auto project = m_service.project();
    if (!project.model)
    {
        return;
    }
    m_revision = project.revision;
    m_request = {};
    if (m_kind == PlateAdjustmentKind::Mirror)
    {
        const QScopedValueRollback<bool> loading(m_loading, true);
        const auto previous = m_plane->currentData();
        m_plane->clear();
        m_plane->addItem(QStringLiteral("请选择底板（腹板）"), QString{});
        for (const auto &id : project.model->partIds)
        {
            const auto name = QString::fromStdString(id);
            m_plane->addItem(id == "web" ? QStringLiteral("底板（腹板） · web") : name, name);
        }
        int index = m_plane->findData(previous);
        if (index <= 0) index = m_plane->findData(QStringLiteral("web"));
        m_plane->setCurrentIndex(std::max(0, index));
        m_request.planePartId = m_plane->currentData().toString().toStdString();
    }
    m_selectionError.clear();
    m_active = true;
    setInputsEnabled(true);
    m_apply->setEnabled(false);
    m_hint->setText(QStringLiteral("请选择实体板件，然后预览效果。"));
    updateSelectionText();
    selectionChanged(m_viewport.selectedObjects());
    if (onChanged)
    {
        onChanged();
    }
}
void PlateAdjustmentTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || m_loading || !m_task.empty() || !m_applyTask.empty() || !m_inspectTask.empty() || m_previewShown)
    {
        return;
    }
    const auto project = m_service.project();
    if (!project.mesh || project.revision != m_revision)
    {
        return;
    }
    m_request.partIds.clear();
    m_selectionError.clear();
    for (const auto &selection : selections)
    {
        if (selection.kind != ModelPickMode::Body)
        {
            m_selectionError = QStringLiteral("请切换到可选：体，选择实体板件。");
            break;
        }
        if (std::find(m_request.partIds.begin(), m_request.partIds.end(), selection.partId) == m_request.partIds.end())
        {
            m_request.partIds.push_back(selection.partId);
        }
    }
    invalidate();
    updateSelectionText();
    if (m_kind == PlateAdjustmentKind::Mirror)
    {
        setInputsEnabled(true);
        if (onChanged) onChanged();
        return;
    }
    m_current->setText(QStringLiteral("尚未读取"));
    setInputsEnabled(false);
    if (!m_request.partIds.empty() && m_selectionError.isEmpty() && m_service.isIdle())
    {
        try
        {
            m_discard = false;
            m_inspectTask = m_service.submitThicknessInspection(m_request.partIds, m_revision);
            m_hint->setText(QStringLiteral("正在读取所选实体的真实板厚…"));
        }
        catch (const std::exception &error)
        {
            m_selectionError = QString::fromUtf8(error.what());
            m_hint->setText(m_selectionError);
            m_service.reportUiFailure("板厚识别", error.what());
        }
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateAdjustmentTool::updateSelectionText()
{
    m_selection->setText(m_request.partIds.empty() ? QStringLiteral("请选择等厚实体板件")
                                                   : QStringLiteral("已选择 %1 个板件").arg(m_request.partIds.size()));
}
void PlateAdjustmentTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
    {
        return;
    }
    const Vec3 origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    // 创建替换模型时先生成选择对象，再禁用拾取；否则切换原模型对比后无法恢复高亮。
    if (m_previewShown)
    {
        m_viewport.setModelPickingEnabled(true);
    }
    if (original || !m_preview)
    {
        m_viewport.setModel(makeModelDisplayParts(*project.mesh, *project.model, origin));
    }
    else
    {
        auto parts = makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin);
        if (m_kind == PlateAdjustmentKind::Mirror)
        {
            for (auto &part : parts)
                if (m_preview->summary().mirroredParts.contains(part.id) && !project.model->mirroredParts.contains(part.id))
                    part.colorSrgb = {0.30, 0.82, 0.40};
        }
        m_viewport.setModel(std::move(parts));
    }
    if (m_previewShown)
    {
        m_viewport.setModelPickingEnabled(false);
    }
    // 预览替换了显示对象；按稳定部件 ID 恢复高亮，清楚显示正在修改的板件。
    if (project.revision == m_revision)
    {
        m_viewport.setSelectedParts(m_request.partIds);
    }
}
void PlateAdjustmentTool::invalidate()
{
    const QScopedValueRollback<bool> loading(m_loading, true);
    if (m_previewShown)
    {
        m_previewShown = false;
        showModel(true);
    }
    m_preview.reset();
    m_apply->setEnabled(false);
    m_original->setChecked(false);
    m_original->setEnabled(false);
    if (m_active)
    {
        m_hint->setText(!m_selectionError.isEmpty() ? m_selectionError
                        : m_request.partIds.empty() ? QStringLiteral("请选择等厚实体板件，然后预览。")
                                                    : QStringLiteral("参数或选择已改变，请预览后再应用。"));
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateAdjustmentTool::setInputsEnabled(bool enabled)
{
    for (QWidget *w : std::initializer_list<QWidget *>{m_size, m_plane, m_previewButton})
    {
        w->setEnabled(enabled);
    }
    m_previewButton->setEnabled(enabled && !m_request.partIds.empty() && m_selectionError.isEmpty());
}
void PlateAdjustmentTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty() || !m_inspectTask.empty())
    {
        return;
    }
    if (!m_selectionError.isEmpty() || m_request.partIds.empty())
    {
        m_hint->setText(m_selectionError.isEmpty() ? QStringLiteral("请先选择等厚实体板件。") : m_selectionError);
        m_service.reportUiFailure(m_kind == PlateAdjustmentKind::Mirror ? "镜像补全" : m_kind == PlateAdjustmentKind::Contour ? "外扩板件轮廓" : "修改板厚", m_hint->text().toUtf8().toStdString());
        return;
    }
    invalidate();
    m_request.valueMm = m_size->value();
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_kind == PlateAdjustmentKind::Mirror
                     ? m_service.submitMirrorCompletionPreview({m_request.partIds, m_request.planePartId}, m_revision)
                     : m_kind == PlateAdjustmentKind::Contour
                     ? m_service.submitContourExpansionPreview({m_request.partIds, m_request.valueMm}, m_revision)
                     : m_service.submitThicknessPreview({m_request.partIds, m_request.valueMm}, m_revision);
        setInputsEnabled(false);
        m_hint->setText(QStringLiteral("正在计算预览… 可退出取消；不会修改模型或录制命令。"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_kind == PlateAdjustmentKind::Mirror ? "镜像补全" : m_kind == PlateAdjustmentKind::Contour ? "外扩板件轮廓" : "修改板厚", error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateAdjustmentTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
    {
        return;
    }
    try
    {
        m_applyTask =
            m_kind == PlateAdjustmentKind::Mirror
                ? m_service.submitMirrorCompletion({m_previewRequest.partIds, m_previewRequest.planePartId}, m_revision)
                : m_kind == PlateAdjustmentKind::Contour
                ? m_service.submitContourExpansion({m_previewRequest.partIds, m_previewRequest.valueMm}, m_revision)
                : m_service.submitThickness({m_previewRequest.partIds, m_previewRequest.valueMm}, m_revision);
        m_apply->setEnabled(false);
        setInputsEnabled(false);
        m_original->setEnabled(false);
        m_hint->setText(QStringLiteral("正在应用实体修改…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_kind == PlateAdjustmentKind::Mirror ? "镜像补全" : m_kind == PlateAdjustmentKind::Contour ? "外扩板件轮廓" : "修改板厚", error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateAdjustmentTool::cancel()
{
    if (!m_task.empty())
    {
        (void)m_service.cancel(m_task);
        m_discard = true;
    }
    if (!m_inspectTask.empty())
    {
        (void)m_service.cancel(m_inspectTask);
        m_discard = true;
    }
    m_active = false;
    invalidate();
    setInputsEnabled(false);
    m_hint->setText(QStringLiteral("板件调整已退出，未应用的预览已取消。"));
}
void PlateAdjustmentTool::sync(const ProjectSnapshot &project, bool idle)
{
    if (m_active && project.revision != m_revision && m_applyTask.empty())
    {
        cancel();
    }
    if (!m_inspectTask.empty())
    {
        const auto task = m_service.task(m_inspectTask);
        if (task.finished())
        {
            m_inspectTask.clear();
            if (m_active && !m_discard && project.revision == m_revision)
            {
                if (task.state == TaskState::Succeeded && !task.plateThicknesses.empty())
                {
                    QStringList values;
                    for (const auto &[id, info] : task.plateThicknesses)
                    {
                        values << QStringLiteral("%1：%2 mm")
                                      .arg(QString::fromStdString(id))
                                      .arg(info.thicknessMm, 0, 'f', 1);
                    }
                    m_current->setText(values.join(QStringLiteral("\n")));
                    m_loading = true;
                    if (m_kind == PlateAdjustmentKind::Thickness)
                    {
                        m_size->setValue(task.plateThicknesses.begin()->second.thicknessMm);
                    }
                    m_loading = false;
                    m_hint->setText(m_kind == PlateAdjustmentKind::Contour
                                        ? QStringLiteral("已确认等厚板件，请输入外扩距离并预览；板厚保持不变。")
                                        : QStringLiteral("已读取真实板厚，请输入目标厚度并预览。"));
                    setInputsEnabled(true);
                }
                else
                {
                    m_selectionError = QString::fromUtf8(task.message.c_str());
                    m_hint->setText(m_selectionError);
                    m_current->setText(QStringLiteral("不支持的板件"));
                    setInputsEnabled(false);
                }
            }
        }
    }
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            auto preview = m_service.takeEdgePreview(m_task);
            m_task.clear();
            if (m_active && !m_discard && project.revision == m_revision)
            {
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
                setInputsEnabled(true);
                if (task.state == TaskState::Succeeded && preview)
                {
                    m_preview = std::move(preview);
                    m_previewShown = true;
                    showModel(false);
                    m_apply->setEnabled(idle);
                    m_original->setEnabled(true);
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
            const bool reopen = m_active && isVisible();
            cancel();
            if (task.result.committed && onCommitted)
            {
                onCommitted();
            }
            if (reopen && idle)
            {
                begin();
            }
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    if (m_preview && m_active)
    {
        m_apply->setEnabled(idle && m_applyTask.empty());
    }
}
void PlateAdjustmentTool::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
