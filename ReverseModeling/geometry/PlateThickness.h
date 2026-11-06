/* 文件描述：等厚板实体的厚度检测与保持中面不动的重新拉伸。 */
#pragma once

#include <TopoDS_Shape.hxx>

#include "core/Types.h"

namespace reverse::detail
{
PlateThicknessInfo inspectPlateThickness(const TopoDS_Shape &shape);
TopoDS_Shape resizePlateThickness(const TopoDS_Shape &shape, double thicknessMm);
} // namespace reverse::detail
