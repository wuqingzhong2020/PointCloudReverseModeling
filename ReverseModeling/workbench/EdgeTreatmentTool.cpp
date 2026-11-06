/* 文件描述：回车只触发预览；切换参数使预览失效，确认后通过统一命令提交实体修改。 */
#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "geometry/Geometry.h"
#include "workbench/EdgeTreatmentTool.h"

namespace reverse::workbench
{
EdgeTreatmentTool::EdgeTreatmentTool(CommandService &service, OccViewport &viewport, QWidget *parent)
    : QWidget(parent), m_service(service), m_viewport(viewport)
{
    auto *layout = new QVBoxLayout(this);
    m_selection = new QLabel(this);
    m_selection->setWordWrap(true);
    layout->addWidget(m_selection);
    auto *form = new QFormLayout;
    m_kind = new QComboBox(this);
    m_kind->addItems({QStringLiteral("圆角"), QStringLiteral("等距倒角")});
    m_kind->setObjectName(QStringLiteral("edgeTreatment.kind"));
    form->addRow(QStringLiteral("类型"), m_kind);
    m_size = new QDoubleSpinBox(this);
    m_size->setObjectName(QStringLiteral("edgeTreatment.size"));
    m_size->setRange(0.001, 1e6);
    m_size->setDecimals(3);
    m_size->setValue(10);
    m_size->setSuffix(QStringLiteral(" mm"));
    m_sizeLabel = new QLabel(QStringLiteral("半径 R"), this);
    form->addRow(m_sizeLabel, m_size);
    layout->addLayout(form);
    auto *explanation = new QLabel(QStringLiteral("板件轮廓倒圆 / 倒角，厚度保持不变。轮廓边处理两端板角；板厚方向的短边处理一个角。\n"
                                                  "Shift 点击追加或取消，支持跨部件多选。倒角采用两侧等距。\n"
                                                  "回车或“预览”只查看效果；点击“应用”才修改实体。"),
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
    connect(m_size->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this, [this] {
        m_size->interpretText();
        preview();
    });
    connect(m_size, &QDoubleSpinBox::valueChanged, this, [this] {
        if (!m_loading)
            invalidate();
    });
    connect(m_kind, &QComboBox::currentIndexChanged, this, [this] {
        m_sizeLabel->setText(m_kind->currentIndex() == 0 ? QStringLiteral("半径 R") : QStringLiteral("距离 D"));
        if (!m_loading)
            invalidate();
    });
    connect(m_original, &QCheckBox::toggled, this, [this](bool original) {
        if (!m_loading && m_preview)
            showModel(original);
    });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    m_apply->setEnabled(false);
}
void EdgeTreatmentTool::begin()
{
    cancel();
    if (!m_task.empty() || !m_applyTask.empty() || !m_service.isIdle())
        return;
    const auto project = m_service.project();
    if (!project.model)
        return;
    m_revision = project.revision;
    m_request = {};
    m_selectionError.clear();
    m_active = true;
    setInputsEnabled(true);
    m_apply->setEnabled(false);
    m_hint->setText(QStringLiteral("请选择边线，然后预览效果。"));
    updateSelectionText();
    selectionChanged(m_viewport.selectedObjects());
    if (onChanged)
        onChanged();
}
void EdgeTreatmentTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || !m_task.empty() || !m_applyTask.empty() || m_previewShown)
        return;
    const auto project = m_service.project();
    if (!project.mesh || project.revision != m_revision)
        return;
    m_selections = selections;
    m_request.targets.clear();
    m_selectionError.clear();
    try
    {
        m_request.targets = edgeTreatmentTargets(*project.mesh, m_selections);
    }
    catch (const std::exception &error)
    {
        m_selectionError = QString::fromUtf8(error.what());
        m_service.reportUiFailure(m_kind->currentIndex() == 0 ? "圆角" : "倒角", error.what());
    }
    invalidate();
    setInputsEnabled(true);
    updateSelectionText();
}
void EdgeTreatmentTool::updateSelectionText()
{
    std::size_t count = 0;
    for (const auto &target : m_request.targets) count += target.edges.size();
    m_selection->setText(count == 0 ? QStringLiteral("请选择实体边线")
        : QStringLiteral("已选择 %1 条边（%2 个部件），按板件轮廓处理").arg(count).arg(m_request.targets.size()));
}
void EdgeTreatmentTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
        return;
    const Vec3 origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    if (original || !m_preview)
    {
        m_viewport.setModel(makeModelDisplayParts(*project.mesh, *project.model, origin));
        if (project.revision == m_revision) m_viewport.setSelectedObjects(m_selections);
    }
    else
        m_viewport.setModel(makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin));
    if (m_previewShown)
        m_viewport.setModelPickingEnabled(false);
}
void EdgeTreatmentTool::invalidate()
{
    if (m_previewShown)
    {
        m_previewShown = false;
        showModel(true);
    }
    m_preview.reset();
    m_apply->setEnabled(false);
    m_loading = true;
    m_original->setChecked(false);
    m_original->setEnabled(false);
    m_loading = false;
    if (m_active)
        m_hint->setText(!m_selectionError.isEmpty() ? m_selectionError
            : m_request.targets.empty() ? QStringLiteral("请选择实体边线，然后预览。")
                                        : QStringLiteral("参数或选边已改变，请预览后再应用。"));
    if (onChanged)
        onChanged();
}
void EdgeTreatmentTool::setInputsEnabled(bool enabled)
{
    for (QWidget *w : std::initializer_list<QWidget *>{m_kind, m_size, m_previewButton})
        w->setEnabled(enabled);
    m_previewButton->setEnabled(enabled && !m_request.targets.empty() && m_selectionError.isEmpty());
}
void EdgeTreatmentTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty())
        return;
    if (!m_selectionError.isEmpty() || m_request.targets.empty())
    {
        m_hint->setText(m_selectionError.isEmpty() ? QStringLiteral("请先选择实体边线。") : m_selectionError);
        m_service.reportUiFailure(m_kind->currentIndex() == 0 ? "圆角" : "倒角", m_hint->text().toUtf8().toStdString());
        return;
    }
    invalidate();
    m_request.kind = m_kind->currentIndex() == 0 ? EdgeTreatmentKind::Fillet : EdgeTreatmentKind::Chamfer;
    m_request.sizeMm = m_size->value();
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_service.submitEdgePreview(m_request, m_revision);
        setInputsEnabled(false);
        m_hint->setText(QStringLiteral("正在计算预览… 可退出取消；不会修改模型或录制命令。"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_kind->currentIndex() == 0 ? "圆角" : "倒角", error.what());
    }
    if (onChanged)
        onChanged();
}
void EdgeTreatmentTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
        return;
    try
    {
        m_applyTask = m_service.submitEdgeTreatment(m_previewRequest, m_revision);
        m_apply->setEnabled(false);
        setInputsEnabled(false);
        m_original->setEnabled(false);
        m_hint->setText(QStringLiteral("正在应用实体修改…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_kind->currentIndex() == 0 ? "圆角" : "倒角", error.what());
    }
    if (onChanged)
        onChanged();
}
void EdgeTreatmentTool::cancel()
{
    if (!m_task.empty())
    {
        (void)m_service.cancel(m_task);
        m_discard = true;
    }
    m_active = false;
    invalidate();
    setInputsEnabled(false);
    m_hint->setText(QStringLiteral("预览已退出。点击圆角 / 倒角可继续编辑。"));
}
void EdgeTreatmentTool::sync(const ProjectSnapshot &project, bool idle)
{
    if (m_active && project.revision != m_revision && m_applyTask.empty())
        cancel();
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
                onCommitted();
            if (reopen && idle)
                begin();
            m_hint->setText(QString::fromUtf8(task.message.c_str()));
        }
    }
    if (m_preview && m_active)
        m_apply->setEnabled(idle && m_applyTask.empty());
}
void EdgeTreatmentTool::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
