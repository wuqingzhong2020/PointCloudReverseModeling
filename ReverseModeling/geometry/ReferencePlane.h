/* 文件描述：三点定面、显示坐标转换及剖视半空间计算，不改变原始几何。 */
#pragma once

#include "core/ReferencePlane.h"

namespace reverse
{
[[nodiscard]] ReferencePlane makeReferencePlane(const std::array<Vec3, 3> &pointsMm);
[[nodiscard]] ReferencePlane localReferencePlane(const ReferencePlane &plane, const Vec3 &displayOriginMm);
// a*x+b*y+c*z+d >= 0 为可见半空间；传入的平面与待计算点必须使用同一坐标系。
[[nodiscard]] std::array<double, 4> sectionEquation(const ReferencePlane &plane, const PlaneSection &section);
}
