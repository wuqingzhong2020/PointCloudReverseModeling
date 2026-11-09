/* 文件描述：边缘参数分别保留；预览和应用仅作用于当前边缘，完成后可继续编辑另一边。 */
#include "workbench/EndCapTool.h"
#include "workbench/DockWorkspace.h"
#include <QDebug>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QPainter>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QShortcut>
#include <QVBoxLayout>
#include <algorithm>

namespace reverse::workbench
{
namespace
{
class CapDiagram final : public QWidget
{
  public:
    explicit CapDiagram(QWidget *parent) : QWidget(parent)
    {
        setMinimumHeight(190);
        setAccessibleName(QStringLiteral("封板边缘截面示意"));
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        const bool convex = property("convex").toBool();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal scale = std::min((width() - 2) / 255.0, (height() - 2) / 190.0);
        p.translate((width() - 255 * scale) / 2, (height() - 190 * scale) / 2);
        p.scale(scale, scale);
        // Match the reference's section view: black background, grey cap,
        // white wing face and grey thickness face, with inward dimension arrows.
        p.fillRect(QRectF(0, 0, 255, 190), Qt::black);
        const qreal capEnd = convex ? 222 : 177;
        const qreal wingOuter = convex ? 174 : 207;
        p.setPen(QPen(Qt::black, 1.5));
        p.setBrush(QColor("#f1f3f4"));
        p.drawRect(QRectF(0, 80, wingOuter - 46, 110));
        p.setBrush(QColor("#d9d9d9"));
        p.drawRect(QRectF(wingOuter - 46, 80, 46, 110));
        p.drawRect(QRectF(0, 33, capEnd, 47));
        const qreal left = std::min(capEnd, wingOuter), right = std::max(capEnd, wingOuter);
        const qreal dimensionY = convex ? 112 : 56;
        const auto arrow = [&p, dimensionY](qreal tip, qreal tail)
        {
            p.setPen(QPen(QColor("#f4f4f4"), 3));
            p.drawLine(QPointF(tail, dimensionY), QPointF(tip, dimensionY));
            p.setPen(QPen(QColor("#303030"), 1));
            p.drawLine(QPointF(tail, dimensionY), QPointF(tip, dimensionY));
            const qreal back = tip + (tail < tip ? -8 : 8);
            p.setBrush(QColor("#303030"));
            p.drawPolygon(
                QPolygonF{QPointF(tip, dimensionY), QPointF(back, dimensionY - 2), QPointF(back, dimensionY + 2)});
        };
        arrow(left, left - 30);
        arrow(right, std::min(253.0, right + 30));
        const QRectF label((left + right) / 2 - 8, dimensionY - 9, 16, 18);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#eeeeee"));
        p.drawRoundedRect(label, 4, 4);
        p.setPen(Qt::black);
        auto font = p.font();
        font.setPixelSize(15);
        p.setFont(font);
        p.drawText(label, Qt::AlignCenter, QStringLiteral("D"));
        p.setPen(QPen(QColor("#008da8"), 1));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(0, 0, 255, 190), 3, 3);
    }
};
} // namespace
EndCapTool::EndCapTool(CommandService &service, OccViewport &viewport, QWidget *parent)
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
    m_hint->setObjectName("endCap.hint");
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    m_pick = new QPushButton(QStringLiteral("请选择封板"), body);
    m_pick->setObjectName("endCap.select");
    layout->addWidget(m_pick);
    connect(m_pick, &QPushButton::clicked, this,
            [this]
            {
                if (!m_active || !m_service.isIdle())
                    return;
                invalidate();
                m_info.reset();
                m_part.clear();
                m_drafts = {};
                m_edge = 1;
                m_viewport.setEndCapEdges({}, 0);
                m_pick->setText(QStringLiteral("请选择封板"));
                showModel(true);
                loadDraft();
                m_hint->setText(QStringLiteral("请在模型或模型树中选择梁端的封板。"));
            });
    auto *edgeRow = new QHBoxLayout;
    auto *modeRow = new QHBoxLayout;
    const auto toggleStyle =
        QStringLiteral("QPushButton:checked { background:#075675; color:#ffffff; border:2px solid #00aeef; }"
                       "QPushButton:disabled { color:#607987; border:1px solid #24495b; }");
    for (int i = 0; i < 2; ++i)
    {
        m_edges[i] = new QPushButton(QStringLiteral("边缘 %1").arg(i + 1), body);
        m_edges[i]->setObjectName(QStringLiteral("endCap.edge%1").arg(i + 1));
        m_edges[i]->setCheckable(true);
        m_edges[i]->setStyleSheet(toggleStyle);
        edgeRow->addWidget(m_edges[i]);
        connect(m_edges[i], &QPushButton::clicked, this, [this, i] { selectEdge(i + 1); });
        m_modes[i] = new QPushButton(i ? QStringLiteral("凸边") : QStringLiteral("错边"), body);
        m_modes[i]->setCheckable(true);
        m_modes[i]->setStyleSheet(toggleStyle);
        modeRow->addWidget(m_modes[i]);
        connect(m_modes[i], &QPushButton::clicked, this,
                [this, i]
                {
                    if (m_loading || !m_service.isIdle())
                        return;
                    saveDraft();
                    m_drafts[m_edge - 1].convex = i != 0;
                    invalidate();
                    loadDraft();
                });
    }
    layout->addLayout(edgeRow);
    layout->addWidget(new QLabel(QStringLiteral("边缘设置"), body));
    layout->addLayout(modeRow);
    m_diagram = new CapDiagram(body);
    layout->addWidget(m_diagram);
    auto *form = new QFormLayout;
    m_distanceMode = new QComboBox(body);
    m_distanceMode->addItems({QStringLiteral("自动设置"), QStringLiteral("自定义")});
    m_distanceMode->setObjectName("endCap.distanceMode");
    m_modeLabel = new QLabel(QStringLiteral("距离方式"), body);
    form->addRow(m_modeLabel, m_distanceMode);
    m_distance = new QDoubleSpinBox(body);
    m_distance->setObjectName("endCap.distance");
    m_distance->setDecimals(2);
    m_distance->setSuffix(QStringLiteral(" mm"));
    form->addRow(QStringLiteral("D：距离"), m_distance);
    layout->addLayout(form);
    m_distanceInfo = new QLabel(body);
    m_distanceInfo->setWordWrap(true);
    layout->addWidget(m_distanceInfo);
    connect(m_distanceMode, &QComboBox::currentIndexChanged, this,
            [this](int index)
            {
                if (m_loading)
                    return;
                saveDraft();
                m_drafts[m_edge - 1].automatic = index == 0;
                invalidate();
                loadDraft();
            });
    connect(m_distance, &QDoubleSpinBox::valueChanged, this,
            [this]
            {
                if (!m_loading)
                {
                    saveDraft();
                    invalidate();
                }
            });
    auto *description = new QLabel(
        QStringLiteral("每次预览、应用只处理当前边缘。\n点击模型上的边缘标记也可切换；两侧参数分别保留。"), body);
    description->setWordWrap(true);
    layout->addWidget(description);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), body);
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
    m_previewButton->setObjectName("endCap.preview");
    m_apply = new QPushButton(QStringLiteral("应用当前边缘"), this);
    m_apply->setObjectName("endCap.apply");
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
    m_viewport.onEndCapEdgeSelected = [this](int edge) { selectEdge(edge); };
    loadDraft();
}
void EndCapTool::begin()
{
    qInfo("cap.ui.begin");
    cancel();
    if (!m_service.isIdle() || !m_task.empty() || !m_inspectTask.empty() || !m_applyTask.empty())
        return;
    const auto p = m_service.project();
    if (!p.model)
        return;
    const auto selection = m_viewport.selectedObjects();
    m_revision = p.revision;
    m_active = true;
    m_part.clear();
    m_info.reset();
    m_drafts = {};
    m_edge = 1;
    m_pick->setText(QStringLiteral("请选择封板"));
    m_hint->setText(QStringLiteral("请选择梁端的封板。"));
    loadDraft();
    selectionChanged(selection);
    if (onChanged)
        onChanged();
}
void EndCapTool::saveDraft()
{
    if (m_loading)
        return;
    auto &d = m_drafts[m_edge - 1];
    if (d.convex)
        d.outset = m_distance->value();
    else if (!d.automatic)
        d.inset = m_distance->value();
}
void EndCapTool::loadDraft()
{
    const QScopedValueRollback<bool> guard(m_loading, true);
    const auto &d = m_drafts[m_edge - 1];
    for (int i = 0; i < 2; ++i)
    {
        m_edges[i]->setChecked(m_edge == i + 1);
        m_modes[i]->setChecked(d.convex == (i == 1));
    }
    m_distanceMode->setCurrentIndex(d.automatic ? 0 : 1);
    m_distanceMode->setVisible(!d.convex);
    m_modeLabel->setVisible(!d.convex);
    m_distance->setRange(1, d.convex ? 200 : 100);
    m_distance->setValue(d.convex                ? d.outset
                         : d.automatic && m_info ? m_info->edges[m_edge - 1].automaticDistance
                                                 : d.inset);
    m_distanceInfo->setText(d.convex      ? QStringLiteral("从当前侧翼板外表面向外伸出 D。")
                            : d.automatic ? QStringLiteral("从翼板外表面向内收 D；自动 D = 当前侧翼板厚度 × 75%。")
                                          : QStringLiteral("从当前侧翼板外表面向内收 D。"));
    m_diagram->setProperty("convex", d.convex);
    m_diagram->setToolTip(d.convex ? QStringLiteral("凸边：封板端部超出翼板外表面 D。")
                                   : QStringLiteral("错边：封板端部相对翼板外表面内收 D。"));
    m_diagram->update();
    enableInputs();
}
void EndCapTool::selectEdge(int edge)
{
    if (!m_active || !m_info || !m_service.isIdle() || edge < 1 || edge > 2)
        return;
    saveDraft();
    invalidate();
    m_edge = edge;
    loadDraft();
    showModel(true);
}
void EndCapTool::selectionChanged(const std::vector<ModelSelection> &selection)
{
    if (!m_active || m_loading || m_previewShown || !m_service.isIdle() || !m_inspectTask.empty() || !m_task.empty() ||
        !m_applyTask.empty() || selection.empty())
        return;
    if (selection.size() != 1)
    {
        m_hint->setText(QStringLiteral("请一次只选择一块封板。"));
        return;
    }
    const auto id = selection.front().partId;
    // 同一模型版本下保留失败结果，避免重复选择通知再次提交识别。
    // “点击重选”会清空 m_part；模型版本变化时会退出本次操作。
    if (id.empty() || id == m_part)
        return;
    invalidate();
    m_part = id;
    qInfo("cap.ui.select part=%s revision=%llu", m_part.c_str(), static_cast<unsigned long long>(m_revision));
    m_info.reset();
    m_drafts = {};
    m_edge = 1;
    m_pick->setText(QStringLiteral("封板：%1（点击重选）").arg(QString::fromStdString(id)));
    showModel(true);
    inspect();
}
void EndCapTool::inspect()
{
    m_info.reset();
    markers(false);
    try
    {
        m_discard = false;
        m_inspectTask = m_service.submitEndCapInspection(m_part, m_revision);
        m_hint->setText(QStringLiteral("正在识别封板两侧边缘…"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
    }
    enableInputs();
}
void EndCapTool::markers(bool enabled)
{
    std::vector<Vec3> points;
    const auto p = m_service.project();
    if (m_active && m_info && p.model)
    {
        const auto origin = p.cloud ? p.cloud->originMm : p.model->frame.originMm;
        for (const auto &edge : m_info->edges)
            points.push_back({edge.position.x - origin.x, edge.position.y - origin.y, edge.position.z - origin.z});
    }
    m_viewport.setEndCapEdges(points, m_edge, enabled);
}
void EndCapTool::showModel(bool original)
{
    const auto p = m_service.project();
    if (!p.model || !p.mesh)
        return;
    const QScopedValueRollback<bool> guard(m_loading, true);
    qInfo("cap.ui.display.begin original=%d part=%s edge=%d revision=%llu", original, m_part.c_str(), m_edge,
          static_cast<unsigned long long>(p.revision));
    const auto origin = p.cloud ? p.cloud->originMm : p.model->frame.originMm;
    auto parts = original || !m_preview ? makeModelDisplayParts(*p.mesh, *p.model, origin)
                                        : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin);
    for (auto &part : parts)
    {
        if (m_active && part.id == m_part)
            part.colorSrgb = {0.05, .65, 1};
        else if (m_active && m_info && part.id == m_info->edges[m_edge - 1].wing)
            part.colorSrgb = {.75, .35, .9};
    }
    m_viewport.setModel(std::move(parts));
    m_viewport.setSelectedParts({});
    m_viewport.setModelPickingEnabled(!m_previewShown);
    markers(m_service.isIdle());
    qInfo("cap.ui.display.end");
}
void EndCapTool::enableInputs()
{
    const bool idle = m_active && m_service.isIdle() && m_task.empty() && m_applyTask.empty() && m_inspectTask.empty();
    m_pick->setEnabled(idle);
    for (auto *w : {m_edges[0], m_edges[1], m_modes[0], m_modes[1], m_previewButton})
        w->setEnabled(idle && m_info.has_value());
    const auto &d = m_drafts[m_edge - 1];
    m_distanceMode->setEnabled(idle && m_info.has_value());
    m_distance->setEnabled(idle && m_info && (d.convex || !d.automatic));
    m_apply->setEnabled(idle && m_preview != nullptr);
    m_original->setEnabled(idle && m_preview != nullptr);
    markers(idle);
}
void EndCapTool::invalidate()
{
    const QScopedValueRollback<bool> guard(m_loading, true);
    const bool restore = m_previewShown;
    m_previewShown = false;
    m_preview.reset();
    m_apply->setEnabled(false);
    m_original->setChecked(false);
    m_original->setEnabled(false);
    if (restore)
        showModel(true);
    if (m_active)
        m_hint->setText(QStringLiteral("参数已改变，请重新预览当前边缘。"));
    if (onChanged)
        onChanged();
}
void EndCapTool::preview()
{
    if (!m_active || !m_info || !m_service.isIdle())
        return;
    saveDraft();
    invalidate();
    const auto &d = m_drafts[m_edge - 1];
    m_previewRequest = {};
    m_previewRequest.operation = "cap";
    m_previewRequest.parts = {m_part};
    m_previewRequest.capEdge = m_edge;
    m_previewRequest.capConvex = d.convex;
    // Freeze the resolved distance for deterministic apply and recording.
    m_previewRequest.capAutomatic = false;
    m_previewRequest.capDistance = d.convex      ? d.outset
                                   : d.automatic ? m_info->edges[m_edge - 1].automaticDistance
                                                 : d.inset;
    try
    {
        qInfo("cap.ui.preview part=%s edge=%d convex=%d distance=%.6f revision=%llu", m_part.c_str(), m_edge,
              d.convex, m_previewRequest.capDistance, static_cast<unsigned long long>(m_revision));
        m_discard = false;
        m_task = m_service.submitPlateEditPreview(m_previewRequest, m_revision);
        m_hint->setText(QStringLiteral("正在预览当前边缘及连接处…"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
    }
    enableInputs();
    if (onChanged)
        onChanged();
}
void EndCapTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
        return;
    try
    {
        qInfo("cap.ui.apply part=%s edge=%d revision=%llu", m_part.c_str(), m_edge,
              static_cast<unsigned long long>(m_revision));
        m_applyTask = m_service.submitPlateEdit(m_previewRequest, m_revision);
        m_hint->setText(QStringLiteral("正在应用当前边缘…"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
    }
    enableInputs();
    if (onChanged)
        onChanged();
}
void EndCapTool::cancel()
{
    for (const auto &task : {m_inspectTask, m_task})
        if (!task.empty())
        {
            (void)m_service.cancel(task);
            m_discard = true;
        }
    const bool restore = m_active;
    m_active = false;
    invalidate();
    m_info.reset();
    m_viewport.setEndCapEdges({}, 0);
    if (restore)
        showModel(true);
    enableInputs();
}
void EndCapTool::sync(const ProjectSnapshot &project, bool)
{
    if (m_active && project.revision != m_revision && m_applyTask.empty())
        cancel();
    if (!m_inspectTask.empty())
    {
        const auto task = m_service.task(m_inspectTask);
        if (task.finished())
        {
            m_inspectTask.clear();
            if (m_active && !m_discard && project.revision == m_revision)
            {
                m_info = task.endCap;
                loadDraft();
                showModel(true);
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
            }
        }
    }
    if (!m_task.empty())
    {
        const auto task = m_service.task(m_task);
        if (task.finished())
        {
            auto result = m_service.takeEdgePreview(m_task);
            m_task.clear();
            if (m_active && !m_discard && project.revision == m_revision)
            {
                m_preview = std::move(result);
                m_previewShown = m_preview != nullptr;
                showModel(!m_previewShown);
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
            }
        }
    }
    if (!m_applyTask.empty())
    {
        const auto task = m_service.task(m_applyTask);
        if (task.finished())
        {
            m_applyTask.clear();
            if (task.result.committed)
            {
                m_revision = project.revision;
                m_preview.reset();
                m_previewShown = false;
                {
                    const QScopedValueRollback<bool> guard(m_loading, true);
                    m_original->setChecked(false);
                }
                if (onCommitted)
                {
                    const QScopedValueRollback<bool> guard(m_commitRefresh, true);
                    onCommitted();
                }
                if (m_active)
                {
                    showModel(true);
                    inspect();
                }
            }
            else
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    enableInputs();
}
void EndCapTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
