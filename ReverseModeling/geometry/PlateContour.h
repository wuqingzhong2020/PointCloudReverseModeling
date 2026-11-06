/* 文件描述：等厚板外轮廓的平面外扩，保持板厚和封闭内孔。 */
#pragma once

#include <TopoDS_Shape.hxx>

namespace reverse::detail
{
TopoDS_Shape expandPlateContour(const TopoDS_Shape &source, double distanceMm);
}
