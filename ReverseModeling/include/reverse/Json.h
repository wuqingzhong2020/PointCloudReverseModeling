/*
 * 文件描述：
 * 将命令契约序列化为版本化 JSON，供命令行及未来通信适配使用，不包含显示大数组。
 */
#pragma once

#include "reverse/CommandService.h"
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
