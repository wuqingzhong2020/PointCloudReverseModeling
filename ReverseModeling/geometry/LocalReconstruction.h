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
// 按当前实体的完整占用范围生成定向盒；不要求原始点云或板厚。
TopoDS_Shape rebuildOccupancyBox(const TopoDS_Shape &shape, const BeamFrame &frame, std::stop_token stop);
LocalReconstructionShape rebuildLocalShape(const std::vector<Vec3> &points, const LocalPlateSource &source,
                                           std::optional<double> thickness, const LocalReconstruction &request,
                                           std::stop_token stop);
} // namespace reverse::detail
