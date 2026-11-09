/*
 * 文件描述：
 * 定义工作台共用动作与能力标记，供 Ribbon、菜单和视图工具条复用。
 */
#pragma once

#include <array>

#include <QObject>

class QAction;

namespace reverse::workbench
{

enum class ActionId
{
    Open,
    ExportStep,
    ExportAndExit,
    ExportCloud,
    ExportModel,
    ExportModelCloud,
    Undo,
    Redo,
    Reset,
    CropCloud,
    DeleteCloud,
    Parameters,
    Reconstruct,
    StopReconstruct,
    LocalReconstruct,
    BuildByPoints,
    ObstacleByPoints,
    PlateThickness,
    ExtendContour,
    ChannelSteel,
    AngleSteel,
    OuterPlate,
    BottomPlate,
    CrossPlate,
    EndCapPlate,
    MirrorComplete,
    MirrorCompleteAll,
    WeldHole,
    BottomHole,
    Fillet,
    Move,
    Rotate,
    DeleteModel,
    Measure,
    BasePlane,
    Deviation,
    Select,
    AddSelection,
    RemoveSelection,
    SelectPoint,
    SelectEdge,
    SelectFace,
    SelectSolid,
    ShowCloud,
    CloudHeight,
    CloudSolid,
    ShowModel,
    ShowThickness,
    ModelSolid,
    ModelWire,
    Section,
    BottomView,
    CancelTask,
    StartRecording,
    StopRecording,
    ExecuteScript,
    Count
};

class WorkbenchActions final : public QObject
{
  public:
    explicit WorkbenchActions(QObject *parent);
    QAction *get(ActionId id) const;
    static constexpr std::array modelAdjustmentIds{
        ActionId::PlateThickness, ActionId::ExtendContour, ActionId::ChannelSteel, ActionId::AngleSteel, ActionId::OuterPlate,
        ActionId::BottomPlate, ActionId::CrossPlate, ActionId::EndCapPlate, ActionId::MirrorComplete, ActionId::MirrorCompleteAll};

  private:
    std::array<QAction *, static_cast<std::size_t>(ActionId::Count)> m_actions{};
};

} // namespace reverse::workbench
