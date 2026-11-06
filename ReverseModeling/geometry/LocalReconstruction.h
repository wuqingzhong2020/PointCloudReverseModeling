/* 文件描述：局部点云在指定板面上的轮廓拟合；生成独立几何，不提交工程。 */
#pragma once

#include <stop_token>
#include <vector>

#include <TopoDS_Shape.hxx>

#include "core/Types.h"

namespace reverse::detail
{
struct LocalPlateSource
{
    RecognizedPlate plate;
    BeamFrame frame;
    std::vector<std::uint64_t> indices; // 原始文件点索引，裁剪后仍稳定。
};
struct LocalReconstructionShape
{
    TopoDS_Shape shape;
    std::optional<PlateThicknessInfo> thickness;
};
LocalReconstructionShape rebuildLocalShape(const std::vector<Vec3> &points, const LocalPlateSource &source,
                                           std::optional<double> thickness, const LocalReconstruction &request,
                                           std::stop_token stop);
} // namespace reverse::detail
