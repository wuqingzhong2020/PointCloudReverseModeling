/*
 * 文件描述：选择开口面及壁厚/高度，后台预览槽钢或角钢，应用后替换源部件。
 * 输入改变使旧预览失效，应用走统一命令；取消、切换工具及工程变化清理临时显示。
 */
#include <algorithm>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

#include "geometry/Geometry.h"
#include "workbench/StructuralSteelTool.h"
#include "workbench/DockWorkspace.h"
#include <QPainter>
#include <QPainterPath>

namespace reverse::workbench
{
class StructuralSteelDiagram final : public QWidget
{
  public:
    explicit StructuralSteelDiagram(QWidget *parent) : QWidget(parent)
    {
        setMinimumHeight(150);
    }
    void setKind(StructuralSteelKind kind)
    {
        m_kind = kind;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const double left = width() * 0.25, top = 20, w = width() * 0.5, h = 98, d = 14;
        QPainterPath path;
        path.moveTo(left, top);
        path.lineTo(left + w, top);
        path.lineTo(left + w, top + h);
        if (m_kind == StructuralSteelKind::Channel)
        {
            path.lineTo(left, top + h);
            path.lineTo(left, top + h - d);
            path.lineTo(left + w - d, top + h - d);
        }
        else
            path.lineTo(left + w - d, top + h);
        path.lineTo(left + w - d, top + d);
        path.lineTo(left, top + d);
        path.closeSubpath();
        painter.setPen(QPen(QColor("#19b9e0"), 2));
        painter.setBrush(QColor("#45798b"));
        painter.drawPath(path);
        painter.setPen(QColor("#d5e9ef"));
        painter.drawLine(QPointF(left - 18, top), QPointF(left - 18, top + h));
        painter.drawText(QPointF(left - 38, top + h / 2), QStringLiteral("H"));
        painter.drawText(QPointF(left + w + 8, top + d), QStringLiteral("D"));
        painter.drawText(QRectF(0, top + h + 8, width(), 25), Qt::AlignCenter,
                         QStringLiteral("← 开口方向（所选侧面）"));
    }

