/*
 * 文件描述：管理部件模型树及只读属性表，以模型快照和稳定部件 ID 关联三维选择。
 */
#pragma once

#include <functional>
#include <string>

#include <QList>
#include <QMap>
#include <QObject>
#include <QPair>
#include <QString>

#include "commands/CommandService.h"
#include "workbench/ModelDisplayData.h"

class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QWidget;

namespace reverse::workbench
{
class ModelInspector final : public QObject
{
public:
    explicit ModelInspector(QWidget *parent);
    QTreeWidget *tree() const;
    QTableWidget *properties() const;
    void setProject(const ProjectSnapshot &project);
    void selectPart(const std::string &id);
    void selectParts(const std::vector<std::string> &ids);
    void selectObject(const ModelSelection &selection);
    void selectObjects(const std::vector<ModelSelection> &selections);
    std::function<void(const std::vector<std::string> &)> onPartsSelected;
    std::function<void(const QPoint &)> onContextMenu;
protected:
    bool eventFilter(QObject *,QEvent *) override;

private:
    using PropertyRows = QList<QPair<QString, QString>>;
    QTreeWidget *m_tree;
    QTableWidget *m_properties;
    ProjectSnapshot m_project;
    QTreeWidgetItem *m_root{};
    QTreeWidgetItem *m_modelNode{};
    QMap<QString, QTreeWidgetItem *> m_parts;
    QMap<QString, PropertyRows> m_partProperties;
    void showProperties(QTreeWidgetItem *item);
};
}
