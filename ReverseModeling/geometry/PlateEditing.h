/* 文件描述：板件间接触、切割、合并与修复的纯几何接口，输入拓扑保持只读。 */
#pragma once
#include "core/PlateEditing.h"
#include "geometry/Geometry.h"
#include <TopoDS_Shape.hxx>
#include <map>

namespace reverse
{
struct PlateEditResult
{
    std::shared_ptr<const Model> model;
    PlateEdit request;
    std::vector<std::string> reports;
    bool changed{};
};
PlateEditResult editModelPlates(const Model &model, PlateEdit request, std::stop_token stop = {},
                                const ProgressCallback &progress = {});
namespace detail
{
struct PlateShapeEdit
{
    std::map<std::string, std::vector<TopoDS_Shape>> replacements;
    std::vector<std::string> reports;
};
PlateShapeEdit editPlateShapes(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request,
                               std::stop_token stop);
TopoDS_Shape repairPlate(const TopoDS_Shape &shape, const PlateEdit &request, std::string &report);
PlateThicknessInfo inspectRepairPlateThickness(const TopoDS_Shape &shape);
std::vector<TopoDS_Shape> plateSolids(const TopoDS_Shape &shape);
double plateVolume(const TopoDS_Shape &shape);
TopoDS_Shape plateHalf(const TopoDS_Shape &shape, const PlateCut &cut);
void resolvePlateCrop(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request);
} // namespace detail
} // namespace reverse
