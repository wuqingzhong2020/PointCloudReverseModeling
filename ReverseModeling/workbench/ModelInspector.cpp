/*
 * 文件描述：按实际模型部件生成带颜色的树节点和只读属性，以稳定 ID 同步树与三维部件。
 * 属性读取当前模型快照，保留识别、估算、手动和未提供的区别；只更新选择，不修改业务模型。
 */
#include <algorithm>

#include <QColor>
#include <QHeaderView>
#include <QIcon>
#include <QItemSelectionModel>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTreeWidget>

#include "workbench/ModelDisplayData.h"
#include "workbench/ModelInspector.h"
#include <QMouseEvent>
#include <QContextMenuEvent>

namespace reverse::workbench
{
namespace
{
constexpr int kindRole = Qt::UserRole;
constexpr int idRole = Qt::UserRole + 1;

QString text(const std::string &value) { return QString::fromStdString(value); }

QString coordinates(const Vec3 &point)
{
    return QStringLiteral("(%1, %2, %3)").arg(point.x, 0, 'f', 3)
        .arg(point.y, 0, 'f', 3).arg(point.z, 0, 'f', 3);
}

QString partName(QString id)
{
    const bool mirrored = id.endsWith(QStringLiteral("-mirror"));
    if (mirrored) id.chop(7);
    QString name = id;
    if (id == QStringLiteral("web")) name = QStringLiteral("底板 / 腹板");
    else if (id == QStringLiteral("other-base")) name = QStringLiteral("焊件底板");
    else if (id.startsWith(QStringLiteral("other-wall-")))
        name = QStringLiteral("焊件立板 / 连接组 %1").arg(id.mid(11));
    else if (id == QStringLiteral("flange-bottom")) name = QStringLiteral("侧板 1 / 翼板");
    else if (id == QStringLiteral("flange-top")) name = QStringLiteral("侧板 2 / 翼板");
    else if (id.startsWith(QStringLiteral("tbar-")))
        name = QStringLiteral("T排 %1 / %2")
                   .arg(id.split(QLatin1Char('-')).value(1),
                        id.endsWith(QStringLiteral("-base")) ? QStringLiteral("底板") : QStringLiteral("立板"));
    else if (id.startsWith(QStringLiteral("point-obstacle-"))) name = QStringLiteral("选点障碍物 %1").arg(id.mid(15));
    else if (id.startsWith(QStringLiteral("point-model-"))) name = QStringLiteral("选点模型 %1").arg(id.mid(12));
    else if (id.startsWith(QStringLiteral("obstacle-"))) name = QStringLiteral("障碍物 %1").arg(id.mid(9));
    else if (id.startsWith(QStringLiteral("rib-")))
    {
        const auto fields = id.split(QLatin1Char('-'));
        name = QStringLiteral("肋板 %1 / 筋板").arg(fields.value(1));
        if (fields.value(2) == QStringLiteral("left")) name += QStringLiteral("（左侧）");
        if (fields.value(2) == QStringLiteral("right")) name += QStringLiteral("（右侧）");
    }
    else if (id.startsWith(QStringLiteral("generated-cover-")))
        name = QStringLiteral("外扩贴板 %1").arg(id.mid(16));
    else if (id.startsWith(QStringLiteral("generated-base-")))
        name = QStringLiteral("下底板 %1").arg(id.mid(15));
    else if (id.startsWith(QStringLiteral("mirrored-part-")))
        name = QStringLiteral("镜像板件 %1").arg(id.mid(14));
    else if (id.startsWith(QStringLiteral("imported-part-")))
        name = QStringLiteral("导入部件 %1").arg(id.mid(14));
    return mirrored ? name + QStringLiteral("（镜像）") : name;
}
}

bool ModelInspector::eventFilter(QObject *object,QEvent *event)
{
    if(object==m_tree->viewport()&&event->type()==QEvent::MouseButtonPress) {
        auto *mouse=static_cast<QMouseEvent*>(event);
        if(mouse->button()==Qt::RightButton) {
            bool hasPart=false;for(auto *item:m_tree->selectedItems()) if(!item->data(0,idRole).toString().isEmpty()) hasPart=true;
            if(!hasPart) {auto *item=m_tree->itemAt(mouse->position().toPoint());if(item&&!item->data(0,idRole).toString().isEmpty()) {m_tree->clearSelection();item->setSelected(true);}}
            return true;
        }
    }
    if(object==m_tree->viewport()&&event->type()==QEvent::ContextMenu) {
        if(onContextMenu) onContextMenu(static_cast<QContextMenuEvent*>(event)->globalPos());return true;
    }
    return QObject::eventFilter(object,event);
}

ModelInspector::ModelInspector(QWidget *parent)
    : QObject(parent), m_tree(new QTreeWidget(parent)), m_properties(new QTableWidget(0, 2, parent))
{
    m_tree->viewport()->installEventFilter(this);
    m_tree->setObjectName(QStringLiteral("panel.modelTree"));
    m_tree->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("板厚 mm")});
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_tree->setAlternatingRowColors(true);
    m_tree->setUniformRowHeights(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_properties->setObjectName(QStringLiteral("panel.properties"));
    m_properties->setHorizontalHeaderLabels({QStringLiteral("属性"), QStringLiteral("值")});
    m_properties->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_properties->horizontalHeader()->setStretchLastSection(true);
    m_properties->verticalHeader()->hide();
    m_properties->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_properties->setAlternatingRowColors(true);
    m_properties->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, [this]()
    {
        std::vector<std::string> ids;
        for (const auto *item : m_tree->selectedItems())
            if (item->data(0, kindRole).toString() == QStringLiteral("part"))
                ids.push_back(item->data(0, idRole).toString().toStdString());
        showProperties(m_tree->currentItem());
        m_properties->scrollToTop();
        if (onPartsSelected) onPartsSelected(ids);
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int)
    {
        if (item->isSelected() && item->data(0, kindRole).toString() == QStringLiteral("part") && onPartClicked)
            onPartClicked(item->data(0, idRole).toString().toStdString());
    });
}

