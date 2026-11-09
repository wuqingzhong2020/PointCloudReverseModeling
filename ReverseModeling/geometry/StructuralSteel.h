/* 文件描述：由矩形实体和开口侧面建立等壁厚槽钢/角钢，并读取其真实截面。 */
#pragma once
#include "core/Types.h"
#include <TopoDS_Shape.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <optional>
#include <vector>

namespace reverse::detail
{
struct SteelSection
{
    StructuralSteelKind kind{};
    gp_Pnt origin;
    gp_Dir along, inward, up;
    double length{}, width{}, height{}, thickness{};
};
TopoDS_Shape buildSteelSection(const SteelSection &section);
SteelSection structuralSteelSection(const TopoDS_Shape &source, const StructuralSteel &request,
                                    const std::vector<TopoDS_Shape> &supports);
std::optional<SteelSection> inspectSteelSection(const TopoDS_Shape &shape);
PlateThicknessInfo steelThickness(const SteelSection &section);
} // namespace reverse::detail
