/* 文件描述：分别拾取连续板 A 和打断板 B，预览几何后经统一命令原子提交。 */
#include "workbench/CrossPlateTool.h"
#include "workbench/DockWorkspace.h"
#include "geometry/Geometry.h"
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>

namespace reverse::workbench
{
namespace
{
class CrossDiagram final : public QWidget
{
  public:
    explicit CrossDiagram(QWidget *parent) : QWidget(parent)
    {
        setMinimumHeight(150);
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(width() / 2.0 - 115, 8);
        const auto plate = [&](QRectF r, QColor color, QString text)
        {
            p.setPen(QPen(color.lighter(), 2));
            p.setBrush(color.darker(180));
            p.drawRect(r);
            p.setPen(Qt::white);
            p.drawText(r, Qt::AlignCenter, text);
        };
        plate({94, 0, 42, 47}, QColor("#b730f7"), "B");
        plate({6, 47, 218, 35}, QColor("#0b99ff"), "A");
        plate({94, 82, 42, 47}, QColor("#b730f7"), "B");
        p.setPen(QColor("#d5e9ef"));
        p.drawText(QRectF(0, 130, 230, 20), Qt::AlignCenter, QStringLiteral("A 保持连续 · B 在交叉处打断"));
    }
};
} // namespace
CrossPlateTool::CrossPlateTool(CommandService &service, OccViewport &viewport, QWidget *parent)
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
    m_hint = new QLabel(body);
    m_hint->setObjectName("crossPlate.hint");
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    layout->addWidget(new CrossDiagram(body));
    m_a = new QPushButton(body);
    m_b = new QPushButton(body);
    m_a->setObjectName("crossPlate.continuous");
    m_b->setObjectName("crossPlate.broken");
    for (auto *button : {m_a, m_b})
    {
        button->setCheckable(true);
        layout->addWidget(button);
    }
    connect(m_a, &QPushButton::clicked, this, [this] { choose(true); });
    connect(m_b, &QPushButton::clicked, this, [this] { choose(false); });
    m_clear = new QPushButton(QStringLiteral("清空选板"), body);
    layout->addWidget(m_clear);
    connect(m_clear, &QPushButton::clicked, this,
            [this]
            {
                if (!m_service.isIdle())
                    return;
                invalidate();
                m_request.target.clear();
                m_request.parts.clear();
                choose(true);
            });
    m_adjustHeight = new QCheckBox(QStringLiteral("修改高度差"), body);
    m_adjustHeight->setObjectName("crossPlate.adjustHeight");
    m_adjustHeight->setChecked(true);
    layout->addWidget(m_adjustHeight);
    auto *form = new QFormLayout;
    m_height = new QDoubleSpinBox(body);
    m_height->setObjectName("crossPlate.height");
    m_height->setRange(1, 10);
    m_height->setDecimals(0);
    m_height->setValue(10);
    m_height->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("H：高度差"), m_height);
    layout->addLayout(form);
    auto *description = new QLabel(
        QStringLiteral("A 为连续板，B 为打断板。\n关闭高度差时保留原有高度。\n蓝色为 A，紫色为 B；预览后应用。"), body);
    description->setWordWrap(true);
    layout->addWidget(description);
    connect(m_adjustHeight, &QCheckBox::toggled, this,
            [this]
            {
                if (!m_loading)
                {
                    invalidate();
                    enableInputs(m_service.isIdle());
                }
            });
    connect(m_height, &QDoubleSpinBox::valueChanged, this,
            [this]
            {
                if (!m_loading)
                    invalidate();
            });
    connect(m_height->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this,
            [this]
            {
                m_height->interpretText();
                preview();
            });
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), body);
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
    m_previewButton->setObjectName("crossPlate.preview");
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    m_apply->setObjectName("crossPlate.apply");
    m_apply->setProperty("primary", true);
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
    updateSelection();
    m_apply->setEnabled(false);
}
void CrossPlateTool::begin()
{
    cancel();
    if (!m_service.isIdle() || !m_task.empty() || !m_applyTask.empty())
        return;
    const auto project = m_service.project();
    if (!project.model)
        return;
    const auto selection = m_viewport.selectedObjects();
    m_revision = project.revision;
    m_request = {};
    m_request.operation = "cross";
    m_active = true;
    m_continuous = true;
    enableInputs(true);
    updateSelection();
    m_hint->setText(QStringLiteral("请选择连续板 A，再选择打断板 B。"));
    selectionChanged(selection);
    if (onChanged)
        onChanged();
}
void CrossPlateTool::choose(bool continuous)
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty())
        return;
    invalidate();
    m_continuous = continuous;
    const QScopedValueRollback<bool> guard(m_loading, true);
    m_viewport.setSelectedParts({});
    showModel(true);
    updateSelection();
    m_hint->setText(continuous ? QStringLiteral("请选择连续板 A。") : QStringLiteral("请选择打断板 B。"));
}
void CrossPlateTool::updateSelection()
{
    m_a->setText(m_request.target.empty() ? QStringLiteral("A：请选择连续板")
                                          : QStringLiteral("A：%1").arg(QString::fromStdString(m_request.target)));
    m_b->setText(m_request.parts.empty()
                     ? QStringLiteral("B：请选择打断板")
                     : QStringLiteral("B：%1").arg(QString::fromStdString(m_request.parts.front())));
    m_a->setChecked(m_continuous);
    m_b->setChecked(!m_continuous);
    m_previewButton->setEnabled(m_active && m_service.isIdle() && !m_request.target.empty() &&
                                !m_request.parts.empty());
}
void CrossPlateTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || m_loading || !m_task.empty() || !m_applyTask.empty() || m_previewShown || selections.empty())
        return;
    if (selections.size() != 1)
    {
        m_hint->setText(QStringLiteral("请一次只选择一块板。"));
        return;
    }
    const auto id = selections.front().partId;
    if (id.empty())
        return;
    if ((m_continuous && !m_request.parts.empty() && id == m_request.parts.front()) ||
        (!m_continuous && id == m_request.target))
    {
        m_hint->setText(QStringLiteral("A 和 B 必须是不同板件。"));
        return;
    }
    invalidate();
    if (m_continuous)
    {
        m_request.target = id;
        m_continuous = false;
    }
    else
        m_request.parts = {id};
    const QScopedValueRollback<bool> guard(m_loading, true);
    showModel(true);
    updateSelection();
    m_hint->setText(m_request.parts.empty() ? QStringLiteral("已选择 A，请选择打断板 B。")
                                            : QStringLiteral("已选择 A、B，请预览检查结果。"));
}
void CrossPlateTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
        return;
    const QScopedValueRollback<bool> guard(m_loading, true);
    const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    auto parts = original || !m_preview ? makeModelDisplayParts(*project.mesh, *project.model, origin)
                                        : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin);
    for (auto &part : parts)
    {
        if (m_active && part.id == m_request.target)
            part.colorSrgb = {0.04, 0.60, 1.0};
        if (m_active && !m_request.parts.empty() &&
            (part.id == m_request.parts.front() ||
             (!original && part.id.starts_with(m_request.parts.front() + "-split-"))))
            part.colorSrgb = {0.72, 0.19, 0.97};
    }
    m_viewport.setModel(std::move(parts));
    m_viewport.setSelectedParts({});
    m_viewport.setModelPickingEnabled(!m_previewShown);
}
void CrossPlateTool::enableInputs(bool enabled)
{
    for (QWidget *widget : std::initializer_list<QWidget *>{m_a, m_b, m_clear, m_adjustHeight})
        widget->setEnabled(enabled);
    m_height->setEnabled(enabled && m_adjustHeight->isChecked());
    m_previewButton->setEnabled(enabled && !m_request.target.empty() && !m_request.parts.empty());
}
void CrossPlateTool::invalidate()
{
    const QScopedValueRollback<bool> guard(m_loading, true);
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
        m_hint->setText(QStringLiteral("参数已改变，请重新预览。"));
    if (onChanged)
        onChanged();
}
void CrossPlateTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty() || m_request.target.empty() ||
        m_request.parts.empty())
        return;
    invalidate();
    m_request.heightOffset = m_adjustHeight->isChecked() ? m_height->value() : 0;
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_service.submitPlateEditPreview(m_request, m_revision);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在计算交叉板预览…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
    }
    if (onChanged)
        onChanged();
}
void CrossPlateTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
        return;
    try
    {
        m_applyTask = m_service.submitPlateEdit(m_previewRequest, m_revision);
        m_apply->setEnabled(false);
        m_original->setEnabled(false);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在应用交叉板优化…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
    }
    if (onChanged)
        onChanged();
}
void CrossPlateTool::cancel()
{
    if (!m_task.empty())
    {
        (void)m_service.cancel(m_task);
        m_discard = true;
    }
    const bool restore = m_active;
    m_active = false;
    invalidate();
    if (restore)
        showModel(true);
    enableInputs(false);
}
void CrossPlateTool::sync(const ProjectSnapshot &project, bool idle)
{
    if (m_active && project.revision != m_revision && m_applyTask.empty())
        cancel();
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            auto result = m_service.takeEdgePreview(m_task);
            m_task.clear();
            if (m_active && !m_discard && project.revision == m_revision)
            {
                enableInputs(true);
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
                if (task.state == TaskState::Succeeded && result)
                {
                    m_preview = std::move(result);
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
                onCommitted();
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    if (m_preview && m_active)
        m_apply->setEnabled(idle && m_applyTask.empty());
}
void CrossPlateTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