QTreeWidget *ModelInspector::tree() const { return m_tree; }
QTableWidget *ModelInspector::properties() const { return m_properties; }

void ModelInspector::setProject(const ProjectSnapshot &project)
{
    const bool changed = !m_root || project.modelId != m_project.modelId ||
        project.cloudId != m_project.cloudId || project.mesh != m_project.mesh;
    m_project = project;
    if (!changed)
    {
        showProperties(m_tree->currentItem());
        return;
    }
    const QSignalBlocker blocker(m_tree);
    m_tree->clear();
    m_parts.clear();
    m_partProperties.clear();
    m_modelNode = nullptr;
    m_root = new QTreeWidgetItem(m_tree, {QStringLiteral("当前工程"), QStringLiteral("工程"), QString()});
    m_root->setData(0, kindRole, QStringLiteral("project"));
    if (project.cloud)
    {
        auto *cloud = new QTreeWidgetItem(m_root, {QStringLiteral("点云"), QStringLiteral("点云"), QString()});
        cloud->setData(0, kindRole, QStringLiteral("cloud"));
        cloud->setToolTip(0, text(project.sourcePathUtf8));
    }
    if (project.model && project.mesh)
    {
        const auto &model = *project.model;
        const auto format = model.importedFormat.empty() ? QStringLiteral("重建模型")
            : text(model.importedFormat).toUpper() + QStringLiteral(" 模型");
        m_modelNode = new QTreeWidgetItem(m_root,
            {format, QStringLiteral("%1 部件").arg(model.partIds.size()), QString()});
        m_modelNode->setData(0, kindRole, QStringLiteral("model"));
        // 只在模型资产变化时生成元信息，点云选区或导出状态变化不重建树，不改变当前选择。
        const auto parts = makeModelDisplayParts(*project.mesh, model, {});
        for (const auto &part : parts)
        {
            const auto id = text(part.id);
            const auto name = partName(id);
            const bool obstacle = model.obstacleParts.contains(part.id);
            const auto plate = std::find_if(model.plates.begin(), model.plates.end(),
                [&part](const auto &value) { return value.id == part.id; });
            const bool upright = plate != model.plates.end() && !plate->uprightFootprint.empty();
            const auto kind = obstacle ? QStringLiteral("障碍物") : upright ? QStringLiteral("连接组实体")
                : part.thicknessUnresolved ? QStringLiteral("观测面") : QStringLiteral("部件");
            const QString thickness = obstacle ? QStringLiteral("—") : part.thicknessMm ? QString::number(*part.thicknessMm, 'f', 1)
                : part.thicknessUnresolved || !model.importedFormat.empty() ? QStringLiteral("未识别") : QStringLiteral("未提供");
            auto *item = new QTreeWidgetItem(m_modelNode, {name, kind, thickness});
            item->setData(0, kindRole, QStringLiteral("part"));
            item->setData(0, idRole, id);
            item->setToolTip(0, name + QStringLiteral("\nID: ") + id);
            QPixmap swatch(14, 14);
            swatch.fill(QColor::fromRgbF(part.colorSrgb[0], part.colorSrgb[1], part.colorSrgb[2]));
            item->setIcon(0, QIcon(swatch));
            m_parts.insert(id, item);
            QString basis = !model.importedFormat.empty()
                ? (model.importedThicknesses.contains(part.id) ? QStringLiteral("实体几何：两平行主面间距")
                                                             : QStringLiteral("未能从实体几何确定等厚板厚"))
                : !model.fromPointCloud ? QStringLiteral("手动建模参数")
                : part.thicknessEstimated ? QStringLiteral("两侧板识别厚度的最大值")
                : part.thicknessUnresolved ? QStringLiteral("仅观测面，缺少厚度依据") : QStringLiteral("点云顶边宽度识别");
            if (upright) basis = plate->thicknessMm ? QStringLiteral("圆角矩形围板四条直边的顶边宽度")
                : QStringLiteral("连接组未确定统一板厚；可双击标签输入，默认 1 mm");
            if (plate != model.plates.end() && plate->method == "other_base_default_thickness")
                basis = QStringLiteral("底板默认 1 mm（非实测），可手动修改；观测上表面固定");
            if (plate != model.plates.end() && plate->method.starts_with("explicit_mirror_of_"))
                basis = QStringLiteral("显式镜像，沿用原板件厚度");
            if (model.thicknessOverrides.contains(part.id))
            {
                basis = model.editSources.contains(part.id) ? QStringLiteral("板件编辑后实体板厚")
                    : part.id == "other-base" ? QStringLiteral("手动修改底板厚度（观测上表面固定）")
                    : QStringLiteral("手动修改实体板厚（中面固定）");
            }
            if (const auto mirrored = model.mirroredParts.find(part.id); mirrored != model.mirroredParts.end())
                basis = QStringLiteral("镜像补全；来源部件：") + text(mirrored->second);
            else if (std::find(model.mirrorCompletedParts.begin(), model.mirrorCompletedParts.end(), part.id) != model.mirrorCompletedParts.end())
                basis = QStringLiteral("镜像补全，保留源板厚度");
            if (const auto generated = model.generatedPlates.find(part.id); generated != model.generatedPlates.end())
            {
                basis = QStringLiteral("附加板显式板厚；来源部件：") + text(generated->second.sourcePartId);
                if (!generated->second.secondSourcePartId.empty())
                {
                    basis += QStringLiteral("、") + text(generated->second.secondSourcePartId);
                }
            }
            if (obstacle) basis = QStringLiteral("不适用（避障包络）");
            PropertyRows rows{{QStringLiteral("名称"), name}, {QStringLiteral("部件 ID"), id},
                {QStringLiteral("模型 ID"), text(project.modelId)}, {QStringLiteral("类型"), kind},
                {QStringLiteral("板厚 mm"), thickness}, {QStringLiteral("厚度依据"), basis},
                {QStringLiteral("包围盒最小 mm"), coordinates(part.boundsMinMm)},
                {QStringLiteral("包围盒最大 mm"), coordinates(part.boundsMaxMm)},
                {QStringLiteral("包围盒尺寸 mm"), coordinates({part.boundsMaxMm.x - part.boundsMinMm.x,
                    part.boundsMaxMm.y - part.boundsMinMm.y, part.boundsMaxMm.z - part.boundsMinMm.z})},
                {QStringLiteral("坐标系"), QStringLiteral("项目坐标 / 世界轴对齐包围盒")},
                {QStringLiteral("三角面数"), QString::number(part.triangles.size())},
                {QStringLiteral("源文件"), text(project.sourcePathUtf8)}};
            if (obstacle)
            {
                const auto &info = model.obstacleParts.at(part.id);
                rows.append({QStringLiteral("用途"), info.boundingBox ? QStringLiteral("占用空间包围盒，不参与工件镜像")
                                                                   : QStringLiteral("独立避障包络，不参与工件镜像")});
                rows.append({QStringLiteral("支持点数"), QString::number(info.supportPoints)});
                rows.append({QStringLiteral("包络分辨率 mm"), QString::number(info.resolutionMm, 'f', 2)});
                rows.append({QStringLiteral("采样余量 mm"), QString::number(info.paddingMm, 'f', 2)});
            }
            if (plate != model.plates.end())
            {
                rows.insert(6, {QStringLiteral("支持点数"), QString::number(plate->supportPoints)});
                rows.insert(7, {QStringLiteral("板厚离散度 mm"), QString::number(plate->thicknessSpreadMm, 'f', 1)});
                if (model.fromPointCloud)
                {
                    rows.append({QStringLiteral("可见轮廓"), upright ? QStringLiteral("直线/圆弧内外环")
                        : plate->outline.empty() ? QStringLiteral("直板近似")
                        : QStringLiteral("点云直线/圆弧拟合")});
                    if (plate->normalAxis == 1 && !upright)
                        rows.append({QStringLiteral("识别腹板孔"), QString::number(plate->holes.size())});
                    if (plate->method.starts_with("other_") && plate->id != "other-base")
                        rows.append({QStringLiteral("根部连接"), QStringLiteral("顶边垂直投影到底板；遮挡根部未实测")});
                    else if (plate->normalAxis == 0)
                        rows.append({QStringLiteral("根部连接"), QStringLiteral("假设落在腹板面；遮挡孔洞/缺口未确认")});
                }
            }
            item->setToolTip(2, basis);
            if(const auto reason=model.localSourceUnavailable.find(part.id);reason!=model.localSourceUnavailable.end())
                rows.append({QStringLiteral("局部重建"),text(reason->second)});
            m_partProperties.insert(id, rows);
        }
    }
    m_tree->expandAll();
    m_tree->setCurrentItem(m_modelNode ? m_modelNode : m_root);
    showProperties(m_tree->currentItem());
    m_properties->scrollToTop();
}

