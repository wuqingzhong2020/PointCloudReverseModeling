/*
 * 文件描述：基准面设置面板管理临时选点与取消恢复，确认后通过 FIFO 工作线程提交可录制命令。
 * 轮询仅同步小型视图状态；原始点云和模型不变，剖视不提交几何删除或重建任务。
 */
#include <algorithm>

#include <QButtonGroup>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include "geometry/ReferencePlane.h"
#include "workbench/ReferencePlaneTool.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
ReferencePlaneTool::ReferencePlaneTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel(QStringLiteral("设置正视和点云剖视的参考平面。保持原始坐标、板厚和模型不变。"), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    auto *methods = new QHBoxLayout();
    auto *group = new QButtonGroup(this);
    m_three = new QPushButton(WorkbenchTheme::icon(QStringLiteral("three-points")), QStringLiteral("3 点"), this);
    m_face = new QPushButton(WorkbenchTheme::icon(QStringLiteral("plane")), QStringLiteral("特征面"), this);
    for (auto *button : {m_three, m_face})
    {
        button->setCheckable(true);
        button->setMinimumHeight(65);
        group->addButton(button);
        methods->addWidget(button);
    }
    layout->addLayout(methods);
    connect(m_three, &QPushButton::clicked, this, [this] { choose(ReferencePlanePickMode::ThreePoints); });
    connect(m_face, &QPushButton::clicked, this, [this] { choose(ReferencePlanePickMode::Face); });
    m_hint = new QLabel(QStringLiteral("选择三点或特征面开始设置。"), this);
    m_hint->setWordWrap(true);
    m_coordinates = new QLabel(this);
    m_coordinates->setWordWrap(true);
    m_coordinates->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_hint);
    layout->addWidget(m_coordinates);
    auto *editRow = new QHBoxLayout();
    auto *reset = new QPushButton(QStringLiteral("重新选择"), this);
    m_flip = new QPushButton(QStringLiteral("反转法线"), this);
    editRow->addWidget(reset);
    editRow->addWidget(m_flip);
    layout->addLayout(editRow);
    connect(reset, &QPushButton::clicked, this, [this] {
        choose(m_face->isChecked() ? ReferencePlanePickMode::Face : ReferencePlanePickMode::ThreePoints);
    });
    connect(m_flip, &QPushButton::clicked, this, [this] {
        if (m_points.size() != 3) return;
        std::swap(m_points[1], m_points[2]);
        m_viewport.setReferencePreview(m_points);
        picked(m_points, {});
    });
    auto *confirmRow = new QHBoxLayout();
    m_confirm = new QPushButton(QStringLiteral("确定"), this);
    m_confirm->setProperty("primary", true);
    auto *cancelButton = new QPushButton(QStringLiteral("取消选择"), this);
    confirmRow->addWidget(m_confirm);
    confirmRow->addWidget(cancelButton);
    layout->addLayout(confirmRow);
    connect(m_confirm, &QPushButton::clicked, this, [this] { submit(false); });
    connect(cancelButton, &QPushButton::clicked, this, [this] { cancel(); });
    m_front = new QPushButton(QStringLiteral("正视基准面"), this);
    m_clear = new QPushButton(QStringLiteral("清除基准面"), this);
    auto *planeRow = new QHBoxLayout();
    planeRow->addWidget(m_front);
    planeRow->addWidget(m_clear);
    layout->addLayout(planeRow);
    connect(m_clear, &QPushButton::clicked, this, [this] { submit(true); });
    connect(m_front, &QPushButton::clicked, this, [this] {
        if (m_project.referencePlane)
            m_viewport.showBottomView(m_project.referencePlane->normal, m_project.referencePlane->xAxis);
    });
    auto *sectionGroup = new QGroupBox(QStringLiteral("点云剖视"), this);
    auto *sectionForm = new QFormLayout(sectionGroup);
    m_section = new QCheckBox(QStringLiteral("启用剖视"), sectionGroup);
    m_reverse = new QCheckBox(QStringLiteral("反向保留"), sectionGroup);
    m_offset = new QDoubleSpinBox(sectionGroup);
    m_offset->setRange(-1e9, 1e9);
    m_offset->setDecimals(3);
    m_offset->setSuffix(QStringLiteral(" mm"));
    sectionForm->addRow(m_section);
    sectionForm->addRow(QStringLiteral("法线偏移"), m_offset);
    sectionForm->addRow(m_reverse);
    auto *sectionHint = new QLabel(QStringLiteral("保留法线 N 一侧。只过滤显示；框选仍贯穿深度，重建和导出使用完整点云。"), sectionGroup);
    sectionHint->setWordWrap(true);
    sectionForm->addRow(sectionHint);
    m_sectionApply = new QPushButton(QStringLiteral("应用剖视"), sectionGroup);
    sectionForm->addRow(m_sectionApply);
    connect(m_sectionApply, &QPushButton::clicked, this, [this] {
        if (!m_idle || picking()) return;
        try
        {
            m_task = m_service.submitSection({m_section->isChecked(), m_offset->value(), m_reverse->isChecked()},
                                            m_project.revision);
            m_idle = false;
            updateControls();
        }
        catch (const std::exception &error)
        {
            m_hint->setText(QString::fromUtf8(error.what()));
            m_service.reportUiFailure("点云剖视", error.what());
        }
    });
    layout->addWidget(sectionGroup);
    layout->addStretch();
    m_viewport.onReferencePicked = [this](const auto &points, const auto &error) { picked(points, error); };
    m_viewport.onReferenceCancelled = [this] { cancel(); };
    updateControls();
}

