/* 文件描述：从未被工件几何覆盖的扫描点构建独立、封闭的避障包络。 */
#pragma once
#include <stop_token>
#include <vector>
#include <TopoDS_Shape.hxx>
#include "core/Types.h"
namespace reverse::detail
{
struct ObstacleShape
{
    TopoDS_Shape shape;
    ObstacleInfo info;
};
std::vector<ObstacleShape> reconstructObstacleShapes(const CloudData &cloud, const MeshData &workpiece,
    const std::vector<bool> &solidParts, double spacingMm, double noiseMm,
    std::stop_token stop, const ProgressCallback &progress, const PlateRecognition *recognition = nullptr,
    double exclusionDistanceMm = 1.0);
}
