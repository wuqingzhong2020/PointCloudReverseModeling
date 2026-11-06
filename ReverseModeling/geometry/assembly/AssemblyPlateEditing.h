/* 文件描述：将 CamAssemblyWelding 的板件算法接入工程快照、撤销与回放。 */
#pragma once
#include "geometry/PlateEditing.h"
namespace reverse::detail
{
void prepareAssemblyPlateEdit(const std::map<std::string, TopoDS_Shape> &, PlateEdit &, std::stop_token);
PlateShapeEdit editAssemblyPlateShapes(const std::map<std::string, TopoDS_Shape> &, PlateEdit &, std::stop_token);
PlateThicknessInfo assemblyPlateThickness(const TopoDS_Shape &);
}
