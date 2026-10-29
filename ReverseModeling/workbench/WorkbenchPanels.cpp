/*
 * 文件描述：
 * 将 H 型钢/肋板有效参数与后续算法占位分离，管理参数编辑、应用和参考值恢复。
 * 构建移动、旋转、孔洞、基准面等共用操作表单，提供局部复位与取消但不提交几何命令。
 */
#include <cmath>
#include <stdexcept>

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
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
    auto *templates = new QComboBox(this);
    templates->setObjectName(QStringLiteral("parameters.template"));
    templates->addItems({QStringLiteral("H 型钢 / 肋板（当前支持）"), QStringLiteral("油箱（待接入）"),
                         QStringLiteral("油箱 / 宽扣槽片体（待接入）")});
    auto *templateModel = qobject_cast<QStandardItemModel *>(templates->model());
    templateModel->item(1)->setEnabled(false);
    templateModel->item(2)->setEnabled(false);
    layout->addWidget(templates);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *content = new QWidget(scroll);
    auto *body = new QVBoxLayout(content);
    m_source = paragraph(QStringLiteral("尚未导入点云。"), content);
    body->addWidget(m_source);
    m_basic = new QGroupBox(QStringLiteral("基础参数 · mm"), content);
    auto *form = new QFormLayout(m_basic);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const QStringList names = {QStringLiteral("梁长"),     QStringLiteral("翼板宽度"), QStringLiteral("截面高度"),
                               QStringLiteral("腹板厚度"), QStringLiteral("翼板厚度"), QStringLiteral("肋板厚度")};
    const QStringList ids = {QStringLiteral("length"),          QStringLiteral("width"),
                             QStringLiteral("height"),          QStringLiteral("webThickness"),
                             QStringLiteral("flangeThickness"), QStringLiteral("ribThickness")};
    for (std::size_t i = 0; i < m_dimensions.size(); ++i)
    {
        auto *spin = number(m_basic);
        spin->setObjectName(QStringLiteral("parameters.") + ids[static_cast<qsizetype>(i)]);
        m_dimensions[i] = spin;
        form->addRow(names[static_cast<qsizetype>(i)], spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this]
                {
                    markEdited();
                });
    }
    m_stations = new QLineEdit(m_basic);
    m_stations->setMinimumWidth(90);
    m_stations->setObjectName(QStringLiteral("parameters.ribStations"));
    m_stations->setPlaceholderText(QStringLiteral("例如 300, 800；留空表示无肋板"));
    form->addRow(QStringLiteral("肋板位置"), m_stations);
    connect(m_stations, &QLineEdit::textChanged, this,
            [this]
            {
                markEdited();
            });
    m_swap = new QCheckBox(QStringLiteral("交换截面宽度 / 高度方向"), m_basic);
    form->addRow(m_swap);
    connect(m_swap, &QCheckBox::toggled, this,
            [this]
            {
                if (m_settingValues)
                {
                    return;
                }
                const double width = m_dimensions[1]->value();
                m_dimensions[1]->setValue(m_dimensions[2]->value());
                m_dimensions[2]->setValue(width);
                markEdited();
            });
    m_frame = paragraph(QStringLiteral("导入后显示 PCA 估计框架。"), m_basic);
    form->addRow(m_frame);
    body->addWidget(m_basic);
    m_hint = paragraph(QStringLiteral("板厚与肋板位置为可编辑的参考值，不代表点云测量结果。"), content);
    body->addWidget(m_hint);
    m_model = paragraph(QStringLiteral("尚无模型。"), content);
    body->addWidget(m_model);

    auto *advancedToggle = new QCheckBox(QStringLiteral("高级参数（待接入）"), content);
    body->addWidget(advancedToggle);
    auto *advanced = new QGroupBox(QStringLiteral("油箱与板件识别"), content);
    auto *advancedForm = new QFormLayout(advanced);
    advancedForm->setRowWrapPolicy(QFormLayout::WrapLongRows);
    const QStringList switches = {
        QStringLiteral("使用优化竖直板算法"),     QStringLiteral("启用多工件建模"), QStringLiteral("地面点云过滤"),
        QStringLiteral("禁用非圆弧曲面"),         QStringLiteral("厚板建模优化"),   QStringLiteral("启用遮挡检测"),
        QStringLiteral("图像轮廓辅助搭接板识别"), QStringLiteral("识别折弯箱沿"),   QStringLiteral("小型油箱专用分类")};
    for (const auto &name : switches)
    {
        auto *toggle = new QCheckBox(name, advanced);
        toggle->setEnabled(false);
        advancedForm->addRow(toggle);
    }
    const QStringList fields = {QStringLiteral("平板投影距离阈值"),   QStringLiteral("直板拟合误差阈值"),
                                QStringLiteral("小体过滤阈值"),       QStringLiteral("腹板合并高度阈值"),
                                QStringLiteral("腹板块最大合并距离"), QStringLiteral("最小立板长度"),
                                QStringLiteral("最小立板高度"),       QStringLiteral("遮挡检测容错距离")};
    for (const auto &name : fields)
    {
        auto *spin = number(advanced);
        spin->setEnabled(false);
        advancedForm->addRow(name, spin);
    }
    advanced->hide();
    connect(advancedToggle, &QCheckBox::toggled, advanced, &QWidget::setVisible);
    body->addWidget(advanced);
    body->addStretch();
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);
    auto *footer = new QWidget(this);
    footer->setProperty("panelFooter", true);
    auto *buttons = new QHBoxLayout(footer);
    m_restore = new QPushButton(QStringLiteral("恢复参考值"), footer);
    m_apply = new QPushButton(QStringLiteral("应用参数"), footer);
    m_apply->setObjectName(QStringLiteral("parameters.apply"));
    m_apply->setProperty("primary", true);
    auto *close = new QPushButton(QStringLiteral("关闭"), footer);
    buttons->addWidget(m_restore);
    buttons->addStretch();
    buttons->addWidget(m_apply);
    buttons->addWidget(close);
    connect(m_restore, &QPushButton::clicked, this, &ReconstructionPanel::restoreRequested);
    connect(m_apply, &QPushButton::clicked, this, &ReconstructionPanel::applyRequested);
    connect(close, &QPushButton::clicked, this, &ReconstructionPanel::closeRequested);
    layout->addWidget(footer);
}

