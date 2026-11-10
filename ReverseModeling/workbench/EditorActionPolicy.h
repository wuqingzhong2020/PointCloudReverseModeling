/* 文件描述：根据编辑会话限制工作台动作；纯值输入输出，不访问控件或修改工具状态。 */
#pragma once

#include <array>
#include <optional>

#include "workbench/WorkbenchActionId.h"

namespace reverse::workbench
{
struct EditorActivity
{
    bool transform{};
    bool edge{};
    bool plateAdjustment{};
    bool fixedPickGeneration{};
    bool localReconstruction{};
    bool directPlateEdit{};
    bool referencePlane{};
};

// 未设置表示保留工程/选区已有的可用性；false 是禁止，true 是明确允许。
using ActionOverrides = std::array<std::optional<bool>, static_cast<std::size_t>(ActionId::Count)>;

ActionOverrides editorActionOverrides(const EditorActivity &activity);
// 选点工具 sync 可能完成预览或退出，因此在 sync 后独立计算其覆盖规则。
ActionOverrides pointModelingActionOverrides(bool idle, bool selectingObstaclePoints, bool showingPreview);
} // namespace reverse::workbench
