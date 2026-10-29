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
    OpenFolder,
    ImportParameters,
    ExportStep,
    ExportCloud,
    ExportModel,
    ExportParameters,
    Undo,
    Redo,
    Reset,
    CropCloud,
    DeleteCloud,
    AutoAlign,
    RemoveDuplicates,
    PrecisionLow,
    PrecisionMedium,
    PrecisionHigh,
    Denoise,
    SplitBoards,
    Parameters,
    Reconstruct,
    StopReconstruct,
    LocalReconstruct,
    BuildSelection,
    Obstacle,
    PlateThickness,
    ExtendContour,
    Channel,
    Angle,
    OuterPlate,
    BottomPlate,
    SteelStructure,
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
    ModelSolid,
    ModelWire,
    Section,
    FitAll,
    CancelTask,
    Count
};

class WorkbenchActions final : public QObject
{
  public:
    explicit WorkbenchActions(QObject *parent);
    QAction *get(ActionId id) const;

  private:
    std::array<QAction *, static_cast<std::size_t>(ActionId::Count)> m_actions{};
};

} // namespace reverse::workbench
