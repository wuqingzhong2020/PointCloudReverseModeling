/*
 * 文件描述：
 * 声明 H 型钢初始参考参数策略，为桌面工作台提供可编辑的参考值。
 */
#pragma once

#include "reverse/Types.h"
#include <optional>

namespace reverse {
// 仅提供可编辑的初始值；过小、非有限或超出支持范围的包围尺寸不生成建议。
[[nodiscard]] std::optional<BeamParameterSuggestion> suggestBeamParameters(const BeamEstimate& estimate);
} // namespace reverse
