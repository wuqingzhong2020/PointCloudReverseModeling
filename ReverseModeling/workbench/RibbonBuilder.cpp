/*
 * 文件描述：
 * 创建文件、编辑、点云、重建、模型修改和工具分组，以及视图显示模式工具条。
 * 下拉菜单复用 QAction，窗口菜单由顶部功能区提供入口。
 */
#include <initializer_list>

#include <QAction>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolBar>
#include <QToolButton>
#include <QWidgetAction>

#include <SARibbonBar.h>
#include <SARibbonCategory.h>
#include <SARibbonPanel.h>
#include <SARibbonTabBar.h>
#include <SARibbonToolButton.h>

#include "workbench/RibbonBuilder.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
QMenu *menu(QWidget *parent, const QString &title, WorkbenchActions &actions, std::initializer_list<ActionId> ids,
            const QString &icon)
{
    auto *result = new QMenu(title, parent);
    result->setIcon(WorkbenchTheme::icon(icon));
    result->setToolTipsVisible(true);
    for (const auto id : ids)
    {
        result->addAction(actions.get(id));
    }
    return result;
}
} // namespace

void buildRibbon(SARibbonBar *ribbon, WorkbenchActions &actions, QMenu *windows, QMenu *recentFiles)
{
    using enum ActionId;
    ribbon->setApplicationButton(nullptr);
    ribbon->setTitleBarHeight(0);
    ribbon->setTabBarHeight(0);
    ribbon->ribbonTabBar()->hide();
    ribbon->setTabDoubleClickToMinimumMode(false);
    ribbon->setPanelTitleHeight(22);
    ribbon->setCategoryHeight(112);
    ribbon->setPanelLargeIconSize(QSize(30, 30));
    ribbon->setPanelSmallIconSize(QSize(18, 18));
    auto *category = ribbon->addCategoryPage(QStringLiteral("建模"));
    auto *file = category->addPanel(QStringLiteral("文件"));
    auto *openMenu =
        menu(ribbon, QStringLiteral("打开"), actions, {Open, OpenFolder, ImportParameters}, QStringLiteral("folder"));
    openMenu->addSeparator();
    openMenu->addMenu(recentFiles);
    file->addLargeAction(actions.get(Open), QToolButton::MenuButtonPopup);
    file->actionToRibbonToolButton(actions.get(Open))->setMenu(openMenu);
    auto *exportMenu = menu(ribbon, QStringLiteral("导出"), actions,
                            {ExportStep, ExportCloud, ExportModel, ExportParameters}, QStringLiteral("export"));
    file->addLargeMenu(exportMenu);

    auto *edit = category->addPanel(QStringLiteral("编辑"));
    edit->addSmallAction(actions.get(Undo));
    edit->addSmallAction(actions.get(Redo));
    edit->addLargeAction(actions.get(Reset));
    auto *cloud = category->addPanel(QStringLiteral("点云"));
    cloud->addSmallAction(actions.get(CropCloud));
    cloud->addSmallAction(actions.get(DeleteCloud));
    auto *more = menu(ribbon, QStringLiteral("更多"), actions, {AutoAlign, RemoveDuplicates}, QStringLiteral("more"));
    more->addMenu(
        menu(more, QStringLiteral("2D 识别精度"), actions, {PrecisionLow, PrecisionMedium, PrecisionHigh}, QString()));
    more->addSeparator();
    more->addAction(actions.get(Denoise));
    more->addAction(actions.get(SplitBoards));
    cloud->addLargeMenu(more);

    auto *reconstruct = category->addPanel(QStringLiteral("重建"));
    for (const auto id : {Parameters, Reconstruct, StopReconstruct, LocalReconstruct})
    {
        reconstruct->addLargeAction(actions.get(id));
    }
    auto *model = category->addPanel(QStringLiteral("模型修改"));
    model->addLargeMenu(
        menu(ribbon, QStringLiteral("选点建模"), actions, {BuildSelection, Obstacle}, QStringLiteral("cube")));
    auto *adjust =
        menu(ribbon, QStringLiteral("模型调整"), actions, {PlateThickness, ExtendContour}, QStringLiteral("cube"));
    adjust->addSeparator();
    adjust->addAction(actions.get(Channel));
    adjust->addAction(actions.get(Angle));
    adjust->addSeparator();
    adjust->addAction(actions.get(OuterPlate));
    adjust->addAction(actions.get(BottomPlate));
    adjust->addSeparator();
    adjust->addAction(actions.get(SteelStructure));
    model->addLargeMenu(adjust);
    model->addLargeMenu(
        menu(ribbon, QStringLiteral("添加孔"), actions, {WeldHole, BottomHole}, QStringLiteral("hole")));
    for (const auto id : {Fillet, Move, Rotate, DeleteModel})
    {
        model->addLargeAction(actions.get(id));
    }
    auto *tools = category->addPanel(QStringLiteral("工具"));
    for (const auto id : {Measure, BasePlane, Deviation})
    {
        tools->addSmallAction(actions.get(id));
    }
    auto *windowPanel = category->addPanel(QStringLiteral("工作区"));
    windowPanel->addLargeMenu(windows);
}

