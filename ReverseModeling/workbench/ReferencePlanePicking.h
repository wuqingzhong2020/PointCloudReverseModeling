/* 文件描述：从已拾取的精确平面拓扑面取得基准面，拒绝曲面的三角化近似。 */
#pragma once

#include "core/ReferencePlane.h"
#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
[[nodiscard]] ReferencePlane referencePlaneFromFace(const ModelDisplayPart &part, std::size_t faceIndex);
}
