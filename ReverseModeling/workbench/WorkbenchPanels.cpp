/*
 * 文件描述：
 * 管理 H 型钢模板入口及点云板件识别重建选项。
 * 承载各操作工具及待接入表单，模型调整下拉框触发与 Ribbon 相同的动作，切换本身不提交命令。
 */

#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include "workbench/WorkbenchPanels.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
QLabel *paragraph(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setMinimumWidth(1);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setTextFormat(Qt::PlainText);
    label->setProperty("muted", true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return label;
}

QDoubleSpinBox *number(QWidget *parent, const QString &suffix = QStringLiteral(" mm"), bool signedValue = false)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(4);
    spin->setRange(signedValue ? -1e9 : 0.0, 1e9);
    spin->setSuffix(suffix);
    spin->setKeyboardTracking(false);
    spin->setMinimumWidth(90);
    return spin;
}

void addChoice(QFormLayout *form, const QString &label, const QStringList &items)
{
    auto *choice = new QComboBox(form->parentWidget());
    choice->addItems(items);
    form->addRow(label, choice);
}
} // namespace

ReconstructionPanel::ReconstructionPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panel.reconstruction"));
    setMinimumWidth(300);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *templateChoice = new QComboBox(this);
    templateChoice->setObjectName(QStringLiteral("parameters.template"));
    templateChoice->addItem(QStringLiteral("H型钢 / H型钢（默认）"), QStringLiteral("h-beam"));
    templateChoice->setToolTip(QStringLiteral("使用点云板件识别重建 H 型钢（含筋板）。"));
    layout->addWidget(templateChoice);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    m_mirror = new QCheckBox(QStringLiteral("镜像对称（补建另一侧）"), content);
    m_mirror->setObjectName(QStringLiteral("parameters.mirrorSymmetry"));
    m_mirror->setToolTip(QStringLiteral("默认关闭。开启后沿腹板中面（未定厚时为观测面）镜像翼板和筋板；镜像部分不是点云实测。"));
    body->addWidget(m_mirror);
    connect(m_mirror, &QCheckBox::toggled, this, &ReconstructionPanel::markEdited);
    auto *endToleranceRow = new QWidget(content);
    auto *endToleranceLayout = new QFormLayout(endToleranceRow);
    endToleranceLayout->setContentsMargins(0, 0, 0, 0);
    m_endOverhangTolerance = new QDoubleSpinBox(endToleranceRow);
    m_endOverhangTolerance->setObjectName(QStringLiteral("parameters.endOverhangToleranceMm"));
    m_endOverhangTolerance->setRange(0, 1000);
    m_endOverhangTolerance->setDecimals(1);
    m_endOverhangTolerance->setSuffix(QStringLiteral(" mm"));
    m_endOverhangTolerance->setValue(PlateBuildOptions{}.endOverhangToleranceMm);
    m_endOverhangTolerance->setToolTip(QStringLiteral(
        "只处理腹板沿梁长方向超出两翼板共同端部的突出。\n"
        "不超过容差时收齐，超过时保留并提示；0 表示不收齐端部。\n"
        "腹板宽度始终按 H 型钢连接关系夹在两翼板内侧，不改动原始点云。"));
    endToleranceLayout->addRow(QStringLiteral("端部突出容差"), m_endOverhangTolerance);
    body->addWidget(endToleranceRow);
    connect(m_endOverhangTolerance, &QDoubleSpinBox::valueChanged, this, &ReconstructionPanel::markEdited);
    m_endOverhangTolerance->setToolTip(m_endOverhangTolerance->toolTip() + QStringLiteral(
        "\n这是腹板端部收齐选项，不是连接间隙、孔洞或圆弧拟合容差。"));
    m_webHoles = new QCheckBox(QStringLiteral("识别腹板孔"), content);
    m_webHoles->setObjectName(QStringLiteral("parameters.recognizeWebHoles"));
    m_webHoles->setChecked(PlateBuildOptions{}.recognizeWebHoles);
    m_webHoles->setToolTip(QStringLiteral("恢复俯视可见的腹板圆孔和长圆孔；关闭后保留完整腹板。筋板上方可见缺口仍按点云轮廓保留。"));
    body->addWidget(m_webHoles);
    auto *holeRow = new QWidget(content);
    auto *holeForm = new QFormLayout(holeRow);
    holeForm->setContentsMargins(0, 0, 0, 0);
    m_minimumHoleRadius = new QDoubleSpinBox(holeRow);
    m_minimumHoleRadius->setObjectName(QStringLiteral("parameters.minimumHoleRadiusMm"));
    m_minimumHoleRadius->setRange(0, 1000);
    m_minimumHoleRadius->setDecimals(1);
    m_minimumHoleRadius->setSuffix(QStringLiteral(" mm"));
    m_minimumHoleRadius->setValue(PlateBuildOptions{}.minimumHoleRadiusMm);
    m_minimumHoleRadius->setToolTip(QStringLiteral("忽略半径小于此值的腹板孔；长圆孔按两端圆弧半径（孔宽的一半）过滤。0 仍受扫描分辨率限制。"));
    holeForm->addRow(QStringLiteral("忽略小孔半径"), m_minimumHoleRadius);
    body->addWidget(holeRow);
    connect(m_webHoles, &QCheckBox::toggled, this, [this](bool checked) {
        m_minimumHoleRadius->setEnabled(m_webHoles->isEnabled() && checked);
        markEdited();
    });
    connect(m_minimumHoleRadius, &QDoubleSpinBox::valueChanged, this, &ReconstructionPanel::markEdited);
    body->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *footer = new QWidget(this);
    footer->setProperty("panelFooter", true);
    auto *buttons = new QHBoxLayout(footer);
    m_restore = new QPushButton(QStringLiteral("恢复默认参数"), footer);
    auto *close = new QPushButton(QStringLiteral("关闭"), footer);
    buttons->addWidget(m_restore);
    buttons->addStretch();
    buttons->addWidget(close);
    connect(m_restore, &QPushButton::clicked, this, &ReconstructionPanel::restoreRequested);
    connect(close, &QPushButton::clicked, this, &ReconstructionPanel::closeRequested);
    layout->addWidget(footer);
}

