/* 文件描述：从工程状态解析底板面、所选剖视面和右上角定位平面，供服务及视图共用。 */
#pragma once

#include <optional>

#include "core/ReferencePlane.h"

namespace reverse
{
struct ProjectSnapshot;
enum class PositioningView { Top, Bottom, Section };
[[nodiscard]] PositioningView positioningView(const ProjectSnapshot &project);
[[nodiscard]] std::optional<ReferencePlane> bottomSectionPlane(const ProjectSnapshot &project);
[[nodiscard]] std::optional<ReferencePlane> selectedSectionPlane(const ProjectSnapshot &project, SectionPlaneSource source);
// 剖视优先；未重建使用工程 XY 上视图，已有本地重建模型使用底板面。
[[nodiscard]] std::optional<ReferencePlane> positioningPlane(const ProjectSnapshot &project);
} // namespace reverse
