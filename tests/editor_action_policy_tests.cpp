/* 文件描述：独立验证编辑动作互斥、工具退出后恢复和选点向导状态，无需启动窗口。 */
#include <gtest/gtest.h>

#include "workbench/EditorActionPolicy.h"

namespace
{
using namespace reverse::workbench;

std::optional<bool> enabled(const ActionOverrides &policy, ActionId action)
{
    return policy[static_cast<std::size_t>(action)];
}

TEST(EditorActionPolicy, IdlePreservesProjectAvailabilityAndRestoresModelVisibility)
{
    const auto policy = editorActionOverrides({});
    EXPECT_EQ(enabled(policy, ActionId::ShowModel), true);
    EXPECT_FALSE(enabled(policy, ActionId::ExportStep).has_value());
    EXPECT_FALSE(enabled(policy, ActionId::SelectSolid).has_value());
}

TEST(EditorActionPolicy, EdgeEditingKeepsEdgePickingButBlocksConflictingEditsAndExport)
{
    const auto policy = editorActionOverrides({.edge = true});
    EXPECT_FALSE(enabled(policy, ActionId::SelectEdge).has_value());
    EXPECT_EQ(enabled(policy, ActionId::SelectSolid), false);
    EXPECT_EQ(enabled(policy, ActionId::Move), false);
    EXPECT_EQ(enabled(policy, ActionId::ExportStep), false);
    EXPECT_EQ(enabled(policy, ActionId::ShowModel), false);
}

TEST(EditorActionPolicy, PlateAndLocalEditingKeepBodyPicking)
{
    for (const auto activity : {EditorActivity{.plateAdjustment = true}, EditorActivity{.localReconstruction = true}})
    {
        const auto policy = editorActionOverrides(activity);
        EXPECT_FALSE(enabled(policy, ActionId::SelectSolid).has_value());
        EXPECT_EQ(enabled(policy, ActionId::SelectEdge), false);
        EXPECT_EQ(enabled(policy, ActionId::ExportModelCloud), false);
    }
}

TEST(EditorActionPolicy, GenerationAndReferenceSelectionLockEveryPickMode)
{
    for (const auto activity : {EditorActivity{.fixedPickGeneration = true}, EditorActivity{.referencePlane = true}})
    {
        const auto policy = editorActionOverrides(activity);
        for (const auto id : {ActionId::SelectPoint, ActionId::SelectEdge, ActionId::SelectFace, ActionId::SelectSolid})
        {
            EXPECT_EQ(enabled(policy, id), false);
        }
    }
}

TEST(EditorActionPolicy, TransformAndDirectPlateEditBlockSelectionChanges)
{
    const auto moving = editorActionOverrides({.transform = true});
    EXPECT_EQ(enabled(moving, ActionId::Select), false);
    EXPECT_EQ(enabled(moving, ActionId::DeleteCloud), false);
    const auto direct = editorActionOverrides({.localReconstruction = true, .directPlateEdit = true});
    EXPECT_EQ(enabled(direct, ActionId::AddSelection), false);
    EXPECT_EQ(enabled(direct, ActionId::RemoveSelection), false);
    EXPECT_FALSE(enabled(direct, ActionId::Select).has_value());
}

TEST(EditorActionPolicy, ConcurrentRestrictionsAccumulateWithoutReenablingActions)
{
    const auto policy = editorActionOverrides({.edge = true, .referencePlane = true});
    EXPECT_EQ(enabled(policy, ActionId::SelectEdge), false);
    EXPECT_EQ(enabled(policy, ActionId::ExportStep), false);
    EXPECT_EQ(enabled(policy, ActionId::CropCloud), false);
}

TEST(EditorActionPolicy, ObstacleSelectionCanChangeSelectionOnlyWhenIdle)
{
    for (const bool idle : {false, true})
    {
        const auto policy = pointModelingActionOverrides(idle, true, false);
        EXPECT_EQ(enabled(policy, ActionId::Select), idle);
        EXPECT_EQ(enabled(policy, ActionId::AddSelection), idle);
        EXPECT_EQ(enabled(policy, ActionId::ShowModel), idle);
        EXPECT_EQ(enabled(policy, ActionId::ShowCloud), false);
        EXPECT_EQ(enabled(policy, ActionId::Section), false);
    }
}

TEST(EditorActionPolicy, PointPreviewRequiresModelVisibilityAndReferencePickingDisallowsRectangleSelection)
{
    EXPECT_EQ(enabled(pointModelingActionOverrides(true, true, true), ActionId::ShowModel), false);
    const auto pickingReference = pointModelingActionOverrides(true, false, false);
    EXPECT_EQ(enabled(pickingReference, ActionId::Select), false);
    EXPECT_EQ(enabled(pickingReference, ActionId::RemoveSelection), false);
}
} // namespace
