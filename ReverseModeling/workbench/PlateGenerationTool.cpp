/*
 * 文件描述：选择主面、单块水平板或两块竖直板，在工作线程生成独立板预览；源板保持原样。
 * 输入改变使旧预览失效，应用走统一命令；取消、切换工具及工程变化清理临时显示。
 */
#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHideEvent>
#include <QGroupBox>
#include <QScrollArea>
#include <QScopedValueRollback>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShortcut>
#include <QVBoxLayout>

#include "geometry/Geometry.h"
#include "workbench/PlateGenerationTool.h"
#include "workbench/PlateGenerationDiagram.h"

namespace reverse::workbench
{
PlateGenerationTool::PlateGenerationTool(CommandService &service, OccViewport &viewport, QWidget *parent)
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
    m_modeRow = new QWidget(body);
    auto *modeLayout = new QFormLayout(m_modeRow);
    modeLayout->setContentsMargins(0, 0, 0, 0);
    m_baseMode = new QComboBox(m_modeRow);
    m_baseMode->setObjectName(QStringLiteral("plateGeneration.baseMode"));
    m_baseMode->addItem(QStringLiteral("单块水平板"));
    m_baseMode->addItem(QStringLiteral("两块竖直板"));
    modeLayout->addRow(QStringLiteral("生成方式"), m_baseMode);
    layout->addWidget(m_modeRow);
    connect(m_baseMode, &QComboBox::currentIndexChanged, this, [this](int row)
    {
        if (m_loading || !m_active)
        {
            return;
        }
        const auto selections = m_viewport.selectedObjects();
        invalidate();
        m_request.baseMode = row == 0 ? BasePlateMode::Horizontal : BasePlateMode::VerticalPair;
        updateDescription();
        selectionChanged(selections);
    });
    m_selected = new QLabel(this);
    m_selected->setWordWrap(true);
    layout->addWidget(m_selected);
    auto *clear = new QPushButton(QStringLiteral("清除选择"), this);
    layout->addWidget(clear);
    connect(clear, &QPushButton::clicked, this,
            [this]
            {
                if (m_service.isIdle() && m_applyTask.empty())
                {
                    invalidate();
                    m_viewport.setSelectedParts({});
                    selectionChanged({});
                }
            });
    m_parameterGroup = new QGroupBox(QStringLiteral("贴板参数"), body);
    auto *parameterLayout = new QVBoxLayout(m_parameterGroup);
    m_diagram = new PlateGenerationDiagram(m_parameterGroup);
    parameterLayout->addWidget(m_diagram);
    layout->addWidget(m_parameterGroup);
    auto *form = new QFormLayout;
    m_offset = new QDoubleSpinBox(this);
    m_thickness = new QDoubleSpinBox(this);
    m_offset->setObjectName(QStringLiteral("plateGeneration.offset"));
    m_thickness->setObjectName(QStringLiteral("plateGeneration.thickness"));
    for (auto *spin : {m_offset, m_thickness})
    {
        spin->setDecimals(spin == m_thickness ? 1 : 3);
        spin->setSuffix(QStringLiteral(" mm"));
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this]
                {
                    if (!m_loading)
                    {
                        invalidate();
                    }
                });
        connect(spin->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this,
                [this, spin]
                {
                    spin->interpretText();
                    preview();
                });
    }
    form->addRow(QStringLiteral("D：外扩"), m_offset);
    form->addRow(QStringLiteral("T：板厚"), m_thickness);
    parameterLayout->addLayout(form);
    m_reverse = new QCheckBox(QStringLiteral("反向（沿选中面的内法线）"), this);
    connect(m_reverse, &QCheckBox::toggled, this,
            [this]
            {
                if (!m_loading)
                {
                    invalidate();
                }
            });
    parameterLayout->addWidget(m_reverse);
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
                {
                    showModel(original);
                }
            });
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
    outer->addLayout(row);
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
    m_apply->setEnabled(false);
}
void PlateGenerationTool::begin(PlateGenerationKind kind)
{
    cancel();
    if (!m_service.isIdle() || !m_task.empty() || !m_applyTask.empty())
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
    m_request.kind = kind;
    m_request.up = {0, 0, 1};
    const bool cover = kind == PlateGenerationKind::Cover;
    m_parameterGroup->setTitle(cover ? QStringLiteral("贴板参数") : QStringLiteral("生成下底板"));
    m_diagram->setKind(kind);
    m_loading = true;
    m_modeRow->setVisible(!cover);
    m_baseMode->setCurrentIndex(0);
    m_offset->setRange(cover ? -100 : 4, cover ? 100 : 1000);
    m_offset->setValue(cover ? 30 : 20);
    m_thickness->setRange(cover ? 1 : 4, cover ? 100 : 50);
    m_thickness->setValue(10);
    m_reverse->setVisible(cover);
    m_reverse->setChecked(false);
    m_loading = false;
    updateDescription();
    m_active = true;
    enableInputs(true);
    selectionChanged(m_viewport.selectedObjects());
    if (onChanged)
    {
        onChanged();
    }
}
void PlateGenerationTool::updateDescription()
{
    const bool cover = m_request.kind == PlateGenerationKind::Cover;
    const bool pair = m_request.baseMode == BasePlateMode::VerticalPair;
    m_diagram->setBaseMode(m_request.baseMode);
    QString description =
        cover ? QStringLiteral("选择一个等厚实体的平面主面，外扩轮廓后沿面法线生成独立贴板。反向会朝原板内部"
                               "生成，可能与原板重叠。")
              : (pair ? QStringLiteral("Shift 选择两块相对平行的竖直板，底部直边需横向分开且沿长度重叠。"
                                       "用两块板底部角点的外包络覆盖两板及中间间隔，生成一块底板。")
                      : QStringLiteral("选择一块水平板，使用朝下主面的真实外轮廓生成一块底板。")) +
                    QStringLiteral("允许偏差 ±5°。投影至源板最低 Z 后外扩，新板平行 XY 平面并沿 -Z 生成；"
                                   "原板不移动，高度差或微倾斜处可能有间隙。弧底或不连续底部不适用。");
    m_description->setToolTip(description + QStringLiteral("\n保留源板，新板不继承封闭内孔。"));
    m_description->setText(cover
        ? QStringLiteral("D：轮廓外扩；T：新增板厚。\n回车或“预览”查看效果，“应用”生成板件。")
        : (pair ? QStringLiteral("Shift 选择两块竖直板，生成一块跨接底板。\n")
                : QStringLiteral("选择一块水平板，沿其底面轮廓生成底板。\n")) +
            QStringLiteral("D：四周外扩；T：沿世界 -Z 新增厚度。\n允许偏差 ±5°；源板不移动。"));
}
void PlateGenerationTool::enableInputs(bool enabled)
{
    for (QWidget *widget : std::initializer_list<QWidget *>{m_offset, m_thickness, m_reverse, m_baseMode})
    {
        widget->setEnabled(enabled);
    }
    m_previewButton->setEnabled(enabled && !m_request.partId.empty());
}
void PlateGenerationTool::selectionChanged(const std::vector<ModelSelection> &selections)
{
    if (!m_active || m_loading || !m_task.empty() || !m_applyTask.empty() || m_previewShown)
    {
        return;
    }
    invalidate();
    m_selection = selections;
    m_request.partId.clear();
    m_request.secondPartId.clear();
    const bool pair = m_request.kind == PlateGenerationKind::Base && m_request.baseMode == BasePlateMode::VerticalPair;
    const std::size_t count = pair ? 2 : 1;
    if (selections.size() != count || std::any_of(selections.begin(), selections.end(),
        [this](const auto &selection) { return selection.kind != pickMode(); }))
    {
        m_selected->setText(pickMode() == ModelPickMode::Face ? QStringLiteral("请选择一个等厚板件的平面主面")
            : QStringLiteral("已选 %1 / %2 块板；%3").arg(selections.size()).arg(count)
                .arg(pair ? QStringLiteral("Shift 加选第二块竖直板") : QStringLiteral("请选择一块水平板")));
        m_hint->setText(pair ? QStringLiteral("需要恰好两块不同的竖直板；适用性检查通过后才能应用。")
                            : QStringLiteral("一次选择一个对象；适用性检查通过后才能应用。"));
        enableInputs(true);
        return;
    }
    m_request.partId = selections.front().partId;
    if (pair)
    {
        m_request.secondPartId = selections.back().partId;
    }
    m_request.faceIndex = static_cast<std::uint32_t>(selections.front().index +
                                                     1); // 显示面编号为 0-based，命令 BRep 编号为 1-based。
    m_selected->setText(QStringLiteral("板件：%1%2")
                            .arg(QString::fromStdString(m_request.partId),
                                 pickMode() == ModelPickMode::Face
                                     ? QStringLiteral(" · 面 %1").arg(m_request.faceIndex)
                                     : pair ? QStringLiteral("、") + QString::fromStdString(m_request.secondPartId)
                                            : QString{}));
    preview();
}
void PlateGenerationTool::showModel(bool original)
{
    const auto project = m_service.project();
    if (!project.model || !project.mesh)
    {
        return;
    }
    const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
    m_viewport.setModelPickingEnabled(true);
    auto parts = original || !m_preview
                     ? makeModelDisplayParts(*project.mesh, *project.model, origin)
                     : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin);
    if (!original && m_preview)
    {
        for (auto &part : parts)
        {
            if (!project.model->generatedPlates.contains(part.id) &&
                m_preview->summary().generatedPlates.contains(part.id))
            {
                part.colorSrgb = {0.30, 0.82, 0.40};
            }
        }
    }
    m_viewport.setModel(std::move(parts));
    m_viewport.setModelPickingEnabled(!m_previewShown);
    // 使用原始拓扑面/部件标识恢复源板的选择，新板保持绿色。
    m_viewport.setSelectedObjects(m_selection);
}
void PlateGenerationTool::invalidate()
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
void PlateGenerationTool::preview()
{
    if (!m_active || !m_service.isIdle() || !m_task.empty() || !m_applyTask.empty() ||
        m_request.partId.empty())
    {
        return;
    }
    invalidate();
    m_request.offsetMm = m_offset->value();
    m_request.thicknessMm = m_thickness->value();
    m_request.reversed = m_reverse->isChecked();
    m_previewRequest = m_request;
    try
    {
        m_discard = false;
        m_task = m_service.submitPlateGenerationPreview(m_request, m_revision);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在检查板件并生成预览…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_request.kind == PlateGenerationKind::Cover ? "生成外扩贴板" : "生成下底板", error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateGenerationTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle() || m_revision != m_service.project().revision)
    {
        return;
    }
    try
    {
        m_applyTask = m_service.submitPlateGeneration(m_previewRequest, m_revision);
        m_apply->setEnabled(false);
        m_original->setEnabled(false);
        enableInputs(false);
        m_hint->setText(QStringLiteral("正在添加独立板件…"));
    }
    catch (const std::exception &error)
    {
        m_hint->setText(QString::fromUtf8(error.what()));
        m_service.reportUiFailure(m_request.kind == PlateGenerationKind::Cover ? "生成外扩贴板" : "生成下底板", error.what());
    }
    if (onChanged)
    {
        onChanged();
    }
}
void PlateGenerationTool::cancel()
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
void PlateGenerationTool::sync(const ProjectSnapshot &project, bool idle)
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
void PlateGenerationTool::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}
} // namespace reverse::workbench
