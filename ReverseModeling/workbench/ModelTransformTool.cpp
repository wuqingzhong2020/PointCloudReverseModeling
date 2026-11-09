/* 文件描述：数值和轴柄实时同步，冻结目标部件；取消不改工程，确定经工作线程修改真实 BRep。 */
#include <algorithm>
#include <cmath>
#include <limits>

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "workbench/ModelTransformTool.h"
#include "workbench/DockWorkspace.h"

namespace reverse::workbench
{
ModelTransformTool::ModelTransformTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    auto *layout = new QVBoxLayout(this);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    m_move = new QGroupBox(QStringLiteral("平移参数 · 项目坐标 mm"), this);
    m_rotate = new QGroupBox(QStringLiteral("旋转参数 · 项目坐标轴"), this);
    auto *moveForm = new QFormLayout(m_move);
    auto *rotateForm = new QFormLayout(m_rotate);
    const auto fields = [&](QFormLayout *form, std::array<QDoubleSpinBox *, 3> &inputs, const QString &prefix) {
        const std::array colors{"#f36e6e", "#53c58b", "#5bafff"};
        for (int i = 0; i < 3; ++i)
        {
            auto *input = new QDoubleSpinBox(this);
            input->setRange(-1e12, 1e12);
            input->setDecimals(6);
            input->setSuffix(QStringLiteral(" mm"));
            input->setObjectName(prefix + QString::number(i));
            auto *label = new QLabel(QString(QChar('X' + i)), this);
            label->setStyleSheet(QStringLiteral("color:%1").arg(colors[i]));
            form->addRow(label, input);
            inputs[i] = input;
            connect(input, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
        }
    };
    fields(moveForm, m_offset, QStringLiteral("transform.offset."));
    m_axis = new QComboBox(this);
    m_axis->addItems({QStringLiteral("X 轴"), QStringLiteral("Y 轴"), QStringLiteral("Z 轴")});
    rotateForm->addRow(QStringLiteral("旋转轴"), m_axis);
    m_angle = new QDoubleSpinBox(this);
    m_angle->setObjectName(QStringLiteral("transform.angle"));
    m_angle->setRange(-180, 180);
    m_angle->setDecimals(3);
    m_angle->setSingleStep(5);
    m_angle->setSuffix(QStringLiteral(" °"));
    rotateForm->addRow(QStringLiteral("角度"), m_angle);
    auto *direction = new QLabel(QStringLiteral("只绕所选轴旋转。从轴正向看向原点，正值逆时针，负值顺时针。"), this);
    direction->setWordWrap(true);
    rotateForm->addRow(direction);
    rotateForm->addRow(new QLabel(QStringLiteral("旋转中心 · 项目坐标 mm"), this));
    fields(rotateForm, m_center, QStringLiteral("transform.center."));
    auto *centerReset = new QPushButton(QStringLiteral("使用所选部件中心"), this);
    rotateForm->addRow(centerReset);
    connect(centerReset, &QPushButton::clicked, this, [this] {
        m_request.centerMm = m_defaultCenter;
        fillControls();
        updatePreview();
    });
    connect(m_axis, &QComboBox::currentIndexChanged, this, [this] { updatePreview(); });
    connect(m_angle, &QDoubleSpinBox::valueChanged, this, [this] { updatePreview(); });
    layout->addWidget(m_move);
    layout->addWidget(m_rotate);
    auto *reset = new QPushButton(QStringLiteral("复位预览"), this);
    layout->addWidget(reset);
    connect(reset, &QPushButton::clicked, this, [this] {
        m_request.offsetMm = {};
        m_request.angleDegrees = 0;
        m_request.centerMm = m_defaultCenter;
        fillControls();
        updatePreview();
    });
    layout->addStretch();
    auto *row = new QHBoxLayout();
    m_confirm = new QPushButton(QStringLiteral("确定"), this);
    m_confirm->setProperty("primary", true);
    m_confirm->setEnabled(false);
    auto *cancelButton = new QPushButton(QStringLiteral("取消"), this);
    row->addWidget(m_confirm);
    row->addWidget(cancelButton);
    layout->addLayout(row);
    connect(m_confirm, &QPushButton::clicked, this, [this] { confirm(); });
    connect(cancelButton, &QPushButton::clicked, this, [this] { cancel(); });
    m_viewport.onTransformDragged = [this](const ModelTransform &request) {
        m_request = request;
        fillControls();
        m_confirm->setEnabled(m_active && !isIdentityTransform(m_request));
    };
    m_viewport.onTransformCancelled = [this] { cancel(); };
}
void ModelTransformTool::begin(ModelTransformKind kind)
{
    cancel();
    const auto project = m_service.project();
    m_ids = m_viewport.selectedParts();
    if (!m_service.isIdle() || !project.model || !project.mesh || m_ids.empty())
        return;
    m_revision = project.revision;
    m_request = {};
    m_request.kind = kind;
    Vec3 low{INFINITY, INFINITY, INFINITY}, high{-INFINITY, -INFINITY, -INFINITY};
    std::vector<bool> selected;
    for (const auto &id : project.mesh->partIds)
        selected.push_back(std::find(m_ids.begin(), m_ids.end(), id) != m_ids.end());
    for (std::size_t i = 0; i < project.mesh->triangles.size(); ++i)
        if (selected.at(project.mesh->trianglePartIndices.at(i)))
            for (const auto index : project.mesh->triangles[i])
            {
                const auto p = project.mesh->vertices.at(index);
                low = {std::min(low.x, p.x), std::min(low.y, p.y), std::min(low.z, p.z)};
                high = {std::max(high.x, p.x), std::max(high.y, p.y), std::max(high.z, p.z)};
            }
    m_defaultCenter = {(low.x + high.x) / 2, (low.y + high.y) / 2, (low.z + high.z) / 2};
    m_request.centerMm = m_defaultCenter;
    fillControls();
    m_move->setVisible(kind == ModelTransformKind::Move);
    m_rotate->setVisible(kind == ModelTransformKind::Rotate);
    const Vec3 origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    m_active = m_viewport.beginTransformPreview(m_ids, origin, m_request);
    m_move->setEnabled(m_active);
    m_rotate->setEnabled(m_active);
    m_hint->setText(
        m_active
            ? QStringLiteral(
                  "已选择 %1 个部件。输入数值或拖动彩色轴柄预览；青色轮廓为原位置。确定后修改实体，Esc / 右键取消。")
                  .arg(m_ids.size())
            : QStringLiteral("无法启动预览，请重新选择部件。"));
    m_confirm->setEnabled(false);
    if (onChanged)
        onChanged();
}
void ModelTransformTool::fillControls()
{
    m_loading = true;
    const auto &p = m_request.offsetMm;
    const auto &c = m_request.centerMm;
    const std::array offsets{p.x, p.y, p.z}, centers{c.x, c.y, c.z};
    for (int i = 0; i < 3; ++i)
    {
        m_offset[i]->setValue(offsets[i]);
        m_center[i]->setValue(centers[i]);
    }
    m_axis->setCurrentIndex(static_cast<int>(m_request.axis));
    m_angle->setValue(m_request.angleDegrees);
    m_loading = false;
}
void ModelTransformTool::updatePreview()
{
    if (m_loading || !m_active)
        return;
    m_request.offsetMm = {m_offset[0]->value(), m_offset[1]->value(), m_offset[2]->value()};
    m_request.centerMm = {m_center[0]->value(), m_center[1]->value(), m_center[2]->value()};
    m_request.axis = static_cast<RotationAxis>(m_axis->currentIndex());
    m_request.angleDegrees = m_angle->value();
    m_viewport.updateTransformPreview(m_request);
    m_confirm->setEnabled(!isIdentityTransform(m_request));
}
void ModelTransformTool::confirm()
{
    if (!m_active || !m_service.isIdle())
        return;
    try
    {
        m_task = m_service.submitModelTransform(m_ids, m_request, m_revision);
        cancel();
        m_hint->setText(QStringLiteral("正在提交实体变换…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_request.kind == ModelTransformKind::Move ? "移动" : "旋转", error.what());
    }
}
void ModelTransformTool::cancel()
{
    if (!m_active)
        return;
    m_active = false;
    m_viewport.endTransformPreview();
    m_confirm->setEnabled(false);
    m_move->setEnabled(false);
    m_rotate->setEnabled(false);
    m_hint->setText(QStringLiteral("预览已结束。请选中部件后重新点击移动或旋转。"));
    if (onChanged)
        onChanged();
}
void ModelTransformTool::sync(const ProjectSnapshot &project, bool idle, bool visible)
{
    if (m_active && (project.revision != m_revision || !idle || !visible))
        cancel();
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
            m_task.clear();
            if (task.result.committed && onCommitted)
                onCommitted();
        }
    }
}
void ModelTransformTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
