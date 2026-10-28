/*
 * 文件描述：
 * 提供命令契约的版本化 JSON 序列化与参数解析，不包含显示大数组。
 */
#pragma once

#include "commands/CommandService.h"
#include <nlohmann/json.hpp>
#include <string_view>

namespace reverse {
[[nodiscard]] const char* taskStateName(TaskState state) noexcept;
[[nodiscard]] LengthUnit parseLengthUnit(std::string_view value);
[[nodiscard]] nlohmann::json toJson(const TaskSnapshot& task);
[[nodiscard]] nlohmann::json toJson(const ProjectSnapshot& project);
[[nodiscard]] BeamParameters parseBeamParameters(const nlohmann::json& value);
[[nodiscard]] std::optional<BeamFrame> parseFrameOverride(const nlohmann::json& value);
} // namespace reverse
