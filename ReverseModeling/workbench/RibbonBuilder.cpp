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
#include "workbench/WorkbenchActionHelp.h"
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
        menu(ribbon, QStringLiteral("打开"), actions, {Open}, QStringLiteral("folder"));
    openMenu->addSeparator();
    openMenu->addMenu(recentFiles);
    file->addLargeAction(actions.get(Open), QToolButton::MenuButtonPopup);
    file->actionToRibbonToolButton(actions.get(Open))->setMenu(openMenu);
    auto *exportMenu = menu(ribbon, QStringLiteral("导出"), actions,
                            {ExportStep, ExportModel, ExportCloud, ExportModelCloud}, QStringLiteral("export"));
    setActionHelp(exportMenu->menuAction(), QStringLiteral("保存当前点云或三维模型。"),
                  QStringLiteral("点击展开菜单，选择导出 STEP/CWRM 模型、当前 PCD 点云或模型表面采样点云，再指定保存位置。"),
                  QStringLiteral("STEP/CWRM 和模型采样需要已有模型；当前点云导出需要已有点云。"));
    file->addLargeMenu(exportMenu);
    file->addLargeAction(actions.get(ExportAndExit));

    auto *edit = category->addPanel(QStringLiteral("编辑"));
    edit->addSmallAction(actions.get(Undo));
    edit->addSmallAction(actions.get(Redo));
    edit->addLargeAction(actions.get(Reset));
    auto *cloud = category->addPanel(QStringLiteral("点云"));
    cloud->addSmallAction(actions.get(CropCloud));
    cloud->addSmallAction(actions.get(DeleteCloud));

    auto *reconstruct = category->addPanel(QStringLiteral("重建"));
    for (const auto id : {Parameters, Reconstruct, StopReconstruct, LocalReconstruct})
    {
        reconstruct->addLargeAction(actions.get(id));
    }
    auto *model = category->addPanel(QStringLiteral("模型修改"));
    model->addLargeMenu(menu(ribbon, QStringLiteral("选点建模"), actions, {BuildByPoints, ObstacleByPoints}, QStringLiteral("cube")));
    auto *adjust = menu(ribbon, QStringLiteral("模型调整"), actions, {}, QStringLiteral("cube"));
    setActionHelp(adjust->menuAction(), QStringLiteral("修改板厚、外扩轮廓、设为槽钢/角钢、生成贴板/底板或镜像补全。"),
                  QStringLiteral("点击展开菜单并选择工具；“全部镜像补全”先选工件任意部件，再自动处理相连整组，其他工具按面板提示操作。"),
                  QStringLiteral("需要已有可见模型。钢结构是分组标题，不能点击。"));
    for (const auto id : WorkbenchActions::modelAdjustmentIds)
    {
        if (id == OuterPlate || id == ChannelSteel)
        {
            adjust->addSeparator();
        }
        else if (id == CrossPlate)
        {
            // 使用禁用文字项，避免当前菜单样式把带标题的 section 画成普通分隔线。
            adjust->addSeparator();
            auto *section = adjust->addAction(QStringLiteral("钢结构"));
            section->setEnabled(false);
        }
        adjust->addAction(actions.get(id));
    }
    model->addLargeMenu(adjust);
    auto *holeMenu = menu(ribbon, QStringLiteral("添加孔"), actions,
                          {WeldHole, BottomHole}, QStringLiteral("hole"));
    setActionHelp(holeMenu->menuAction(), QStringLiteral("给板件添加过焊孔或底边孔。"),
                  QStringLiteral("展开后选择孔类型，再选择板件、底边和尺寸；查看绿色孔位，预览后应用。"),
                  QStringLiteral("需要已有可见的等厚实体板件。"));
    model->addLargeMenu(holeMenu);
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
    windows->setToolTipsVisible(true);
    setActionHelp(windows->menuAction(), QStringLiteral("显示或隐藏工具窗口，恢复默认布局。"),
                  QStringLiteral("点击展开菜单，勾选重建参数、操作面板、模型树、属性或命令窗口。"),
                  QStringLiteral("隐藏窗口或恢复布局不会删除模型及点云。"));
    windowPanel->addLargeMenu(windows);
    auto *script = category->addPanel(QStringLiteral("脚本"));
    for (const auto id : {StartRecording, StopRecording, ExecuteScript})
    {
        script->addLargeAction(actions.get(id));
    }
}

QWidget *createViewToolbar(QWidget *parent, WorkbenchActions &actions, QMenu *sectionMenu)
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
    for (const auto id : {SelectPoint, SelectEdge, SelectFace, SelectSolid})
    {
        QObject::connect(actions.get(id), &QAction::toggled, filter, [filter, &actions, id](bool checked)
        {
            if (checked) filter->setText(QStringLiteral("可选：") + actions.get(id)->text());
        });
    }
    filter->setPopupMode(QToolButton::InstantPopup);
    auto *filterMenu = menu(toolbar, QStringLiteral("选择过滤"), actions,
                            {SelectPoint, SelectEdge, SelectFace, SelectSolid}, QString());
    setActionHelp(filterMenu->menuAction(), QStringLiteral("指定鼠标拾取点、线、面还是整个部件。"),
                  QStringLiteral("点击选择类型，再左键拾取；Shift 点击追加/取消。点模式还可拖框选择点云。"),
                  QStringLiteral("线、面、体需要可见模型；切换类型会清空已有选择。"));
    filter->setMenu(filterMenu);
    filter->setToolTip(filterMenu->menuAction()->toolTip());
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
    auto *sectionButton = new QToolButton(toolbar);
    sectionButton->setObjectName(QStringLiteral("button.section"));
    sectionButton->setDefaultAction(actions.get(Section));
    sectionButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sectionButton->setPopupMode(QToolButton::MenuButtonPopup);
    sectionButton->setMenu(sectionMenu);
    toolbar->addWidget(sectionButton);
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