void ModelInspector::selectPart(const std::string &id)
{
    selectParts(id.empty() ? std::vector<std::string>{} : std::vector<std::string>{id});
}

void ModelInspector::selectParts(const std::vector<std::string> &ids)
{
    const QSignalBlocker blocker(m_tree);
    m_tree->clearSelection();
    auto *item = m_modelNode ? m_modelNode : m_root;
    for (const auto &id : ids)
    {
        if (auto *part = m_parts.value(text(id), nullptr))
        {
            part->setSelected(true);
            item = part;
        }
    }
    m_tree->setCurrentItem(item, 0, QItemSelectionModel::NoUpdate);
    if (item) m_tree->scrollToItem(item);
    showProperties(item);
    m_properties->scrollToTop();
}

void ModelInspector::selectObjects(const std::vector<ModelSelection> &selections)
{
    if (selections.size() == 1)
    {
        selectObject(selections.front());
        return;
    }
    std::vector<std::string> ids;
    QStringList objects;
    for (const auto &selection : selections)
    {
        if (std::find(ids.begin(), ids.end(), selection.partId) == ids.end()) ids.push_back(selection.partId);
        const auto *part = m_parts.value(text(selection.partId), nullptr);
        objects << QStringLiteral("%1：%2").arg(part ? part->text(0) : text(selection.partId))
                       .arg(selection.index + 1);
    }
    selectParts(ids);
    if (selections.empty() || selections.front().kind == ModelPickMode::Body) return;
    const auto kind = selections.front().kind;
    const QString type = kind == ModelPickMode::Vertex ? QStringLiteral("点")
        : kind == ModelPickMode::Edge ? QStringLiteral("线") : QStringLiteral("面");
    const PropertyRows rows{{QStringLiteral("拾取对象"), type + QStringLiteral("（多选）")},
        {QStringLiteral("选中对象数"), QString::number(selections.size())},
        {QStringLiteral("所属部件数"), QString::number(ids.size())},
        {QStringLiteral("部件 / 子对象编号"), objects.join(QStringLiteral("\n"))},
        {QStringLiteral("模型 ID"), text(m_project.modelId)}};
    m_properties->setRowCount(static_cast<int>(rows.size()));
    for (qsizetype row = 0; row < rows.size(); ++row)
    {
        m_properties->setItem(static_cast<int>(row), 0, new QTableWidgetItem(rows[row].first));
        auto *value = new QTableWidgetItem(rows[row].second);
        value->setToolTip(rows[row].second);
        m_properties->setItem(static_cast<int>(row), 1, value);
    }
    m_properties->resizeRowsToContents();
    m_properties->scrollToTop();
}