void ReconstructionPanel::markEdited()
{
    if (m_settingValues) return;
    m_hasEdits = true;
    emit edited();
}

PlateBuildOptions ReconstructionPanel::options() const
{
    return {m_mirror->isChecked(), m_endOverhangTolerance->value(), m_webHoles->isChecked(), m_minimumHoleRadius->value()};
}

void ReconstructionPanel::setOptions(const PlateBuildOptions &options)
{
    m_settingValues = true;
    m_mirror->setChecked(options.mirrorSymmetry);
    m_endOverhangTolerance->setValue(options.endOverhangToleranceMm);
    m_webHoles->setChecked(options.recognizeWebHoles);
    m_minimumHoleRadius->setValue(options.minimumHoleRadiusMm);
    m_settingValues = false;
    m_hasEdits = false;
}

void ReconstructionPanel::acceptEdits()
{
    m_hasEdits = false;
}

bool ReconstructionPanel::hasEdits() const
{
    return m_hasEdits;
}

void ReconstructionPanel::setEditable(bool editable)
{
    m_mirror->setEnabled(editable);
    m_endOverhangTolerance->setEnabled(editable);
    m_restore->setEnabled(editable);
    m_webHoles->setEnabled(editable);
    m_minimumHoleRadius->setEnabled(editable && m_webHoles->isChecked());
}

OperationPanel::OperationPanel(WorkbenchActions &actions, QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panel.operation"));
    setMinimumWidth(300);
    auto *layout = new QVBoxLayout(this);
    m_title = new QLabel(QStringLiteral("操作面板"), this);
    m_title->setProperty("panelTitle", true);
    layout->addWidget(m_title);
    m_adjustment = new QComboBox(this);
    m_adjustment->setObjectName(QStringLiteral("operation.modelAdjustment"));
    m_adjustment->setAccessibleName(QStringLiteral("模型调整选项"));
    m_adjustment->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_adjustment->setMinimumHeight(34);
    m_adjustment->setToolTip(QStringLiteral("切换模型调整操作；未应用的预览会取消，已应用的模型保持不变。"));
    for (const auto id : WorkbenchActions::modelAdjustmentIds)
    {
        if (id == ActionId::MirrorComplete)
        {
            m_adjustment->addItem(QStringLiteral("钢结构"));
            auto *model = qobject_cast<QStandardItemModel *>(m_adjustment->model());
            auto *section = model->item(m_adjustment->count() - 1);
            section->setFlags(Qt::NoItemFlags);
            auto font = section->font();
            font.setBold(true);
            section->setFont(font);
        }
        auto *action = actions.get(id);
        const int row = m_adjustment->count();
        m_adjustment->addItem(action->text(), static_cast<int>(id));
        const auto syncAction = [this, action, row]
        {
            m_adjustment->setItemText(row, action->text());
            m_adjustment->setItemData(row, action->toolTip(), Qt::ToolTipRole);
            auto *model = qobject_cast<QStandardItemModel *>(m_adjustment->model());
            model->item(row)->setEnabled(action->isEnabled());
        };
        syncAction();
        connect(action, &QAction::changed, this, syncAction);
    }
    m_adjustment->setMaxVisibleItems(m_adjustment->count());
    connect(m_adjustment, &QComboBox::activated, this, [this, &actions](int row)
    {
        const auto value = m_adjustment->itemData(row);
        if (!value.isValid())
        {
            return;
        }
        const auto id = static_cast<ActionId>(value.toInt());
        // 由已存在的动作处理清除预览、选择模式及工具启动；不另建业务执行路径。
        m_adjustment->setCurrentIndex(m_adjustment->findData(static_cast<int>(m_current)));
        actions.get(id)->trigger();
    });
    layout->addWidget(m_adjustment);
    m_intro = paragraph(QStringLiteral("选择上方工具以设置参数。几何操作待接入，当前输入不会修改模型。"), this);
    layout->addWidget(m_intro);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_stack = new QStackedWidget(scroll);
    scroll->setWidget(m_stack);
    layout->addWidget(scroll, 1);
    auto *footer = new QWidget(this);
    footer->setProperty("panelFooter", true);
    m_footer = footer;
    auto *buttons = new QHBoxLayout(footer);
    auto *reset = new QPushButton(QStringLiteral("复位"), footer);
    auto *apply = new QPushButton(QStringLiteral("确定（待接入）"), footer);
    apply->setProperty("primary", true);
    apply->setEnabled(false);
    auto *cancel = new QPushButton(QStringLiteral("取消"), footer);
    buttons->addWidget(reset);
    buttons->addStretch();
    buttons->addWidget(apply);
    buttons->addWidget(cancel);
    connect(reset, &QPushButton::clicked, this,
            [this]
            {
                showOperation(m_current, m_currentTitle);
            });
    connect(cancel, &QPushButton::clicked, this,
            [this]
            {
                showOperation(ActionId::Count, QStringLiteral("操作面板"));
                emit closeRequested();
            });
    layout->addWidget(footer);
    showOperation(ActionId::Count, QStringLiteral("操作面板"));
}

