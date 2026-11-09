/* 文件描述：板件间接触、切割、合并与修复的纯几何接口，输入拓扑保持只读。 */
#pragma once
#include "core/PlateEditing.h"
#include "geometry/Geometry.h"
#include <TopoDS_Shape.hxx>
#include <map>

namespace reverse
{
EndCapInspection inspectModelEndCap(const Model &model, const std::string &part, std::stop_token stop = {});
struct PlateEditResult
{
    std::shared_ptr<const Model> model;
    PlateEdit request;
    std::vector<std::string> reports;
    bool changed{};
};
PlateEdit prepareDirectPlateEdit(const Model &model, PlateEdit request, bool all, std::stop_token stop,
                            const std::function<void(std::string_view)> &output = {});
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
PlateShapeEdit optimizeCrossPlateShapes(const std::map<std::string, TopoDS_Shape> &shapes,
                                       const PlateEdit &request, std::stop_token stop);
EndCapInspection inspectEndCapShapes(const std::map<std::string, TopoDS_Shape> &shapes,
                                     const std::string &part, std::stop_token stop);
PlateShapeEdit optimizeEndCapShapes(const std::map<std::string, TopoDS_Shape> &shapes,
                                    const PlateEdit &request, std::stop_token stop);
TopoDS_Shape repairPlate(const TopoDS_Shape &shape, const PlateEdit &request, std::string &report);
PlateThicknessInfo inspectRepairPlateThickness(const TopoDS_Shape &shape);
std::vector<TopoDS_Shape> plateSolids(const TopoDS_Shape &shape);
double plateVolume(const TopoDS_Shape &shape);
TopoDS_Shape plateHalf(const TopoDS_Shape &shape, const PlateCut &cut);
void resolvePlateCrop(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request);
void prepareDirectPlateEdit(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request,
                            bool all, std::stop_token stop,
                            const std::function<void(std::string_view)> &output = {});
} // namespace detail
} // namespace reverse
