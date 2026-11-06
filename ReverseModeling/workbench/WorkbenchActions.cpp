/*
 * 文件描述：
 * 创建工作台动作、稳定对象名、快捷键和能力提示；未接入的业务动作保持禁用，
 * 实际服务执行由主窗口连接。
 */
#include <initializer_list>

#include <QAction>
#include <QActionGroup>
#include <QKeySequence>

#include "workbench/WorkbenchActionHelp.h"
#include "workbench/WorkbenchActions.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
enum class Capability
{
    Available,
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
                                {ExportAndExit, "exportAndExit", "导出并退出", "export", Available},
                                {ExportCloud, "exportCloud", "导出点云 (*.pcd)", "cloud", Available},
                                {ExportModel, "exportModel", "导出模型 (*.cwrm)", "cube", Available},
                                {ExportModelCloud, "exportModelCloud", "模型采样导出 (*.pcd)", "cloud", Available},
                                {Undo, "undo", "撤销", "undo", Available},
                                {Redo, "redo", "重做", "redo", Available},
                                {Reset, "reset", "重置", "reset", Available},
                                {CropCloud, "cropCloud", "截取点云", "crop", Available},
                                {DeleteCloud, "deleteCloud", "删除点云", "delete", Available},
                                {Parameters, "parameters", "重建参数", "parameters", Available},
                                {Reconstruct, "reconstruct", "开始重建", "play", Available},
                                {StopReconstruct, "stopReconstruct", "停止重建", "stop", Available},
                                {LocalReconstruct, "localReconstruct", "局部重建", "reset", Available},
                                {PlateThickness, "plateThickness", "修改板厚", "parameters", Available},
                                {ExtendContour, "extendContour", "外扩板件轮廓", "cube", Available},
                                {OuterPlate, "outerPlate", "生成外扩贴板", "cube", Available},
                                {BottomPlate, "bottomPlate", "生成下底板", "cube", Available},
                                {MirrorComplete, "mirrorComplete", "镜像补全", "cube", Available},
                                {WeldHole, "weldHole", "过焊孔", "hole", Available},
                                {BottomHole, "bottomHole", "底边孔", "hole", Available},
                                {Fillet, "fillet", "圆角/倒角", "cube", Available},
                                {Move, "move", "移动", "move", Available},
                                {Rotate, "rotate", "旋转", "rotate", Available},
                                {DeleteModel, "deleteModel", "删除模型", "delete", Available},
                                {Measure, "measure", "测量", "measure", Available},
                                {BasePlane, "basePlane", "基准面", "plane", Available},
                                {Deviation, "deviation", "计算偏差", "measure", Available},
                                {Select, "select", "选择", "cursor", Available},
                                {AddSelection, "addSelection", "新增选择", "cursor-add", Available},
                                {RemoveSelection, "removeSelection", "移除选择", "cursor-remove", Available},
                                {SelectPoint, "selectPoint", "点", "", Available},
                                {SelectEdge, "selectEdge", "线", "", Available},
                                {SelectFace, "selectFace", "面", "", Available},
                                {SelectSolid, "selectSolid", "体", "cube", Available},
                                {ShowCloud, "showCloud", "点云", "cloud", Available},
                                {CloudHeight, "cloudHeight", "高度渐变", "", Available},
                                {CloudSolid, "cloudSolid", "纯色", "", Available},
                                {ShowModel, "showModel", "实体", "cube", Available},
                                {ShowThickness, "showThickness", "板厚", "parameters", Available},
                                {ModelSolid, "modelSolid", "实体", "", Available},
                                {ModelWire, "modelWire", "线框", "", Available},
                                {Section, "section", "剖视", "plane", Available},
                                {BottomView, "bottomView", "上视图", "home", Available},
                                {CancelTask, "cancelTask", "取消任务", "stop", Available},
                                {StartRecording, "startRecording", "开始录制", "record", Available},
                                {StopRecording, "stopRecording", "停止录制", "stop", Available},
                                {ExecuteScript, "executeScript", "执行脚本", "script", Available}};
    for (const auto &spec : specs)
    {
        auto *action =
            new QAction(WorkbenchTheme::icon(QString::fromLatin1(spec.icon)), QString::fromUtf8(spec.label), this);
        action->setObjectName(QStringLiteral("action.") + QString::fromLatin1(spec.name));
        action->setProperty("capability", static_cast<int>(spec.capability));
        if (spec.capability == Pending)
        {
            action->setEnabled(false);
        }
        m_actions[static_cast<std::size_t>(spec.id)] = action;
    }
    get(Measure)->setCheckable(true);
    get(Section)->setCheckable(true);
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
    exclusive({SelectPoint, SelectEdge, SelectFace, SelectSolid}, SelectPoint);
    exclusive({CloudHeight, CloudSolid}, CloudHeight);
    exclusive({ModelSolid, ModelWire}, ModelSolid);
    for (const auto id : {ShowCloud, ShowModel, ShowThickness})
    {
        get(id)->setCheckable(true);
        get(id)->setChecked(id == ShowCloud || id == ShowModel || id == ShowThickness);
    }
    get(Open)->setShortcut(QKeySequence::Open);
    get(ExportStep)->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    get(Parameters)->setShortcut(QKeySequence(QStringLiteral("Ctrl+P")));
    initializeActionHelp(*this);
}

QAction *WorkbenchActions::get(ActionId id) const
{
    return m_actions.at(static_cast<std::size_t>(id));
}
} // namespace reverse::workbench