void ReferencePlaneTool::begin() { choose(ReferencePlanePickMode::ThreePoints); }
void ReferencePlaneTool::choose(ReferencePlanePickMode mode)
{
    if (!m_idle || (!m_cloudVisible && !m_modelVisible) || (mode == ReferencePlanePickMode::Face && !m_modelVisible)) return;
    m_mode = mode;
    m_points.clear();
    m_three->setChecked(mode == ReferencePlanePickMode::ThreePoints);
    m_face->setChecked(mode == ReferencePlanePickMode::Face);
    m_viewport.setReferencePlanePicking(mode);
    picked({}, {});
    updateControls();
}
void ReferencePlaneTool::cancel()
{
    if (!picking()) return;
    m_mode = ReferencePlanePickMode::None;
    m_points.clear();
    m_viewport.setReferencePlanePicking(m_mode);
    m_hint->setText(QStringLiteral("已结束选择，保留已确认的基准面。"));
    m_coordinates->clear();
    updateControls();
}
void ReferencePlaneTool::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}
void ReferencePlaneTool::picked(const std::vector<Vec3> &points, const QString &error)
{
    m_points = points;
    if (!error.isEmpty())
    {
        m_service.reportUiFailure("基准面拾取", error.toUtf8().toStdString());
    }
    QStringList lines;
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto &p = points[i];
        lines << QStringLiteral("%1：(%2, %3, %4) mm").arg(i + 1)
            .arg(p.x + m_origin.x, 0, 'f', 3).arg(p.y + m_origin.y, 0, 'f', 3).arg(p.z + m_origin.z, 0, 'f', 3);
    }
    m_coordinates->setText(lines.join('\n'));
    m_hint->setText(!error.isEmpty() ? error : points.size() == 3
        ? QStringLiteral("平面预览已就绪，检查法线 N 后点击确定。")
        : m_mode == ReferencePlanePickMode::Face ? QStringLiteral("左键选择模型的一个平面；Esc / 右键退出。")
        : QStringLiteral("已选 %1 / 3 点，支持顶点吸附；三点不能共线。Esc / 右键退出。").arg(points.size()));
    updateControls();
}
void ReferencePlaneTool::submit(bool clear)
{
    if (!m_idle || (!clear && m_points.size() != 3)) return;
    try
    {
        std::optional<std::array<Vec3, 3>> points;
        if (!clear)
        {
            points.emplace();
            for (std::size_t i = 0; i < 3; ++i)
                (*points)[i] = {m_points[i].x + m_origin.x, m_points[i].y + m_origin.y, m_points[i].z + m_origin.z};
        }
        m_task = m_service.submitReferencePlane(points, m_project.revision);
        cancel();
        m_idle = false;
        updateControls();
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure("设置基准面", error.what());
    }
}
void ReferencePlaneTool::sync(const ProjectSnapshot &project, bool idle, bool cloudVisible, bool modelVisible)
{
    const bool geometryChanged = m_project.cloudId != project.cloudId || m_project.modelId != project.modelId;
    const bool changed = geometryChanged || m_project.referencePlane != project.referencePlane || m_project.section != project.section;
    if (geometryChanged || !idle || (!cloudVisible && !modelVisible) ||
        (m_mode == ReferencePlanePickMode::Face && !modelVisible)) cancel();
    m_project = project;
    m_idle = idle;
    m_cloudVisible = cloudVisible;
    m_modelVisible = modelVisible;
    m_origin = project.cloud ? project.cloud->originMm : project.model ? project.model->frame.originMm : Vec3{};
    if (geometryChanged && !project.referencePlane)
    {
        m_hint->setText(QStringLiteral("选择三点或特征面开始设置。"));
        m_coordinates->clear();
    }
    if (changed)
    {
        m_viewport.setReferencePlane(project.referencePlane, project.section, m_origin);
        m_section->setChecked(project.section.enabled);
        m_offset->setValue(project.section.offsetMm);
        m_reverse->setChecked(project.section.reversed);
        if (onChanged) onChanged();
    }
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
            m_task.clear();
        }
    }
    updateControls();
}
void ReferencePlaneTool::updateControls()
{
    m_three->setEnabled(m_idle && (m_cloudVisible || m_modelVisible));
    m_face->setEnabled(m_idle && m_modelVisible);
    m_confirm->setEnabled(m_idle && picking() && m_points.size() == 3);
    m_flip->setEnabled(m_confirm->isEnabled());
    const bool plane = m_idle && static_cast<bool>(m_project.referencePlane);
    m_clear->setEnabled(plane);
    m_front->setEnabled(plane && !picking());
    const bool section = plane && !picking() && static_cast<bool>(m_project.cloud);
    m_sectionApply->setEnabled(section);
    m_section->setEnabled(section);
    m_offset->setEnabled(section);
    m_reverse->setEnabled(section);
}
}
