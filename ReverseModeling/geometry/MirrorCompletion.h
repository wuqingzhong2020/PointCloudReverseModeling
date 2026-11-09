/* 文件描述：板件镜像的精确变换、体积交叠检测及同板合并，全部在后台执行。 */
#pragma once
#include <stop_token>

#include <TopoDS_Shape.hxx>

#include "core/Types.h"
namespace reverse::detail
{
bool mirrorPartsConnected(const TopoDS_Shape &a, const TopoDS_Shape &b, double toleranceMm, std::stop_token stop);
TopoDS_Shape mirrorPlate(const TopoDS_Shape &source, const PlateThicknessInfo &plane);
double mirrorOverlapVolume(const TopoDS_Shape &a, const TopoDS_Shape &b, std::stop_token stop);
// 相接或重叠且能合并为单实体时返回合并板；分离时返回空形状。
TopoDS_Shape uniteMirrorPlate(const TopoDS_Shape &a, const TopoDS_Shape &b, std::stop_token stop);
} // namespace reverse::detail