void ReconstructionPanel::markEdited()
{
    if (m_settingValues)
    {
        return;
    }
    m_hasEdits = true;
    m_hint->setText(QStringLiteral("参数尚未应用；应用后再重建模型。"));
    emit edited();
}

BeamParameters ReconstructionPanel::values() const
{
    BeamParameters result;
    result.length = m_dimensions[0]->value();
    result.width = m_dimensions[1]->value();
    result.height = m_dimensions[2]->value();
    result.webThickness = m_dimensions[3]->value();
    result.flangeThickness = m_dimensions[4]->value();
    result.ribThickness = m_dimensions[5]->value();
    result.swapCrossSection = m_swap->isChecked();
    for (const auto &token :
         m_stations->text().split(QRegularExpression(QStringLiteral("[,，;；\\s]+")), Qt::SkipEmptyParts))
    {
        bool valid = false;
        const double station = token.toDouble(&valid);
        if (!valid || !std::isfinite(station) || station <= 0.0 || station >= result.length)
        {
            throw std::invalid_argument("肋板位置必须为大于 0 且小于梁长的有限数值（mm）。");
        }
        result.ribStations.push_back(station);
    }
    if (result.length <= 0 || result.width <= 0 || result.height <= 0 || result.webThickness <= 0 ||
        result.flangeThickness <= 0 || result.webThickness >= result.width ||
        result.flangeThickness * 2 >= result.height || (!result.ribStations.empty() && result.ribThickness <= 0))
    {
        throw std::invalid_argument(
            "请检查尺寸和板厚：尺寸与主板厚度必须大于 0，腹板厚度小于翼板宽度，双翼板厚度小于高度。");
    }
    return result;
}