void ModelInspector::selectObject(const ModelSelection &selection)
{
    selectPart(selection.partId);
    if (selection.partId.empty()) return;
    const QString kind = selection.kind == ModelPickMode::Vertex ? QStringLiteral("点")
        : selection.kind == ModelPickMode::Edge ? QStringLiteral("线")
        : selection.kind == ModelPickMode::Face ? QStringLiteral("面") : QStringLiteral("体（整个部件）");
    m_properties->insertRow(0);
    m_properties->setItem(0, 0, new QTableWidgetItem(QStringLiteral("拾取对象")));
    m_properties->setItem(0, 1, new QTableWidgetItem(kind));
    if (selection.kind != ModelPickMode::Body)
    {
        m_properties->insertRow(1);
        m_properties->setItem(1, 0, new QTableWidgetItem(QStringLiteral("子对象编号")));
        m_properties->setItem(1, 1, new QTableWidgetItem(QString::number(selection.index + 1)));
    }
    m_properties->resizeRowsToContents();
    m_properties->scrollToTop();
}

void ModelInspector::showProperties(QTreeWidgetItem *item)
{
    const auto kind = item ? item->data(0, kindRole).toString() : QString();
    PropertyRows rows;
    QStringList selectedIds, selectedNames;
    for (const auto *selected : m_tree->selectedItems())
    {
        if (selected->data(0, kindRole).toString() != QStringLiteral("part")) continue;
        selectedIds << selected->data(0, idRole).toString();
        selectedNames << selected->text(0);
    }
    if (selectedIds.size() > 1)
    {
        rows = {{QStringLiteral("拾取对象"), QStringLiteral("体（多个部件）")},
                {QStringLiteral("选中部件数"), QString::number(selectedIds.size())},
                {QStringLiteral("名称"), selectedNames.join(QStringLiteral("\n"))},
                {QStringLiteral("部件 ID"), selectedIds.join(QStringLiteral("\n"))},
                {QStringLiteral("模型 ID"), text(m_project.modelId)}};
    }
    else if (kind == QStringLiteral("part")) rows = m_partProperties.value(item->data(0, idRole).toString());
    else
    {
        rows = {{QStringLiteral("对象"), item ? item->text(0) : QStringLiteral("当前工程")},
            {QStringLiteral("工程版本"), QString::number(m_project.revision)},
            {QStringLiteral("源文件"), m_project.sourcePathUtf8.empty() ? QStringLiteral("尚未导入")
                                                                        : text(m_project.sourcePathUtf8)},
            {QStringLiteral("单位"), QStringLiteral("mm")}};
        if (kind != QStringLiteral("cloud") && m_project.model)
        {
            const auto &model = *m_project.model;
            rows.append({QStringLiteral("模型 ID"), text(m_project.modelId)});
            rows.append({QStringLiteral("部件数"), QString::number(model.partIds.size())});
            rows.append({QStringLiteral("实体数"), QString::number(model.solidCount)});
            rows.append({QStringLiteral("独立曲面数"), QString::number(model.surfaceCount)});
            rows.append({QStringLiteral("总体积 mm³"), QString::number(model.volumeMm3, 'f', 3)});
        }
        if (kind != QStringLiteral("model") && m_project.cloud)
        {
            rows.append({QStringLiteral("点云 ID"), text(m_project.cloudId)});
            rows.append({QStringLiteral("有效点数"), QString::number(m_project.cloud->points.size())});
            rows.append({QStringLiteral("选中点数"), QString::number(m_project.selection ? m_project.selection->size() : 0)});
        }
    }
    m_properties->setRowCount(static_cast<int>(rows.size()));
    for (qsizetype row = 0; row < rows.size(); ++row)
    {
        m_properties->setItem(static_cast<int>(row), 0, new QTableWidgetItem(rows[row].first));
        auto *value = new QTableWidgetItem(rows[row].second);
        value->setToolTip(rows[row].second);
        m_properties->setItem(static_cast<int>(row), 1, value);
    }
    m_properties->resizeRowsToContents();
}
}
