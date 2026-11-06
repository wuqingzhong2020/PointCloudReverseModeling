/* 文件描述：剖视菜单打开即启用可用平面，选择或修改偏移时提交可录制命令；失败保留已提交状态。 */
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QWidgetAction>

#include "commands/SectionPlane.h"
#include "workbench/SectionTool.h"

namespace reverse::workbench
{
SectionTool::SectionTool(CommandService &service, QWidget *parent) : QMenu(parent), m_service(service)
{
    setObjectName(QStringLiteral("menu.section"));
    auto *panel = new QWidget(this);
    panel->setMinimumWidth(280);
    auto *form = new QFormLayout(panel);
    form->setContentsMargins(14, 12, 14, 12);
    form->addRow(new QLabel(QStringLiteral("剖视"), panel));
    m_plane = new QComboBox(panel);
    m_plane->setObjectName(QStringLiteral("section.plane"));
    m_plane->setPlaceholderText(QStringLiteral("暂无可用平面"));
    form->addRow(m_plane);
    m_offset = new QDoubleSpinBox(panel);
    m_offset->setRange(-1e9, 1e9);
    m_offset->setDecimals(3);
    m_offset->setSingleStep(1);
    m_offset->setKeyboardTracking(false);
    m_offset->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("偏移"), m_offset);
    m_reverse = new QCheckBox(QStringLiteral("反向保留"), panel);
    form->addRow(m_reverse);
    m_hint = new QLabel(panel);
    m_hint->setWordWrap(true);
    m_hint->setMaximumWidth(300);
    form->addRow(m_hint);
    auto *widgetAction = new QWidgetAction(this);
    widgetAction->setDefaultWidget(panel);
    addAction(widgetAction);
    connect(this, &QMenu::aboutToShow, this, [this] {
        if (!m_project.section.enabled) setSectionEnabled(true);
    });
    connect(m_plane, &QComboBox::activated, this, [this] { apply(); });
    connect(m_offset, &QDoubleSpinBox::valueChanged, this, [this] { apply(); });
    connect(m_reverse, &QCheckBox::toggled, this, [this] { apply(); });
    updateControls();
}

void SectionTool::setSectionEnabled(bool enabled)
{
    if (enabled) apply();
    else
    {
        auto section = m_project.section;
        section.enabled = false;
        submit(section);
    }
}

void SectionTool::apply()
{
    if (m_plane->currentIndex() < 0) return;
    submit({true, m_offset->value(), m_reverse->isChecked(),
            static_cast<SectionPlaneSource>(m_plane->currentData().toInt())});
}

void SectionTool::submit(PlaneSection section)
{
    if (!m_idle || !m_task.empty() || m_project.section == section) return;
    try
    {
        m_task = m_service.submitSection(section, m_project.revision);
        m_idle = false;
        m_hint->setText(QStringLiteral("正在更新剖视…"));
        updateControls();
        if (onChanged) onChanged();
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure("点云剖视", error.what());
    }
}

void SectionTool::sync(const ProjectSnapshot &project, bool idle)
{
    bool changed = m_project.cloudId != project.cloudId || m_project.modelId != project.modelId ||
        m_project.referencePlane != project.referencePlane || m_project.section != project.section;
    m_project = project;
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            m_task.clear();
            changed = true;
            m_hint->setText(QString::fromUtf8(task.message));
        }
    }
    m_idle = idle && m_task.empty();
    if (changed)
    {
        const QSignalBlocker planeBlock(m_plane), offsetBlock(m_offset), reverseBlock(m_reverse);
        m_plane->clear();
        if (bottomSectionPlane(project))
            m_plane->addItem(QStringLiteral("底板面1"), static_cast<int>(SectionPlaneSource::Bottom));
        if (project.referencePlane)
            m_plane->addItem(QStringLiteral("基准面"), static_cast<int>(SectionPlaneSource::Reference));
        const int selected = m_plane->findData(static_cast<int>(project.section.source));
        m_plane->setCurrentIndex(selected >= 0 ? selected : m_plane->count() ? 0 : -1);
        m_offset->setValue(project.section.offsetMm);
        m_reverse->setChecked(project.section.reversed);
    }
    updateControls();
}

void SectionTool::updateControls()
{
    const bool available = m_idle && m_project.cloud && m_plane->count() > 0;
    m_plane->setEnabled(available);
    m_offset->setEnabled(available);
    m_reverse->setEnabled(available);
    if (m_task.empty() && m_hint->text().isEmpty())
        m_hint->setText(QStringLiteral("选择后立即应用；右上角“剖视面”用于正视。只过滤点云显示。"));
}
} // namespace reverse::workbench
