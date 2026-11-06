/* 文件描述：将等厚板件的轮廓选边转换为贯穿板厚的角边，保持板厚不变。 */
#pragma once

#include <cstdint>
#include <vector>

#include <TopoDS_Face.hxx>
#include <gp_Pln.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Shape.hxx>

namespace reverse::detail
{
struct PlateProfile
{
    TopoDS_Face front, back;
    gp_Pln plane;
    double thickness{};
};
PlateProfile plateProfile(const TopoDS_Shape &shape);
std::vector<TopoDS_Edge> plateOutlineCornerEdges(const TopoDS_Shape &shape,
                                               const std::vector<std::uint32_t> &indices);
}
