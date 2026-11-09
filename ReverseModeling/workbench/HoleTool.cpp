/* 文件描述：按明确底边和参考点开孔；Enter 预览、应用才提交命令及历史。 */
#include "workbench/HoleTool.h"
#include "workbench/DockWorkspace.h"
#include "geometry/Geometry.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QTabBar>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <Standard_Failure.hxx>

namespace reverse::workbench
{
HoleTool::HoleTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    m_loading = true;
    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(8);
    const auto selector = [&](const QString &name, const QStringList &items)
    {
        auto *tabs = new QTabBar(this);
        tabs->setObjectName(name);
        tabs->setProperty("holeSelector", true);
        tabs->setExpanding(true);
        tabs->setDrawBase(false);
        tabs->setUsesScrollButtons(false);
        tabs->setFocusPolicy(Qt::StrongFocus);
        for (const auto &item : items)
            tabs->addTab(item);
        return tabs;
    };
    m_kind = selector(QStringLiteral("hole.kind"), {QStringLiteral("过焊孔"), QStringLiteral("底边孔")});
    layout->addWidget(m_kind);
    m_selection = new QLabel(this);
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);
    auto *shapeGroup = new QGroupBox(QStringLiteral("形状"), this);
    auto *shapeLayout = new QVBoxLayout(shapeGroup);
    shapeLayout->setContentsMargins(0, 0, 0, 0);
    m_shape = selector(QStringLiteral("hole.shape"), {QStringLiteral("圆形"), QStringLiteral("三角形")});
    shapeLayout->addWidget(m_shape);
    auto *form = new QFormLayout;
    const auto combo = [&](const QString &name, const QStringList &items)
    {
        auto *value = new QComboBox(this);
        value->setObjectName(name);
        value->addItems(items);
        return value;
    };
    m_edge = combo(QStringLiteral("hole.edge"), {});
    const auto number = [&](const QString &name, double value)
    {
        auto *spin = new QDoubleSpinBox(this);
        spin->setObjectName(name);
        spin->setRange(0.001, 1e6);
        spin->setDecimals(3);
        spin->setValue(value);
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
        return spin;
    };
    m_size = number(QStringLiteral("hole.size"), 20);
    m_width = number(QStringLiteral("hole.width"), 30);
    m_height = number(QStringLiteral("hole.height"), 15);
    m_offset = number(QStringLiteral("hole.offset"), 0.001);
    m_offset->setRange(-1e6, 1e6);
    m_offset->setValue(0);
    m_sizeLabel = new QLabel(QStringLiteral("半径 R"), this);
    m_widthLabel = new QLabel(QStringLiteral("宽度"), this);
    m_heightLabel = new QLabel(QStringLiteral("高度"), this);
    m_referenceLabel = new QLabel(QStringLiteral("参考点"), this);
    m_offsetLabel = new QLabel(QStringLiteral("偏移"), this);
    form->addRow(m_sizeLabel, m_size);
    form->addRow(m_widthLabel, m_width);
    form->addRow(m_heightLabel, m_height);
    shapeLayout->addLayout(form);
    layout->addWidget(shapeGroup);
    auto *positionGroup = new QGroupBox(QStringLiteral("位置"), this);
    auto *positionLayout = new QVBoxLayout(positionGroup);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    auto *positionForm = new QFormLayout;
    positionForm->addRow(QStringLiteral("底边"), m_edge);
    m_reference = combo(QStringLiteral("hole.reference"),
                        {QStringLiteral("底边端点 1"), QStringLiteral("底边中点"), QStringLiteral("底边端点 2")});
    m_reference->setCurrentIndex(1);
    positionForm->addRow(m_referenceLabel, m_reference);
    positionForm->addRow(m_offsetLabel, m_offset);
    positionLayout->addLayout(positionForm);
    auto *positions = new QHBoxLayout;
    m_first = new QCheckBox(QStringLiteral("过焊孔 1"), this);
    m_first->setChecked(true);
    m_second = new QCheckBox(QStringLiteral("过焊孔 2"), this);
    m_second->setChecked(true);
    positions->addWidget(m_first);
    positions->addWidget(m_second);
    positionLayout->addLayout(positions);
    layout->addWidget(positionGroup);
    auto *help = new QLabel(QStringLiteral("绿色半透明体显示孔位和尺寸，参数修改后同步更新；回车或“预览”查看开孔结果，“应用”才修改实体。"), this);
    help->setToolTip(QStringLiteral("选择一个等厚实体板件，可切换底边。过焊孔切除底角，底边孔沿底边定位。\n"
                                    "端点偏移向内为正，中点偏移朝 2 为正。孔贯穿板厚，应用后记录命令。"));
    m_offset->setToolTip(QStringLiteral("端点偏移向内为正；中点偏移朝端点 2 为正，单位 mm。"));
    help->setWordWrap(true);
    layout->addWidget(help);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), this);
    m_original->setEnabled(false);
    layout->addWidget(m_original);
    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    layout->addWidget(m_hint);
    layout->addStretch();
    auto *buttons = new QHBoxLayout;
    m_previewButton = new QPushButton(QStringLiteral("预览"), this);
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    m_apply->setProperty("primary", true);
    auto *exit = new QPushButton(QStringLiteral("退出"), this);
    for (auto *button : {m_previewButton, m_apply, exit})
    {
        button->setAutoDefault(false);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    connect(m_kind, &QTabBar::currentChanged, this,
            [this]
            {
                if (m_loading)
                    return;
                m_loading = true;
                m_shape->setTabText(1, m_kind->currentIndex() == 0 ? QStringLiteral("三角形") : QStringLiteral("矩形"));
                m_shape->setCurrentIndex(0);
                m_loading = false;
                updateFields();
                invalidate();
            });
    connect(m_shape, &QTabBar::currentChanged, this,
            [this]
            {
                if (!m_loading)
                {
                    updateFields();
                    invalidate();
                }
            });
    connect(m_edge, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (!m_loading)
                {
                    invalidate();
                }
            });
    connect(m_reference, &QComboBox::currentIndexChanged, this,
            [this]
            {
                if (!m_loading)
                    invalidate();
            });
    for (auto *check : {m_first, m_second})
        connect(check, &QCheckBox::toggled, this,
                [this]
                {
                    if (!m_loading)
                        invalidate();
                });
    connect(m_original, &QCheckBox::toggled, this,
            [this](bool original)
            {
                if (!m_loading && m_preview)
                    showModel(original);
            });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    m_apply->setEnabled(false);
    m_loading = false;
}

