/* 文件描述：CamAssemblyWelding 板件算法依赖的四个原始拓扑查询接口。 */
#pragma once
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <gp_Vec.hxx>
namespace reverse::detail::assembly::OccTopologyUtils
{
bool twoLargestFaces(const TopoDS_Shape &, TopoDS_Face &, TopoDS_Face &);
bool tryCalculateThickness(const TopoDS_Shape &, double &);
bool tryFaceNormal(const TopoDS_Face &, gp_Vec &);
double faceArea(const TopoDS_Face &);
}
