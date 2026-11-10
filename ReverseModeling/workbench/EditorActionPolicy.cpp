/* 文件描述：集中定义编辑工具的互斥动作和选点向导的选择规则。 */
#include "workbench/EditorActionPolicy.h"

#include <initializer_list>

namespace reverse::workbench
{
namespace
{
void setEnabled(ActionOverrides &result, std::initializer_list<ActionId> actions, bool enabled)
{
    for (const auto action : actions)
    {
        result[static_cast<std::size_t>(action)] = enabled;
    }
}

void lockPickModes(ActionOverrides &result)
{
    setEnabled(result, {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid}, false);
}
} // namespace

ActionOverrides editorActionOverrides(const EditorActivity &activity)
{
    ActionOverrides result{};
    setEnabled(result, {ActionId::ShowModel}, !activity.transform);
    if (activity.transform)
    {
        lockPickModes(result);
        setEnabled(result, {ActionId::Select, ActionId::AddSelection, ActionId::RemoveSelection, ActionId::Measure,
                            ActionId::BasePlane, ActionId::DeleteModel, ActionId::CropCloud, ActionId::DeleteCloud}, false);
    }
    const bool editingPlate = activity.edge || activity.plateAdjustment || activity.fixedPickGeneration ||
                              activity.localReconstruction;
    if (editingPlate)
    {
        setEnabled(result, {ActionId::Measure, ActionId::Move, ActionId::Rotate, ActionId::BasePlane,
                            ActionId::DeleteModel, ActionId::ShowModel, ActionId::ExportStep, ActionId::ExportModel,
                            ActionId::ExportModelCloud, ActionId::ExportAndExit}, false);
    }
    if (activity.edge)
    {
        setEnabled(result, {ActionId::SelectPoint, ActionId::SelectFace, ActionId::SelectSolid}, false);
    }
    if (activity.plateAdjustment || activity.localReconstruction)
    {
        setEnabled(result, {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace}, false);
    }
    if (activity.fixedPickGeneration)
    {
        lockPickModes(result);
    }
    if (activity.directPlateEdit)
    {
        setEnabled(result, {ActionId::AddSelection, ActionId::RemoveSelection}, false);
    }
    if (activity.referencePlane)
    {
        lockPickModes(result);
        setEnabled(result, {ActionId::CropCloud, ActionId::DeleteCloud}, false);
    }
    return result;
}

ActionOverrides pointModelingActionOverrides(bool idle, bool selectingObstaclePoints, bool showingPreview)
{
    ActionOverrides result{};
    lockPickModes(result);
    setEnabled(result, {ActionId::Measure, ActionId::BasePlane, ActionId::DeleteModel, ActionId::CropCloud,
                        ActionId::DeleteCloud, ActionId::Section, ActionId::ShowThickness, ActionId::ExportAndExit,
                        ActionId::ShowCloud}, false);
    setEnabled(result, {ActionId::Select, ActionId::AddSelection, ActionId::RemoveSelection},
               idle && selectingObstaclePoints);
    setEnabled(result, {ActionId::ShowModel}, idle && selectingObstaclePoints && !showingPreview);
    return result;
}
} // namespace reverse::workbench