void HoleTool::updateFields()
{
    const bool weld = m_kind->currentIndex() == 0, rectangle = !weld && m_shape->currentIndex() == 1;
    m_sizeLabel->setText(weld && m_shape->currentIndex() == 1 ? QStringLiteral("直角边长") : QStringLiteral("半径 R"));
    m_sizeLabel->setVisible(!rectangle);
    m_size->setVisible(!rectangle);
    for (QWidget *w : std::initializer_list<QWidget *>{m_widthLabel, m_width, m_heightLabel, m_height})
        w->setVisible(rectangle);
    for (QWidget *w : std::initializer_list<QWidget *>{m_referenceLabel, m_reference, m_offsetLabel, m_offset})
        w->setVisible(!weld);
    m_first->setVisible(weld);
    m_second->setVisible(weld);
}
void HoleTool::begin(PlateHoleKind kind)
{
    cancel();
    if (!m_service.isIdle() || !m_task.empty() || !m_inspectTask.empty() || !m_applyTask.empty())
        return;
    const auto project = m_service.project();
    if (!project.model)
        return;
    m_revision = project.revision;
    m_request = {};
    m_bases.clear();
    m_selections.clear();
    m_active = true;
    m_loading = true;
    m_kind->setCurrentIndex(kind == PlateHoleKind::Weld ? 0 : 1);
    m_shape->setTabText(1, kind == PlateHoleKind::Weld ? QStringLiteral("三角形") : QStringLiteral("矩形"));
    m_shape->setCurrentIndex(0);
    m_size->setValue(kind == PlateHoleKind::Weld ? 20 : 15);
    m_edge->clear();
    m_loading = false;
    updateFields();
    setInputsEnabled(true);
    m_apply->setEnabled(false);
    m_selection->setText(QStringLiteral("请选择一个实体板件"));
    m_hint->setText(QStringLiteral("请选择板件，然后核对底边。"));
    selectionChanged(m_viewport.selectedObjects());
    if (onChanged)
        onChanged();
}
void HoleTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || !m_task.empty() || !m_inspectTask.empty() || !m_applyTask.empty() || m_previewShown)
        return;
    if (selections == m_selections)
        return;
    m_selections = selections;
    m_request.partId.clear();
    m_bases.clear();
    m_loading = true;
    m_edge->clear();
    m_loading = false;
    invalidate();
    m_viewport.setHoleGuide({});
    if (selections.size() != 1 || selections.front().kind != ModelPickMode::Body)
    {
        m_selection->setText(QStringLiteral("请单选一个实体板件"));
        setInputsEnabled(true);
        return;
    }
    m_request.partId = selections.front().partId;
    m_selection->setText(QStringLiteral("板件：%1").arg(QString::fromStdString(m_request.partId)));
    try
    {
        m_discard = false;
        m_inspectTask = m_service.submitHoleInspection(m_request.partId, m_revision);
        setInputsEnabled(false);
        m_hint->setText(QStringLiteral("正在识别板件底边…"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
        m_service.reportUiFailure("添加孔", e.what());
    }
    if (onChanged)
        onChanged();
}
PlateHole HoleTool::currentRequest() const
{
    auto request = m_request;
    request.bottomEdge = m_bases.at(m_edge->currentIndex()).edge;
    request.kind = m_kind->currentIndex() == 0 ? PlateHoleKind::Weld : PlateHoleKind::Bottom;
    request.shape = m_shape->currentIndex() == 0          ? PlateHoleShape::Circle
                    : request.kind == PlateHoleKind::Weld ? PlateHoleShape::Triangle
                                                         : PlateHoleShape::Rectangle;
    request.sizeMm = m_size->value();
    request.widthMm = m_width->value();
    request.heightMm = m_height->value();
    request.reference = static_cast<HoleReference>(m_reference->currentIndex());
    request.offsetMm = m_offset->value();
    request.first = m_first->isChecked();
    request.second = m_second->isChecked();
    return request;
}

void HoleTool::updateGuide()
{
    if (!m_active || m_edge->currentIndex() < 0 || m_edge->currentIndex() >= static_cast<int>(m_bases.size()))
    {
        m_viewport.clearHolePreview();
        return;
    }
    const auto &base = m_bases[m_edge->currentIndex()];
    const auto project = m_service.project();
    if (!project.model)
    {
        m_viewport.clearHolePreview();
        return;
    }
    const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    const auto a = base.firstMm, b = base.secondMm;
    m_viewport.setHoleGuide({{a.x - origin.x, a.y - origin.y, a.z - origin.z},
                             {(a.x + b.x) / 2 - origin.x, (a.y + b.y) / 2 - origin.y, (a.z + b.z) / 2 - origin.z},
                             {b.x - origin.x, b.y - origin.y, b.z - origin.z}});
    if (m_original->isChecked())
    {
        m_viewport.clearHolePreview();
        return;
    }
    try
    {
        m_viewport.setHolePreview(base, currentRequest(), origin);
    }
    catch (const Standard_Failure &error)
    {
        m_hint->setText(QStringLiteral("孔位预览失败：%1").arg(QString::fromUtf8(error.what())));
        m_service.reportUiFailure("孔位预览", error.what());
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure("添加孔", error.what());
    }
}
void HoleTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
        return;
    const Vec3 origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    if (original || !m_preview)
    {
        m_viewport.setModel(makeModelDisplayParts(*project.mesh, *project.model, origin));
        if (project.revision == m_revision)
            m_viewport.setSelectedObjects(m_selections);
    }
    else
        m_viewport.setModel(makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin));
    if (m_previewShown)
        m_viewport.setModelPickingEnabled(false);
    updateGuide();
}
void HoleTool::invalidate()
{
    // 恢复源显示时暂时禁止 selectionChanged，避免 setModel 的清空通知重新提交识别。
    if (m_previewShown)
    {
        showModel(true);
        m_previewShown = false;
    }
    m_preview.reset();
    m_apply->setEnabled(false);
    m_loading = true;
    m_original->setChecked(false);
    m_original->setEnabled(false);
    m_loading = false;
    if (m_active)
        m_hint->setText(QStringLiteral("参数或底边已改变，请预览后再应用。"));
    updateGuide();
    if (onChanged)
        onChanged();
}
void HoleTool::setInputsEnabled(bool enabled)
{
    for (QWidget *w : std::initializer_list<QWidget *>{m_kind, m_shape, m_edge, m_size, m_width, m_height, m_offset,
                                                       m_reference, m_first, m_second, m_previewButton})
        w->setEnabled(enabled);
    m_previewButton->setEnabled(enabled && !m_bases.empty());
}
void HoleTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_inspectTask.empty() || !m_applyTask.empty() ||
        m_bases.empty())
        return;
    invalidate();
    m_request = currentRequest();
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_service.submitHolePreview(m_request, m_revision);
        setInputsEnabled(false);
        m_hint->setText(QStringLiteral("正在计算开孔预览… 可退出取消。"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
        m_service.reportUiFailure("添加孔", e.what());
    }
    if (onChanged)
        onChanged();
}
void HoleTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
        return;
    try
    {
        m_applyTask = m_service.submitHole(m_previewRequest, m_revision);
        m_apply->setEnabled(false);
        setInputsEnabled(false);
        m_original->setEnabled(false);
        m_hint->setText(QStringLiteral("正在应用开孔…"));
    }
    catch (const std::exception &e)
    {
        m_hint->setText(QString::fromUtf8(e.what()));
        m_service.reportUiFailure("添加孔", e.what());
    }
    if (onChanged)
        onChanged();
}
void HoleTool::cancel()
{
    for (const auto &task : {m_task, m_inspectTask})
        if (!task.empty())
        {
            (void)m_service.cancel(task);
            m_discard = true;
        }
    m_active = false;
    invalidate();
    m_viewport.setHoleGuide({});
    setInputsEnabled(false);
    m_hint->setText(QStringLiteral("添加孔已退出。"));
}
void HoleTool::sync(const ProjectSnapshot &project, bool idle)
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
                m_bases = task.holeBases;
                m_loading = true;
                m_edge->clear();
                const auto up = project.referencePlane                  ? project.referencePlane->normal
                                : project.model->importedFormat.empty() ? project.model->frame.yAxis
                                                                        : Vec3{0, 0, 1};
                int best = 0;
                double bestScore = -2;
                for (std::size_t i = 0; i < m_bases.size(); ++i)
                {
                    const auto &b = m_bases[i];
                    const double length =
                        std::hypot(b.firstMm.x - b.secondMm.x, b.firstMm.y - b.secondMm.y, b.firstMm.z - b.secondMm.z);
                    m_edge->addItem(QStringLiteral("边 %1 · %2 mm").arg(b.edge).arg(length, 0, 'f', 1));
                    const double score = b.inward.x * up.x + b.inward.y * up.y + b.inward.z * up.z;
                    if (score > bestScore)
                    {
                        bestScore = score;
                        best = static_cast<int>(i);
                    }
                }
                if (!m_bases.empty())
                    m_edge->setCurrentIndex(best);
                m_loading = false;
                setInputsEnabled(true);
                m_hint->setText(QString::fromUtf8(task.message.c_str()));
                updateGuide();
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
            const auto kind = m_previewRequest.kind;
            cancel();
            if (task.result.committed && onCommitted)
                onCommitted();
            if (reopen && idle)
                begin(kind);
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    if (m_preview && m_active)
        m_apply->setEnabled(idle && m_applyTask.empty());
}
void HoleTool::hideEvent(QHideEvent *event)
{
    if (shouldCancelDockToolOnHide(this))
        cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
