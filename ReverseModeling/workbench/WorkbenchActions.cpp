/*
 * 文件描述：
 * 创建工作台动作、稳定对象名、快捷键和能力提示；未接入的业务动作保持禁用，
 * 表单预览动作只负责打开操作面板，实际服务执行由主窗口连接。
 */
#include <initializer_list>

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
enum class Capability
{
    Available,
    Preview,
    Pending
};
struct ActionSpec
{
    ActionId id;
    const char *name;
    const char *label;
    const char *icon;
    Capability capability;
};
} // namespace

WorkbenchActions::WorkbenchActions(QObject *parent)
    : QObject(parent)
{
    using enum ActionId;
    using enum Capability;
    const ActionSpec specs[] = {{Open, "open", "打开", "folder", Available},
                                {OpenFolder, "openFolder", "打开文件夹", "folder", Pending},
                                {ImportParameters, "importParameters", "导入重建参数", "parameters", Pending},
                                {ExportStep, "exportStep", "导出 STEP", "export", Available},
                                {ExportCloud, "exportCloud", "导出点云 (*.pcd)", "cloud", Pending},
                                {ExportModel, "exportModel", "导出工程模型", "cube", Pending},
                                {ExportParameters, "exportParameters", "导出重建参数", "parameters", Pending},
                                {Undo, "undo", "撤销", "undo", Pending},
                                {Redo, "redo", "重做", "redo", Pending},
                                {Reset, "reset", "重置", "reset", Pending},
                                {CropCloud, "cropCloud", "截取点云", "crop", Pending},
                                {DeleteCloud, "deleteCloud", "删除点云", "delete", Pending},
                                {AutoAlign, "autoAlign", "自动对齐", "align", Pending},
                                {RemoveDuplicates, "removeDuplicates", "拼接时去重", "cloud", Pending},
                                {PrecisionLow, "precisionLow", "低", "", Pending},
                                {PrecisionMedium, "precisionMedium", "中", "", Pending},
                                {PrecisionHigh, "precisionHigh", "高", "", Pending},
                                {Denoise, "denoise", "AI 去噪", "cloud", Pending},
                                {SplitBoards, "splitBoards", "AI 辅助分板", "cube", Pending},
                                {Parameters, "parameters", "重建参数", "parameters", Available},
                                {Reconstruct, "reconstruct", "开始重建", "play", Available},
                                {StopReconstruct, "stopReconstruct", "停止重建", "stop", Available},
                                {LocalReconstruct, "localReconstruct", "局部重建", "reset", Pending},
                                {BuildSelection, "buildSelection", "选点建模", "cube", Preview},
                                {Obstacle, "obstacle", "选点生成障碍物", "cube", Preview},
                                {PlateThickness, "plateThickness", "修改板厚", "parameters", Preview},
                                {ExtendContour, "extendContour", "外拉板件轮廓", "cube", Preview},
                                {Channel, "channel", "设为槽钢", "cube", Preview},
                                {Angle, "angle", "设为角钢", "cube", Preview},
                                {OuterPlate, "outerPlate", "生成外扩贴板", "cube", Preview},
                                {BottomPlate, "bottomPlate", "生成下底板", "cube", Preview},
                                {SteelStructure, "steelStructure", "钢结构", "cube", Pending},
                                {WeldHole, "weldHole", "过焊孔", "hole", Preview},
                                {BottomHole, "bottomHole", "底边孔", "hole", Preview},
                                {Fillet, "fillet", "圆角/倒角", "cube", Preview},
                                {Move, "move", "移动", "move", Preview},
                                {Rotate, "rotate", "旋转", "rotate", Preview},
                                {DeleteModel, "deleteModel", "删除模型", "delete", Pending},
                                {Measure, "measure", "测量", "measure", Preview},
                                {BasePlane, "basePlane", "基准面", "plane", Preview},
                                {Deviation, "deviation", "计算偏差", "measure", Available},
                                {Select, "select", "选择", "cursor", Pending},
                                {AddSelection, "addSelection", "新增选择", "cursor", Pending},
                                {RemoveSelection, "removeSelection", "移除选择", "cursor", Pending},
                                {SelectPoint, "selectPoint", "点", "", Pending},
                                {SelectEdge, "selectEdge", "线", "", Pending},
                                {SelectFace, "selectFace", "面", "", Pending},
                                {SelectSolid, "selectSolid", "体", "cube", Pending},
                                {ShowCloud, "showCloud", "点云", "cloud", Available},
                                {CloudHeight, "cloudHeight", "高度渐变（待接入）", "", Pending},
                                {CloudSolid, "cloudSolid", "纯色", "", Available},
                                {ShowModel, "showModel", "实体", "cube", Available},
                                {ModelSolid, "modelSolid", "实体", "", Available},
                                {ModelWire, "modelWire", "线框（待接入）", "", Pending},
                                {Section, "section", "剖视", "plane", Preview},
                                {FitAll, "fitAll", "适合视图", "fit", Available},
                                {CancelTask, "cancelTask", "取消任务", "stop", Available}};
    for (const auto &spec : specs)
    {
        auto *action =
            new QAction(WorkbenchTheme::icon(QString::fromLatin1(spec.icon)), QString::fromUtf8(spec.label), this);
        action->setObjectName(QStringLiteral("action.") + QString::fromLatin1(spec.name));
        action->setProperty("capability", static_cast<int>(spec.capability));
        if (spec.capability == Pending)
        {
            action->setEnabled(false);
            action->setToolTip(action->text() + QStringLiteral("：功能待接入"));
        }
        else if (spec.capability == Preview)
        {
            action->setToolTip(action->text() + QStringLiteral("：打开参数面板，几何操作待接入"));
        }
        m_actions[static_cast<std::size_t>(spec.id)] = action;
    }
    const auto exclusive = [this](std::initializer_list<ActionId> ids, ActionId selected)
    {
        auto *group = new QActionGroup(this);
        for (const auto id : ids)
        {
            get(id)->setCheckable(true);
            group->addAction(get(id));
        }
        get(selected)->setChecked(true);
    };
    exclusive({Select, AddSelection, RemoveSelection}, Select);
    exclusive({SelectPoint, SelectEdge, SelectFace, SelectSolid}, SelectSolid);
    exclusive({PrecisionLow, PrecisionMedium, PrecisionHigh}, PrecisionMedium);
    exclusive({CloudHeight, CloudSolid}, CloudSolid);
    exclusive({ModelSolid, ModelWire}, ModelSolid);
    for (const auto id : {ShowCloud, ShowModel, AutoAlign, RemoveDuplicates})
    {
        get(id)->setCheckable(true);
        get(id)->setChecked(id == ShowCloud || id == ShowModel);
    }
    get(Open)->setShortcut(QKeySequence::Open);
    get(ExportStep)->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    get(Parameters)->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    get(FitAll)->setShortcut(QKeySequence(QStringLiteral("F")));
}

QAction *WorkbenchActions::get(ActionId id) const
{
    return m_actions.at(static_cast<std::size_t>(id));
}
} // namespace reverse::workbench
