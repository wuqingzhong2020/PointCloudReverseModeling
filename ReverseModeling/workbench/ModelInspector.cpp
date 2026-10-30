/*
 * 文件描述：按实际模型部件生成带颜色的树节点和只读属性，以稳定 ID 同步树与三维部件。
 * 属性读取当前模型快照，保留识别、估算、手动和未提供的区别；只更新选择，不修改业务模型。
 */
#include <algorithm>

#include <QColor>
#include <QHeaderView>
#include <QIcon>
#include <QPixmap>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTreeWidget>

#include "workbench/ModelDisplayData.h"
#include "workbench/ModelInspector.h"

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
    else if (id == QStringLiteral("flange-bottom")) name = QStringLiteral("侧板 1 / 翼板");
    else if (id == QStringLiteral("flange-top")) name = QStringLiteral("侧板 2 / 翼板");
    else if (id.startsWith(QStringLiteral("rib-")))
    {
        const auto fields = id.split(QLatin1Char('-'));
        name = QStringLiteral("肋板 %1").arg(fields.value(1));
        if (fields.value(2) == QStringLiteral("left")) name += QStringLiteral("（左侧）");
        if (fields.value(2) == QStringLiteral("right")) name += QStringLiteral("（右侧）");
    }
    else if (id.startsWith(QStringLiteral("imported-part-")))
        name = QStringLiteral("导入部件 %1").arg(id.mid(14));
    return mirrored ? name + QStringLiteral("（镜像）") : name;
}
}

ModelInspector::ModelInspector(QWidget *parent)
    : QObject(parent), m_tree(new QTreeWidget(parent)), m_properties(new QTableWidget(0, 2, parent))
{
    m_tree->setObjectName(QStringLiteral("panel.modelTree"));
    m_tree->setHeaderLabels({QStringLiteral("名称"), QStringLiteral("类型"), QStringLiteral("板厚 mm")});
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
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
    connect(m_tree, &QTreeWidget::currentItemChanged, this, [this](QTreeWidgetItem *item)
    {
        showProperties(item);
        m_properties->scrollToTop();
        if (onPartSelected)
            onPartSelected(item && item->data(0, kindRole).toString() == QStringLiteral("part")
                ? item->data(0, idRole).toString().toStdString() : std::string{});
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
            const auto kind = part.thicknessUnresolved ? QStringLiteral("观测面") : QStringLiteral("部件");
            QString thickness = part.thicknessMm ? QString::number(*part.thicknessMm, 'f', 1)
                : part.thicknessUnresolved ? QStringLiteral("未识别") : QStringLiteral("未提供");
            if (part.thicknessEstimated) thickness += QStringLiteral("（估算）");
            auto *item = new QTreeWidgetItem(m_modelNode, {name, kind, thickness});
            item->setData(0, kindRole, QStringLiteral("part"));
            item->setData(0, idRole, id);
            item->setToolTip(0, name + QStringLiteral("\nID: ") + id);
            QPixmap swatch(14, 14);
            swatch.fill(QColor::fromRgbF(part.colorSrgb[0], part.colorSrgb[1], part.colorSrgb[2]));
            item->setIcon(0, QIcon(swatch));
            m_parts.insert(id, item);
            QString basis = !model.importedFormat.empty() ? QStringLiteral("导入文件未提供板厚，不作推测")
                : !model.fromPointCloud ? QStringLiteral("手动建模参数")
                : part.thicknessEstimated ? QStringLiteral("两侧板识别厚度的最大值（估算）")
                : part.thicknessUnresolved ? QStringLiteral("仅观测面，缺少厚度依据") : QStringLiteral("点云顶边宽度识别");
            const auto plate = std::find_if(model.plates.begin(), model.plates.end(),
                [&part](const auto &value) { return value.id == part.id; });
            if (plate != model.plates.end() && plate->method.starts_with("explicit_mirror_of_"))
                basis = QStringLiteral("显式镜像，沿用原板件厚度");
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
            if (plate != model.plates.end())
            {
                rows.insert(6, {QStringLiteral("支持点数"), QString::number(plate->supportPoints)});
                rows.insert(7, {QStringLiteral("板厚离散度 mm"), QString::number(plate->thicknessSpreadMm, 'f', 3)});
            }
            item->setToolTip(2, basis);
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
    const QSignalBlocker blocker(m_tree);
    auto *item = m_parts.value(text(id), m_modelNode ? m_modelNode : m_root);
    m_tree->setCurrentItem(item);
    if (item) m_tree->scrollToItem(item);
    showProperties(item);
    m_properties->scrollToTop();
}

void ModelInspector::showProperties(QTreeWidgetItem *item)
{
    const auto kind = item ? item->data(0, kindRole).toString() : QString();
    PropertyRows rows;
    if (kind == QStringLiteral("part")) rows = m_partProperties.value(item->data(0, idRole).toString());
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