  private:
    StructuralSteelKind m_kind{};
};
StructuralSteelTool::StructuralSteelTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *body = new QWidget(scroll);
    auto *layout = new QVBoxLayout(body);
    scroll->setWidget(body);
    outer->addWidget(scroll, 1);
    m_selected = new QLabel(this);
    m_selected->setWordWrap(true);
    m_selected->setObjectName(QStringLiteral("structuralSteel.selection"));
    layout->addWidget(m_selected);
    m_hint = new QLabel(this);
    m_hint->setObjectName(QStringLiteral("structuralSteel.hint"));
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    auto *clear = new QPushButton(QStringLiteral("重新选择开口面"), this);
    layout->addWidget(clear);
    connect(clear, &QPushButton::clicked, this,
            [this]
            {
                if (!m_service.isIdle() || !m_applyTask.empty())
                    return;
                invalidate();
                m_viewport.setSelectedParts({});
                selectionChanged({});
            });
    m_parameterGroup = new QGroupBox(QStringLiteral("型钢参数"), body);
    auto *parameters = new QVBoxLayout(m_parameterGroup);
    m_diagram = new StructuralSteelDiagram(m_parameterGroup);
    parameters->addWidget(m_diagram);
    auto *form = new QFormLayout;
    m_thickness = new QDoubleSpinBox(this);
    m_height = new QDoubleSpinBox(this);
    m_thickness->setObjectName(QStringLiteral("structuralSteel.thickness"));
    m_height->setObjectName(QStringLiteral("structuralSteel.height"));
    m_thickness->setRange(1, 50);
    m_thickness->setValue(10);
    m_height->setRange(1, 1000);
    m_height->setValue(100);
    for (auto *spin : {m_thickness, m_height})
    {
        spin->setDecimals(1);
        spin->setSuffix(QStringLiteral(" mm"));
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this]
                {
                    if (!m_loading)
                        invalidate();
                });
        connect(spin->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this,
                [this, spin]
                {
                    spin->interpretText();
                    preview();
                });
    }
    form->addRow(QStringLiteral("D：壁厚"), m_thickness);
    m_autoHeight = new QCheckBox(QStringLiteral("自动高度"), this);
    m_autoHeight->setObjectName(QStringLiteral("structuralSteel.autoHeight"));
    m_autoHeight->setChecked(true);
    form->addRow(QStringLiteral("H：高度"), m_autoHeight);
    form->addRow(QStringLiteral("自定义"), m_height);
    connect(m_autoHeight, &QCheckBox::toggled, this,
            [this](bool automatic)
            {
                m_height->setEnabled(!automatic && m_task.empty() && m_applyTask.empty());
                if (!m_loading)
                    invalidate();
            });
    parameters->addLayout(form);
    layout->addWidget(m_parameterGroup);
    m_description = new QLabel(this);
    m_description->setWordWrap(true);
    layout->addWidget(m_description);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), this);
    m_original->setEnabled(false);
    layout->addWidget(m_original);
    connect(m_original, &QCheckBox::toggled, this,
            [this](bool original)
            {
                if (!m_loading && m_preview)
                    showModel(original);
            });
    layout->addStretch();
    auto *buttons = new QHBoxLayout;
    m_previewButton = new QPushButton(QStringLiteral("预览"), this);
    m_previewButton->setObjectName(QStringLiteral("structuralSteel.preview"));
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    m_apply->setObjectName(QStringLiteral("structuralSteel.apply"));
    m_apply->setProperty("primary", true);
    m_apply->setEnabled(false);
    auto *exit = new QPushButton(QStringLiteral("退出"), this);
    for (auto *button : {m_previewButton, m_apply, exit})
    {
        button->setAutoDefault(false);
        buttons->addWidget(button);
    }
    outer->addLayout(buttons);
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
}
void StructuralSteelTool::begin(StructuralSteelKind kind)
{
    cancel();
    if (!m_service.isIdle() || !m_task.empty() || !m_applyTask.empty())
        return;
    const auto project = m_service.project();
    if (!project.model)
        return;
    m_revision = project.revision;
    m_request = {};
    m_request.kind = kind;
    m_loading = true;
    m_autoHeight->setChecked(true);
    m_loading = false;
    updateDescription();
    m_active = true;
    enableInputs(true);
    selectionChanged(m_viewport.selectedObjects());
    if (onChanged)
        onChanged();
}
void StructuralSteelTool::updateDescription()
{
    m_diagram->setKind(m_request.kind);
    m_description->setText(QStringLiteral("选择矩形实体的竖直开口侧面。\n自动高度参考下方支撑面；自定义高度固定底部。\n"
                                          "回车或预览查看效果，应用后替换原部件。"));
    m_description->setToolTip(
        QStringLiteral("自动寻找下方 50 mm 内覆盖开口底边的水平支撑面；无支撑时使用原实体高度。\n槽钢 H > 2D，角钢 H > "
                       "D，宽度 > D。底面允许 5° 内倾斜，超出请先旋转放平。"));
}
void StructuralSteelTool::enableInputs(bool enabled)
{
    for (QWidget *widget : std::initializer_list<QWidget *>{m_thickness, m_autoHeight})
        widget->setEnabled(enabled);
    m_height->setEnabled(enabled && !m_autoHeight->isChecked());
    m_previewButton->setEnabled(enabled && !m_request.partId.empty());
}
void StructuralSteelTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || m_loading || !m_task.empty() || !m_applyTask.empty() || m_previewShown)
        return;
    invalidate();
    m_selection = selections;
    m_request.partId.clear();
    if (selections.size() != 1 || selections.front().kind != ModelPickMode::Face)
    {
        m_selected->setText(QStringLiteral("请选择一个型钢开口侧面"));
        m_hint->setText(QStringLiteral("一次选择一个面，检查通过后才能应用。"));
        enableInputs(true);
        return;
    }
    m_request.partId = selections.front().partId;
    m_request.faceIndex = static_cast<std::uint32_t>(selections.front().index + 1);
    m_selected->setText(
        QStringLiteral("开口面：%1 · 面 %2").arg(QString::fromStdString(m_request.partId)).arg(m_request.faceIndex));
    preview();
}
void StructuralSteelTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
    {
        return;
    }
    const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    m_viewport.setModelPickingEnabled(true);
    auto parts = original || !m_preview ? makeModelDisplayParts(*project.mesh, *project.model, origin)
                                        : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin);
    if (!original && m_preview)
    {
        for (auto &part : parts)
        {
            if (part.id == m_request.partId)
            {
                part.colorSrgb = {0.30, 0.82, 0.40};
            }
        }
    }
    m_viewport.setModel(std::move(parts));
    m_viewport.setModelPickingEnabled(!m_previewShown);
    // 使用原始拓扑面/部件标识恢复源板的选择，新板保持绿色。
    m_viewport.setSelectedObjects(original ? m_selection : std::vector<ModelSelection>{});
}
void StructuralSteelTool::invalidate()
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
        m_hint->setText(QStringLiteral("参数或选择已改变，请预览后再应用。"));
    }
    if (onChanged)
    {
        onChanged();
    }
}
void StructuralSteelTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty() || m_request.partId.empty())
    {
        return;
    }
    invalidate();
    m_request.heightMm = m_height->value();
    m_request.thicknessMm = m_thickness->value();
    m_request.autoHeight = m_autoHeight->isChecked();
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_service.submitStructuralSteelPreview(m_request, m_revision);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在检查开口面并生成型钢预览…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_request.kind == StructuralSteelKind::Channel ? "设为槽钢" : "设为角钢",
                                  error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void StructuralSteelTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
    {
        return;
    }
    try
    {
        m_applyTask = m_service.submitStructuralSteel(m_previewRequest, m_revision);
        m_apply->setEnabled(false);
        m_original->setEnabled(false);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在替换型钢部件…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_request.kind == StructuralSteelKind::Channel ? "设为槽钢" : "设为角钢",
                                  error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void StructuralSteelTool::cancel()
{
    if (!m_task.empty())
    {
        (void)m_service.cancel(m_task);
        m_discard = true;
    }
    m_active = false;
    invalidate();
    enableInputs(false);
    m_hint->setText(QStringLiteral("已退出，未应用的预览已取消。"));
}
void StructuralSteelTool::sync(const ProjectSnapshot &project, bool idle)
{
    if (m_active && project.revision != m_revision && m_applyTask.empty())
    {
        cancel();
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
                enableInputs(true);
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
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
            cancel();
            if (task.result.committed && onCommitted)
            {
                onCommitted();
            }
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    if (m_preview && m_active)
    {
        m_apply->setEnabled(idle && m_applyTask.empty());
    }
}
void StructuralSteelTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
