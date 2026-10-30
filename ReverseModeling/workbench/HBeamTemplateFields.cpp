/*
 * 文件描述：
 * 按 H 型钢参考界面组织基础和高级识别参数、单位与帮助说明。
 * 当前所有模板算法项均为只读预览，参考值不参与识别、不作为板厚或工程参数保存。
 * 帮助入口保持可用，展开高级分组仅影响界面，不改变现有模型与识别结果。
 */
#include <span>

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWhatsThis>

#include "workbench/HBeamTemplateFields.h"

namespace reverse::workbench
{
namespace
{
enum class FieldKind
{
    Number,
    Switch
};

struct FieldSpec
{
    const char *id;
    const char *label;
    FieldKind kind;
    double referenceValue;
    const char *unit;
    const char *help;
};

using enum FieldKind;
const FieldSpec basicFields[] = {
    {"recognizeEndPlates", "识别封头水平板", Switch, 0, "",
     "识别 H 型钢端部的水平封头板。需要独立的端部板件检测，当前未接入。"},
    {"horizontalProjectionGap", "平板投影距离阈值", Number, 30, "mm",
     "用于控制水平板投影补齐的距离容差；不是板厚，也不能据此补出不可见背面。"},
    {"verticalProjectionGap", "竖板投影距离阈值", Number, 30, "mm",
     "控制竖板投影时允许补齐的边界缝隙。以约 26 mm 的缝隙为例：阈值 20 mm 时保留，30 mm 时可补齐。"},
    {"optimizeVerticalPlates", "使用优化竖直板算法", Switch, 0, "",
     "切换竖直板识别与拟合策略。当前仅提供现有顶边条带识别流程，尚无可切换的优化算法。"},
    {"curveMergeGap", "曲线合并距离阈值", Number, 30, "mm",
     "控制相邻板件投影曲线的合并距离。以约 20 mm 的间距为例：阈值 15 mm 时分开，25 mm 时可合并。"},
    {"recognizeSidePatches", "识别侧贴板", Switch, 0, "",
     "识别贴附于主板侧面的附加板件，需要区分贴板与主板的独立观测证据。"},
    {"unequalThicknessButt", "识别不同板厚竖直板小角度对接", Switch, 0, "",
     "处理不同厚度竖直板之间的小角度对接关系，不将各板厚度统一为一个默认值。"}};

const FieldSpec advancedFields[] = {
    {"minimumPlateHeightGap", "最小板高间隔", Number, 20, "mm",
     "区分高度接近的水平板层。阈值较小时可保留相近层，较大时可能合并为同一层。"},
    {"boundarySimplification", "边界曲线简化阈值", Number, 7, "mm",
     "边界简化的距离容差。值越大，轮廓越简化；值越小，保留更多细节。需要先恢复真实边界。"},
    {"smallBodyThreshold", "小体过滤阈值", Number, 20, "mm",
     "按识别坐标系中包围盒 X 或 Y 方向尺寸过滤小实体。以约 21 mm 的小体为例：阈值 10 mm 保留，30 mm 过滤。"},
    {"webHoleArea", "腹板孔洞面积阈值", Number, 1000000, "mm²",
     "腹板孔洞填补的面积阈值，值越大，可填补的孔洞范围越大。此处统一以 mm² 显示，当前尚未恢复孔洞。"},
    {"purlinBracketMergeGap", "檩托板合并距离", Number, 50, "mm",
     "用于控制檩托板候选区域的合并距离，需要先完成相应板件分类。"},
    {"multipleAssemblies", "启用多工件建模", Switch, 0, "",
     "将多个独立工件分别识别和建模。当前 H 型钢流程仍面向单个工件。"},
    {"groundFilter", "地面点云过滤", Switch, 0, "",
     "过滤扫描中的地面背景，需区分地面与工件底板，不能直接删除最低平面。"},
    {"regionGrowingRadius", "区域生长搜索半径", Number, 100, "mm",
     "区域生长时搜索邻近支持点的半径。较大半径更容易连通分散点，也可能连接相邻板件。"},
    {"minimumHorizontalSize", "最小水平板尺寸", Number, 80, "mm",
     "水平板候选区域的最小尺寸要求。小于要求的候选需按算法规则排除。"},
    {"minimumHorizontalDensity", "最小水平板密度", Number, 100, "",
     "水平板候选的支持密度要求。参考界面未明确密度计算口径，接入算法前需定义，当前不作点数或面积密度解释。"},
    {"minimumInclinedDensity", "最小斜板密度", Number, 3, "",
     "斜板候选的支持密度要求。密度口径尚待定义，当前未接入斜板识别。"},
    {"verticalDeformation", "立板变形补偿", Switch, 0, "",
     "拟合时处理立板的局部变形。当前按直板构建，尚未实现变形补偿。"},
    {"webDeformation", "腹板变形补偿", Switch, 1, "",
     "拟合时处理腹板的局部变形。参考开关为开启，但当前算法未实现，本项显示不表示补偿生效。"},
    {"rescanRegions", "自动识别补扫区域", Switch, 0, "",
     "定位观测不足、建议重新扫描的区域。当前不会自动生成补扫区域或采集指令。"},
    {"minimumInnerHoleFillArea", "内孔填充最小面积", Number, 25, "mm²",
     "内孔填充的面积参数。需在轮廓与孔洞算法接入时明确填充范围及阈值方向，当前不参与处理。"},
    {"minimumVerticalLength", "最小立板长度", Number, 50, "mm", "立板候选沿其长度方向的最小尺寸，不是板厚阈值。"},
    {"occlusionDetection", "启用遮挡检测", Switch, 1, "",
     "重建阶段的点云遮挡判断，尚未接入。与已经生效的视图板厚标签遮挡隐藏是不同功能。"},
    {"occlusionTolerance", "遮挡检测容错距离", Number, 5, "mm",
     "重建遮挡判断允许的位置偏差；不控制屏幕上板厚标签的显隐。"},
    {"flattenNonThroughTButt", "非贯穿T接头对接焊缝拉平", Switch, 0, "",
     "处理非贯穿 T 接头的对接焊缝几何。当前未接入焊缝识别及拉平操作。"}};

QWidget *fieldWidget(const FieldSpec &spec, QWidget *parent)
{
    auto *row = new QWidget(parent);
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    QWidget *editor = nullptr;
    if (spec.kind == Switch)
    {
        auto *toggle = new QCheckBox(row);
        toggle->setChecked(spec.referenceValue != 0);
        editor = toggle;
    }
    else
    {
        auto *spin = new QDoubleSpinBox(row);
        spin->setRange(0, 1e9);
        spin->setDecimals(0);
        spin->setValue(spec.referenceValue);
        spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
        spin->setMinimumWidth(85);
        spin->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
        editor = spin;
    }
    editor->setObjectName(QStringLiteral("hBeam.reference.") + QString::fromLatin1(spec.id));
    editor->setEnabled(false);
    layout->addWidget(editor, 1);
    auto *help = new QToolButton(row);
    help->setText(QStringLiteral("?"));
    help->setObjectName(QStringLiteral("hBeam.help.") + QString::fromLatin1(spec.id));
    help->setAccessibleName(QString::fromUtf8(spec.label) + QStringLiteral("说明"));
    const auto description = QString::fromUtf8(spec.help);
    help->setToolTip(description + QStringLiteral("\n尚未接入算法，仅供查看。"));
    QObject::connect(help, &QToolButton::clicked, help,
                     [help, description, title = QString::fromUtf8(spec.label)]
                     {
                         const auto html = QStringLiteral("<b>%1</b><p>%2</p><p>尚未接入算法，仅供查看。</p>")
                                               .arg(title.toHtmlEscaped(), description.toHtmlEscaped());
                         QWhatsThis::showText(help->mapToGlobal(help->rect().bottomLeft()), html, help);
                     });
    layout->addWidget(help);
    return row;
}

QGroupBox *group(const QString &title, std::span<const FieldSpec> fields, QWidget *parent)
{
    auto *box = new QGroupBox(title, parent);
    auto *form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    for (const auto &spec : fields)
    {
        auto fieldTitle = QString::fromUtf8(spec.label);
        if (spec.unit[0] != '\0')
        {
            fieldTitle += QStringLiteral("（%1）").arg(QString::fromUtf8(spec.unit));
        }
        auto *label = new QLabel(fieldTitle, box);
        label->setWordWrap(true);
        label->setFixedWidth(145);
        form->addRow(label, fieldWidget(spec, box));
    }
    return box;
}
} // namespace

QWidget *createHBeamTemplateFields(QWidget *parent)
{
    auto *widget = new QWidget(parent);
    widget->setObjectName(QStringLiteral("hBeam.templateFields"));
    auto *layout = new QVBoxLayout(widget);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *notice =
        new QLabel(QStringLiteral("H型钢识别参数预览：以下参考设置尚未接入算法，不影响当前识别结果。"), widget);
    notice->setWordWrap(true);
    notice->setMinimumWidth(1);
    notice->setProperty("muted", true);
    layout->addWidget(notice);
    layout->addWidget(group(QStringLiteral("基础参数（待接入）"), basicFields, widget));
    auto *advancedToggle = new QCheckBox(QStringLiteral("显示高级参数"), widget);
    advancedToggle->setObjectName(QStringLiteral("hBeam.showAdvanced"));
    layout->addWidget(advancedToggle);
    auto *advanced = group(QStringLiteral("高级参数（待接入）"), advancedFields, widget);
    advanced->hide();
    QObject::connect(advancedToggle, &QCheckBox::toggled, advanced, &QWidget::setVisible);
    layout->addWidget(advanced);
    return widget;
}
} // namespace reverse::workbench