void ReconstructionPanel::setValues(const BeamParameters &parameters)
{
    m_settingValues = true;
    const std::array values = {parameters.length,       parameters.width,           parameters.height,
                               parameters.webThickness, parameters.flangeThickness, parameters.ribThickness};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        m_dimensions[i]->setValue(values[i]);
    }
    m_swap->setChecked(parameters.swapCrossSection);
    QStringList stations;
    for (const auto value : parameters.ribStations)
    {
        stations.push_back(QString::number(value, 'f', 4));
    }
    m_stations->setText(stations.join(QStringLiteral(", ")));
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
void ReconstructionPanel::setEditable(bool editable, bool canRestore)
{
    m_basic->setEnabled(editable);
    m_apply->setEnabled(editable);
    m_restore->setEnabled(canRestore);
}
void ReconstructionPanel::setSourceText(const QString &text)
{
    m_source->setText(text);
}
void ReconstructionPanel::setFrameText(const QString &text)
{
    m_frame->setText(text);
}
void ReconstructionPanel::setHint(const QString &text)
{
    m_hint->setText(text);
}
void ReconstructionPanel::setModelText(const QString &text)
{
    m_model->setText(text);
}

OperationPanel::OperationPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("panel.operation"));
    setMinimumWidth(300);
    auto *layout = new QVBoxLayout(this);
    m_title = new QLabel(QStringLiteral("操作面板"), this);
    m_title->setProperty("panelTitle", true);
    layout->addWidget(m_title);
    layout->addWidget(
        paragraph(QStringLiteral("选择上方工具以设置参数。几何操作待接入，当前输入不会修改模型。"), this));
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_stack = new QStackedWidget(scroll);
    scroll->setWidget(m_stack);
    layout->addWidget(scroll, 1);
    auto *footer = new QWidget(this);
    footer->setProperty("panelFooter", true);
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

void OperationPanel::showOperation(ActionId id, const QString &title)
{
    using enum ActionId;
    m_current = id;
    m_currentTitle = title;
    m_title->setText(title);
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
    else if (id == BasePlane)
    {
        auto *choices = new QWidget(group);
        auto *row = new QHBoxLayout(choices);
        auto *buttons = new QButtonGroup(choices);
        buttons->setExclusive(true);
        for (const auto &label : {QStringLiteral("3 点"), QStringLiteral("特征面")})
        {
            auto *button = new QPushButton(WorkbenchTheme::icon(QStringLiteral("plane")), label, choices);
            button->setCheckable(true);
            button->setMinimumHeight(75);
            buttons->addButton(button);
            row->addWidget(button);
            if (label == QStringLiteral("3 点"))
            {
                button->setChecked(true);
            }
        }
        form->addRow(choices);
        form->addRow(paragraph(QStringLiteral("已选择 0 个对象 · 视图拾取待接入"), group));
    }
    else if (id == Measure)
    {
        addChoice(form, QStringLiteral("测量方式"),
                  {QStringLiteral("两点距离"), QStringLiteral("夹角"), QStringLiteral("半径")});
        form->addRow(paragraph(QStringLiteral("选择几何对象后在此显示测量结果。"), group));
    }
    else if (id == Fillet)
    {
        addChoice(form, QStringLiteral("类型"), {QStringLiteral("圆角"), QStringLiteral("倒角")});
        value(QStringLiteral("尺寸"), QStringLiteral(" mm"));
    }
    else if (id == WeldHole || id == BottomHole)
    {
        addChoice(form, QStringLiteral("孔型"), {QStringLiteral("圆形"), QStringLiteral("长圆形")});
        value(QStringLiteral("半径"), QStringLiteral(" mm"));
        value(QStringLiteral("偏移"), QStringLiteral(" mm"), true);
    }
    else if (id == PlateThickness)
    {
        value(QStringLiteral("板厚"), QStringLiteral(" mm"));
    }
    else if (id == BuildSelection || id == Obstacle)
    {
        addChoice(form, QStringLiteral("构建类型"), {QStringLiteral("平板"), QStringLiteral("实体")});
        form->addRow(paragraph(QStringLiteral("尚未选择点云。选点交互待接入。"), group));
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
