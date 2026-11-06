/* 文件描述：固定源板集合、显式目标/保留侧，整批预览成功后才提交命令。 */
#include "workbench/PlateEditTool.h"
#include "commands/PlateEditCommands.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QTableWidget>
#include <QVBoxLayout>
#include <algorithm>
namespace reverse::workbench
{
namespace
{
QString q(const std::string &s)
{
    return QString::fromUtf8(s.c_str());
}
QComboBox *combo(QTableWidget *table, int row, int column)
{
    return qobject_cast<QComboBox *>(table->cellWidget(row, column));
}
} // namespace
PlateEditTool::PlateEditTool(CommandService &s, OccViewport &v, QWidget *parent)
    : QWidget(parent), m_service(s), m_viewport(v)
{
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    m_fields = new QWidget(scroll);
    m_fields->setMinimumWidth(0);
    m_fields->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    auto *body = new QVBoxLayout(m_fields);
    m_description = new QLabel(m_fields);
    m_description->setWordWrap(true);
    body->addWidget(m_description);
    m_target = new QComboBox(m_fields);
    m_target->setObjectName("plateEditTarget");
    body->addWidget(m_target);
    m_table = new QTableWidget(m_fields);
    m_table->setObjectName("plateEditParts");
    m_table->setMinimumHeight(180);
    m_table->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->hide();
    body->addWidget(m_table);
    m_form = new QFormLayout;
    body->addLayout(m_form);
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    const auto number = [&](const char *key, const char *title, double value, double max, const char *suffix)
    {
        auto *spin = new QDoubleSpinBox(m_fields);
        spin->setRange(0, max);
        spin->setDecimals(2);
        spin->setValue(value);
        spin->setSuffix(q(suffix));
        spin->setObjectName("plateEdit_" + q(key));
        m_numbers[key] = spin;
        m_form->addRow(q(title), spin);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this]
                {
                    invalidate();
                    updateGuide();
                });
        connect(spin->findChild<QLineEdit *>(), &QLineEdit::returnPressed, this, [this] { preview(); });
    };
    number("max_distance", "最大延长", 50, 1e6, " mm");
    number("tolerance", "接触容差", .15, 10, " mm");
    number("clearance", "装配间隙", 0, 1e5, " mm");
    number("normal_angle", "法向角容差", 1, 15, " °");
    number("inplane_angle", "面内角容差", 1, 15, " °");
    number("max_gap", "最大桥接间隙", 5, 1e5, " mm");
    number("max_hole_area", "孔面积上限（0 全部）", 0, 1e12, " mm²");
    number("notch_width", "缺口跨度上限", 35, 1e5, " mm");
    number("notch_depth", "缺口深度上限", 20, 1e5, " mm");
    number("simplify_tolerance", "缺口检测容差", .01, 10, " mm");
    m_shape = new QComboBox(m_fields);
    m_shape->addItem(QStringLiteral("圆形"), "circle");
    m_shape->addItem(QStringLiteral("三角形"), "triangle");
    m_form->addRow(QStringLiteral("过焊孔形状"), m_shape);
    number("size", "半径 / 直角边长", 20, 1e5, " mm");
    m_boundary = new QCheckBox(QStringLiteral("修复边缘小缺口（包括过焊孔）"), m_fields);
    body->addWidget(m_boundary);
    m_review = new QPushButton(QStringLiteral("重新检查处理列表"), m_fields);
    body->addWidget(m_review);
    m_original = new QCheckBox(QStringLiteral("对比原模型（未应用）"), m_fields);
    body->addWidget(m_original);
    m_hint = new QLabel(m_fields);
    m_hint->setWordWrap(true);
    m_hint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    body->addWidget(m_hint);
    body->addStretch();
    scroll->setWidget(m_fields);
    layout->addWidget(scroll);
    auto *buttons = new QHBoxLayout;
    m_previewButton = new QPushButton(QStringLiteral("预览"), this);
    m_apply = new QPushButton(QStringLiteral("应用"), this);
    auto *exit = new QPushButton(QStringLiteral("退出"), this);
    for (auto *b : {m_previewButton, m_apply, exit})
        buttons->addWidget(b);
    layout->addLayout(buttons);
    connect(m_target, &QComboBox::currentIndexChanged, this, [this] { targetChanged(); });
    connect(m_table, &QTableWidget::itemChanged, this,
            [this]
            {
                invalidate();
                updateGuide();
            });
    connect(m_boundary, &QCheckBox::toggled, this,
            [this]
            {
                if (!m_loading)
                    review();
            });
    connect(m_table, &QTableWidget::currentCellChanged, this,
            [this](int row, int, int, int)
            {
                if (row >= 0 && !m_loading)
                    m_hint->setText(m_table->item(row, m_operation == "holes" ? 4 : 3)->toolTip());
            });
    connect(m_shape, &QComboBox::currentIndexChanged, this,
            [this]
            {
                invalidate();
                updateGuide();
            });
    connect(m_original, &QCheckBox::toggled, this,
            [this](bool original)
            {
                if (!m_loading && m_preview)
                    showModel(original);
            });
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this] { apply(); });
    connect(m_review, &QPushButton::clicked, this, [this] { review(); });
    connect(exit, &QPushButton::clicked, this, [this] { cancel(); });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, [this] { cancel(); });
}
void PlateEditTool::hideEvent(QHideEvent *event)
{
    cancel();
    QWidget::hideEvent(event);
}
void PlateEditTool::begin(const std::string &operation, const std::vector<std::string> &ids, bool all)
{
    cancel();
    m_operation = operation;
    m_active = true;
    m_sources = ids;
    m_revision = m_service.project().revision;
    m_rectangle.reset();
    m_bases.clear();
    setup(m_service.project(), all);
    updateInputs();
    if (operation == "repair" || operation == "holes")
        review();
    if (operation == "crop")
        m_viewport.setPlateCropEnabled(true);
    if (onChanged)
        onChanged();
}
void PlateEditTool::setup(const ProjectSnapshot &project, bool all)
{
    m_loading = true;
    const bool relation =
        m_operation == "extend" || m_operation == "trim" || m_operation == "merge" || m_operation == "split";
    if (all && project.model)
        m_sources = project.model->partIds;
    m_target->clear();
    m_target->addItem(QStringLiteral("点选目标板，或在此选择…"), QString());
    if (project.model)
        for (const auto &id : project.model->partIds)
            m_target->addItem(q(id), q(id));
    m_target->setVisible(relation);
    const bool holes = m_operation == "holes", repair = m_operation == "repair";
    m_table->clear();
    m_table->setColumnCount(holes ? 5 : 4);
    m_table->setHorizontalHeaderLabels(holes ? QStringList{"处理 / 源板", "底边", "端点 1", "端点 2", "说明"}
                                             : QStringList{"处理 / 源板", "目标面", "保留侧", "说明"});
    m_table->setRowCount(static_cast<int>(m_sources.size()));
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        const auto &id = m_sources[row];
        const auto name = q(id).startsWith("imported-part-") ? QStringLiteral("导入部件 %1").arg(q(id).mid(14)) : q(id);
        auto *item = new QTableWidgetItem(name);
        item->setToolTip(q(id));
        item->setCheckState(Qt::Checked);
        m_table->setItem(row, 0, item);
        auto *face = new QComboBox(m_table);
        face->addItem(holes ? QStringLiteral("检查中…") : QStringLiteral("自动判断"), 0);
        m_table->setCellWidget(row, 1, face);
        connect(face, &QComboBox::currentIndexChanged, this,
                [this]
                {
                    invalidate();
                    updateGuide();
                });
        if (holes)
            for (int col : {2, 3})
            {
                auto *it = new QTableWidgetItem;
                it->setCheckState(Qt::Checked);
                m_table->setItem(row, col, it);
            }
        else
        {
            auto *side = new QComboBox(m_table);
            side->addItem(QStringLiteral("请选择保留侧"), -1);
            side->addItem(QStringLiteral("法向正侧"), 1);
            side->addItem(QStringLiteral("法向负侧"), 0);
            m_table->setCellWidget(row, 2, side);
            connect(side, &QComboBox::currentIndexChanged, this, [this] { invalidate(); });
        }
        m_table->setItem(row, holes ? 4 : 3, new QTableWidgetItem(QStringLiteral("待检查")));
    }
    m_table->setColumnHidden(1, repair || m_operation == "crop" || m_operation == "merge" || m_operation == "split");
    m_table->setColumnHidden(2, !holes && m_operation != "trim");
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_table->setColumnWidth(0, 94);
    m_table->setColumnWidth(1, 86);
    m_table->setColumnWidth(2, holes ? 58 : 110);
    if (holes)
        m_table->setColumnWidth(3, 58);
    m_table->horizontalHeader()->setStretchLastSection(true);
    for (const auto &[key, spin] : m_numbers)
    {
        bool visible =
            (m_operation == "extend" && (key == "tolerance" || key == "max_distance")) ||
            ((m_operation == "trim" || m_operation == "split") && key == "clearance") ||
            (m_operation == "merge" && (key == "normal_angle" || key == "inplane_angle" || key == "max_gap")) ||
            (repair &&
             (key == "max_hole_area" || key == "notch_width" || key == "notch_depth" || key == "simplify_tolerance")) ||
            (holes && key == "size");
        m_form->setRowVisible(spin, visible);
    }
    m_form->setRowVisible(m_shape, holes);
    m_boundary->setVisible(repair);
    m_boundary->setChecked(false);
    m_review->setVisible(repair || holes);
    m_description->setText(
        relation ? QStringLiteral("源板已固定；后续点击只指定目标。可从列表指定一块预选板为目标，该板自动退出源集合。\n"
                                  "截取须明确每板的保留侧；合并以目标厚度为准，其他操作保留目标。")
        : repair
            ? QStringLiteral(
                  "检查结果列出全部候选；勾选需要处理的板。默认只填内部贯穿孔，保留外轮廓与厚度。边缘缺口须另行启用。")
        : holes
            ? QStringLiteral("每板单独选择底边及端点开关；尺寸和形状统一。绿色半透明体为切除范围；应用时整批提交。")
            : QStringLiteral("左键拖框切除所选板件的一端，禁止中间或全切。应用将记录工程坐标截断面，回放不依赖相机。"));
    m_hint->setText(QStringLiteral("先设置参数并预览，确认后点击应用。"));
    m_loading = false;
}
void PlateEditTool::targetChanged()
{
    if (m_loading || !m_active)
        return;
    invalidate();
    m_loading = true;
    const auto project = m_service.project();
    const auto id = m_target->currentData().toString().toStdString();
    for (int row = 0; row < m_table->rowCount(); ++row)
    {
        auto *item = m_table->item(row, 0);
        const bool target = m_sources[row] == id;
        item->setFlags(target ? Qt::ItemIsEnabled : Qt::ItemIsEnabled | Qt::ItemIsUserCheckable | Qt::ItemIsSelectable);
        if (target)
            item->setCheckState(Qt::Unchecked);
        auto *face = combo(m_table, row, 1);
        face->clear();
        face->addItem(QStringLiteral("自动判断"), 0);
        if (project.mesh)
            for (const auto &f : project.mesh->planarFaces)
                if (project.mesh->partIds.at(f[0]) == id)
                    face->addItem(QStringLiteral("面 %1").arg(f[1]), f[1]);
        m_table->item(row, 3)->setText(target ? QStringLiteral("目标板") : QStringLiteral("源板"));
    }
    m_loading = false;
    if (project.mesh && project.model)
    {
        const auto origin = project.cloud ? project.cloud->originMm : project.model->frame.originMm;
        m_viewport.setPlateEditReference(makeModelDisplayParts(*project.mesh, *project.model, origin), m_sources, id,
                                         false);
    }
}
void PlateEditTool::selectionChanged(const std::vector<ModelSelection> &selection)
{
    if (!m_active || m_loading || !m_target->isVisible() || !m_service.isIdle() || m_showing || selection.empty())
        return;
    const auto &last = selection.back();
    if (last.kind != ModelPickMode::Body)
        return;
    m_target->setCurrentIndex(m_target->findData(q(last.partId)));
}
PlateEdit PlateEditTool::request() const
{
    PlateEdit r;
    r.operation = m_operation;
    r.target = m_target->currentData().toString().toStdString();
    r.rectangle = m_rectangle;
    r.maxDistance = m_numbers.at("max_distance")->value();
    r.tolerance = m_numbers.at("tolerance")->value();
    r.clearance = m_numbers.at("clearance")->value();
    r.normalAngle = m_numbers.at("normal_angle")->value();
    r.inplaneAngle = m_numbers.at("inplane_angle")->value();
    r.maxGap = m_numbers.at("max_gap")->value();
    r.boundary = m_boundary->isChecked();
    r.maxHoleArea = m_numbers.at("max_hole_area")->value();
    r.notchWidth = m_numbers.at("notch_width")->value();
    r.notchDepth = m_numbers.at("notch_depth")->value();
    r.simplifyTolerance = m_numbers.at("simplify_tolerance")->value();
    for (int row = 0; row < m_table->rowCount(); ++row)
        if (m_table->item(row, 0)->checkState() == Qt::Checked)
        {
            const auto &id = m_sources[row];
            r.parts.push_back(id);
            if (m_operation == "extend" || m_operation == "trim")
                r.contacts.push_back({id, combo(m_table, row, 1)->currentData().toUInt()});
            if (m_operation == "trim")
            {
                const int side = combo(m_table, row, 2)->currentData().toInt();
                if (side < 0)
                    throw std::runtime_error((id + "：请指定法向正侧或负侧作为保留侧，再预览确认。").c_str());
                r.cuts.push_back({id, {}, {1, 0, 0}, side == 1});
            }
            if (m_operation == "holes")
            {
                PlateHole h;
                h.partId = id;
                h.bottomEdge = combo(m_table, row, 1)->currentData().toUInt();
                h.sizeMm = m_numbers.at("size")->value();
                h.shape = m_shape->currentData() == "circle" ? PlateHoleShape::Circle : PlateHoleShape::Triangle;
                h.first = m_table->item(row, 2)->checkState() == Qt::Checked;
                h.second = m_table->item(row, 3)->checkState() == Qt::Checked;
                r.holes.push_back(h);
            }
        }
    return r;
}
void PlateEditTool::showModel(bool original)
{
    const auto p = m_service.project();
    if (!p.model || !p.mesh)
        return;
    m_loading = true;
    const auto origin = p.cloud ? p.cloud->originMm : p.model->frame.originMm;
    m_viewport.setModelPickingEnabled(true);
    m_viewport.setModel(original || !m_preview
                            ? makeModelDisplayParts(*p.mesh, *p.model, origin)
                            : makeModelDisplayParts(m_preview->mesh(), m_preview->summary(), origin));
    if (p.revision == m_revision)
    {
        auto ids = m_sources;
        if (!m_prepared.target.empty())
            ids.push_back(m_prepared.target);
        m_viewport.setSelectedParts(ids);
    }
    m_viewport.setModelPickingEnabled(!m_showing);
    m_loading = false;
    if (m_active)
        m_viewport.setPlateEditReference(
            makeModelDisplayParts(*p.mesh, *p.model, origin), m_prepared.parts, m_prepared.target,
            !original && m_showing && (m_operation == "trim" || m_operation == "split" || m_operation == "crop"));
}
void PlateEditTool::invalidate()
{
    if (m_loading)
        return;
    if (m_showing)
    {
        m_showing = false;
        showModel(true);
    }
    m_preview.reset();
    m_loading = true;
    m_original->setChecked(false);
    m_loading = false;
    updateInputs();
    if (onChanged)
        onChanged();
}
void PlateEditTool::updateInputs()
{
    const bool ready =
        m_active && m_service.isIdle() && m_previewTask.empty() && m_applyTask.empty() && m_reviewTask.empty();
    m_target->setEnabled(ready);
    m_table->setEnabled(ready);
    for (auto &[_, spin] : m_numbers)
        spin->setEnabled(ready);
    m_shape->setEnabled(ready);
    m_boundary->setEnabled(ready);
    m_previewButton->setEnabled(ready);
    m_review->setEnabled(ready);
    m_apply->setEnabled(ready && bool(m_preview));
    m_original->setEnabled(ready && bool(m_preview));
    if (m_active && m_operation == "crop")
        m_viewport.setPlateCropEnabled(ready);
}
void PlateEditTool::fail(const QString &message)
{
    m_hint->setText(message);
    m_service.reportUiFailure(plateEditTitle(m_operation), message.toUtf8().toStdString());
}
void PlateEditTool::updateGuide()
{
    if (m_loading || !m_active || m_operation != "holes" || m_bases.empty() || !m_service.isIdle())
        return;
    try
    {
        const auto r = request();
        std::vector<std::pair<PlateHoleBase, PlateHole>> items;
        for (const auto &h : r.holes)
        {
            const auto &bases = m_bases.at(h.partId);
            auto it = std::find_if(bases.begin(), bases.end(), [&](const auto &b) { return b.edge == h.bottomEdge; });
            if (it != bases.end())
                items.emplace_back(*it, h);
        }
        const auto p = m_service.project();
        const auto o = p.cloud ? p.cloud->originMm : p.model->frame.originMm;
        m_viewport.setBatchHolePreview(items, o);
    }
    catch (const std::exception &e)
    {
        m_viewport.clearHolePreview();
        m_hint->setText(q(e.what()));
    }
}
void PlateEditTool::preview()
{
    if (!m_active || !m_service.isIdle())
        return;
    invalidate();
    try
    {
        auto r = request();
        if (r.operation == "crop" && !r.rectangle)
            throw std::runtime_error("请先在视图中框选待切除端部。");
        m_previewTask = m_service.submitPlateEditPreview(r, m_revision);
        m_hint->setText(QStringLiteral("正在后台计算预览…"));
    }
    catch (const std::exception &e)
    {
        fail(q(e.what()));
    }
    updateInputs();
    if (onChanged)
        onChanged();
}
void PlateEditTool::apply()
{
    if (!m_active || !m_preview || !m_service.isIdle())
        return;
    try
    {
        m_applyTask = m_service.submitPlateEdit(m_prepared, m_revision);
    }
    catch (const std::exception &e)
    {
        fail(q(e.what()));
    }
    updateInputs();
    if (onChanged)
        onChanged();
}
void PlateEditTool::review()
{
    if (!m_active || !m_service.isIdle())
        return;
    invalidate();
    try
    {
        m_reviewTask = m_service.submitPlateReview(m_sources, request(), m_revision);
        m_hint->setText(QStringLiteral("后台检查板件，请稍候…"));
    }
    catch (const std::exception &e)
    {
        fail(q(e.what()));
    }
    updateInputs();
}
void PlateEditTool::crop(const CloudSelectionRect &rectangle)
{
    if (!m_active || m_operation != "crop" || !m_service.isIdle())
        return;
    m_rectangle = rectangle;
    const auto p = m_service.project();
    const auto origin = p.cloud ? p.cloud->originMm : p.model->frame.originMm;
    // 视口用显示原点附近的坐标；几何切割和录制必须使用工程坐标。
    auto &m = m_rectangle->viewProjection;
    for (int row = 0; row < 4; ++row)
        m[row * 4 + 3] -= m[row * 4] * origin.x + m[row * 4 + 1] * origin.y + m[row * 4 + 2] * origin.z;
    preview();
}
void PlateEditTool::cancel()
{
    m_active = false;
    if (!m_previewTask.empty())
        (void)m_service.cancel(m_previewTask);
    if (!m_reviewTask.empty())
        (void)m_service.cancel(m_reviewTask);
    m_previewTask.clear();
    m_reviewTask.clear();
    m_viewport.setPlateCropEnabled(false);
    m_viewport.clearHolePreview();
    m_viewport.clearPlateEditReference();
    invalidate();
}
void PlateEditTool::sync(const ProjectSnapshot &project, bool)
{
    if (m_loading)
        return;
    if (m_active && project.revision != m_revision && m_applyTask.empty())
        cancel();
    if (!m_previewTask.empty())
    {
        const auto t = m_service.task(m_previewTask);
        if (t.finished())
        {
            auto model = m_service.takeEdgePreview(m_previewTask);
            m_previewTask.clear();
            if (m_active && project.revision == m_revision)
            {
                m_hint->setText(q(t.message));
                if (t.state == TaskState::Succeeded && model)
                {
                    m_preview = std::move(model);
                    m_prepared = plateEditRequest(plateEditCommand(m_operation), t.result.value.as<rm::Arguments>());
                    m_showing = true;
                    m_viewport.clearHolePreview();
                    showModel(false);
                }
            }
        }
    }
    if (!m_reviewTask.empty())
    {
        const auto t = m_service.task(m_reviewTask);
        if (t.finished())
        {
            m_reviewTask.clear();
            if (m_active && project.revision == m_revision)
            {
                m_hint->setText(q(t.message));
                if (t.state == TaskState::Succeeded)
                {
                    m_loading = true;
                    m_bases = t.batchHoleBases;
                    const auto rows = commandJson(t.result.value);
                    for (int row = 0; row < m_table->rowCount(); ++row)
                    {
                        const auto &result = rows.at(row);
                        const bool supported = result.at("supported");
                        auto *item = m_table->item(row, 0);
                        item->setCheckState(supported && result.at("changed").get<bool>() ? Qt::Checked
                                                                                          : Qt::Unchecked);
                        item->setFlags(supported ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
                        const int col = m_operation == "holes" ? 4 : 3;
                        m_table->item(row, col)->setText(!supported ? QStringLiteral("不支持（点击查看）")
                                                         : !result.at("changed").get<bool>()
                                                             ? QStringLiteral("无需修改")
                                                         : m_operation == "holes" ? QStringLiteral("请指定底边")
                                                                                  : QStringLiteral("可修复"));
                        m_table->item(row, col)->setToolTip(q(result.at("message").get<std::string>()));
                        if (m_operation == "holes")
                        {
                            auto *edge = combo(m_table, row, 1);
                            edge->clear();
                            edge->addItem(QStringLiteral("请选择底边"), 0);
                            for (const auto &b : m_bases[m_sources[row]])
                                edge->addItem(QStringLiteral("边 %1").arg(b.edge), b.edge);
                        }
                    }
                    m_loading = false;
                    updateGuide();
                }
            }
        }
    }
    if (!m_applyTask.empty())
    {
        const auto t = m_service.task(m_applyTask);
        if (t.finished())
        {
            m_applyTask.clear();
            cancel();
            if (t.result.committed && onCommitted)
                onCommitted();
            m_hint->setText(q(t.message));
        }
    }
    updateInputs();
}
} // namespace reverse::workbench