QWidget *createViewToolbar(QWidget *parent, WorkbenchActions &actions)
{
    using enum ActionId;
    auto *container = new QWidget(parent);
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(6);
    auto *toolbar = new QToolBar(container);
    toolbar->setObjectName(QStringLiteral("viewport.toolbar"));
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    toolbar->setMinimumWidth(1);
    for (const auto id : {Select, AddSelection, RemoveSelection})
    {
        auto *button = new QToolButton(toolbar);
        button->setDefaultAction(actions.get(id));
        button->setToolButtonStyle(Qt::ToolButtonIconOnly);
        toolbar->addWidget(button);
    }
    auto *filter = new QToolButton(toolbar);
    filter->setText(QStringLiteral("可选：点"));
    for (const auto id : {SelectPoint, SelectSolid})
    {
        QObject::connect(actions.get(id), &QAction::toggled, filter, [filter, &actions, id](bool checked)
        {
            if (checked) filter->setText(QStringLiteral("可选：") + actions.get(id)->text());
        });
    }
    filter->setPopupMode(QToolButton::InstantPopup);
    filter->setMenu(menu(toolbar, QStringLiteral("选择过滤"), actions,
                         {SelectPoint, SelectEdge, SelectFace, SelectSolid}, QString()));
    toolbar->addWidget(filter);
    toolbar->addSeparator();
    const auto split = [toolbar, &actions](ActionId action, std::initializer_list<ActionId> modes)
    {
        auto *button = new QToolButton(toolbar);
        button->setDefaultAction(actions.get(action));
        button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        button->setPopupMode(QToolButton::MenuButtonPopup);
        button->setMenu(menu(toolbar, QString(), actions, modes, QString()));
        toolbar->addWidget(button);
    };
    split(ShowCloud, {CloudHeight, CloudSolid});
    split(ShowModel, {ModelSolid, ModelWire});
    toolbar->addAction(actions.get(ShowThickness));
    auto *section = new QToolButton(toolbar);
    section->setText(QStringLiteral("剖视"));
    section->setIcon(WorkbenchTheme::icon(QStringLiteral("plane")));
    section->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    section->setPopupMode(QToolButton::InstantPopup);
    auto *sectionMenu = new QMenu(section);
    auto *card = new QWidget(sectionMenu);
    auto *form = new QFormLayout(card);
    auto *plane = new QToolButton(card);
    plane->setText(QStringLiteral("未选择平面"));
    plane->setEnabled(false);
    auto *offset = new QDoubleSpinBox(card);
    offset->setSuffix(QStringLiteral(" mm"));
    offset->setEnabled(false);
    form->addRow(QStringLiteral("剖视"), plane);
    form->addRow(QStringLiteral("偏移"), offset);
    form->addRow(new QLabel(QStringLiteral("剖切功能待接入"), card));
    auto *cardAction = new QWidgetAction(sectionMenu);
    cardAction->setDefaultWidget(card);
    sectionMenu->addAction(cardAction);
    section->setMenu(sectionMenu);
    toolbar->addWidget(section);
    toolbar->addSeparator();
    toolbar->addAction(actions.get(FitAll));
    layout->addWidget(toolbar, 1);
    auto *bottomView = new QToolButton(container);
    bottomView->setObjectName(QStringLiteral("button.bottomView"));
    bottomView->setDefaultAction(actions.get(BottomView));
    bottomView->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    bottomView->setIconSize(QSize(18, 18));
    layout->addWidget(bottomView);
    return container;
}
} // namespace reverse::workbench
