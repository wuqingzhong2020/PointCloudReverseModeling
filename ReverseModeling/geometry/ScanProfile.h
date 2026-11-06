/* 文件描述：拟合俯视扫描可见顶边的直线/圆弧，并将闭合板面轮廓拉伸为等厚实体。 */
#pragma once

#include <span>
#include <stop_token>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "core/Types.h"

namespace reverse::detail
{
std::vector<PlateBoundarySegment> fitScanProfile(std::span<const Vec3> points, double toleranceMm,
                                                std::stop_token stop = {});
TopoDS_Shape buildScanProfile(const RecognizedPlate &plate);
}