void OperationPanel::setOperationTitle(ActionId id, const QString &title)
{
    m_current = id;
    m_currentTitle = title;
    const int row = m_adjustment->findData(static_cast<int>(id));
    m_title->setText(row >= 0 ? QStringLiteral("模型调整") : title);
    m_adjustment->setCurrentIndex(row);
    m_adjustment->setVisible(row >= 0);
}

void OperationPanel::showCustom(const QString &title, QWidget *content, ActionId id)
{
    setOperationTitle(id, title);
    m_intro->hide();
    m_footer->hide();
    if (m_stack->indexOf(content) < 0) m_stack->addWidget(content);
    m_stack->setCurrentWidget(content);
}

void OperationPanel::showOperation(ActionId id, const QString &title)
{
    m_intro->show();
    m_footer->show();
    using enum ActionId;
    setOperationTitle(id, title);
    if (m_form)
    {
        m_stack->removeWidget(m_form);
        delete m_form;
    }
    m_form = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(m_form);
    auto *group = new QGroupBox(QStringLiteral("设置"), m_form);
    auto *form = new QFormLayout(group);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const auto value = [group, form](const QString &name, const QString &unit, bool signedValue = false)
    {
        auto *spin = number(group, unit, signedValue);
        form->addRow(name, spin);
    };
    if (id == Count)
    {
        form->addRow(paragraph(QStringLiteral("从模型修改或工具分组选择一项操作。"), group));
    }
    else if (id == Move || id == Rotate)
    {
        if (id == Rotate)
        {
            addChoice(form, QStringLiteral("旋转轴"), {QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")});
            value(QStringLiteral("角度"), QStringLiteral(" °"), true);
        }
        const QStringList axes{QStringLiteral("X"), QStringLiteral("Y"), QStringLiteral("Z")};
        const QStringList colors{QStringLiteral("#f16a72"), QStringLiteral("#52c99a"), QStringLiteral("#38b8ec")};
        for (qsizetype i = 0; i < axes.size(); ++i)
        {
            auto *label = new QLabel((id == Rotate ? QStringLiteral("中心 ") : QString()) + axes[i], group);
            label->setStyleSheet(QStringLiteral("color: %1").arg(colors[i]));
            form->addRow(label, number(group, QStringLiteral(" mm"), true));
        }
    }
    else
    {
        value(QStringLiteral("尺寸"), QStringLiteral(" mm"));
        value(QStringLiteral("偏移"), QStringLiteral(" mm"), true);
    }
    if (id != Count && id != BasePlane)
    {
        auto *pick = new QPushButton(QStringLiteral("选择对象（待接入）"), group);
        pick->setEnabled(false);
        form->addRow(pick);
    }
    layout->addWidget(group);
    layout->addStretch();
    m_stack->addWidget(m_form);
    m_stack->setCurrentWidget(m_form);
}
} // namespace reverse::workbench
