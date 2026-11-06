/*
 * 文件描述：复制板件，以主面的外环进行平面等距偏移，直边延伸相交，封闭内孔不参与偏移。
 * 沿原厚度方向重新拉伸，校验实体、体积及原实体包含关系；失败时不改变输入拓扑。
 */
#include <algorithm>
#include <cmath>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>

#include "geometry/Geometry.h"
#include "geometry/PlateContour.h"
#include "geometry/PlateOutlineTreatment.h"

namespace reverse::detail
{
TopoDS_Shape expandPlateContour(const TopoDS_Shape &source, double distanceMm)
{
    if (!std::isfinite(distanceMm) || distanceMm < 0.001 || distanceMm > 1e6)
    {
        throw GeometryError("invalid_distance", "参数 --distance 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    if (source.ShapeType() != TopAbs_SOLID)
    {
        throw GeometryError("not_solid", "外扩轮廓需要等厚实体板件，不能修改观测面或开放曲面。");
    }
    const auto copy = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto profile = plateProfile(copy);
    const auto front = TopoDS::Face(profile.front.Oriented(TopAbs_FORWARD));
    const auto outer = BRepTools::OuterWire(front);
    BRepOffsetAPI_MakeOffset offset(BRepBuilderAPI_MakeFace(profile.plane, outer, true).Face(), GeomAbs_Intersection);
    offset.Perform(distanceMm);
    if (!offset.IsDone() || offset.Shape().IsNull())
    {
        throw GeometryError("contour_failed", "外轮廓偏移失败，请减小外扩距离或检查轮廓。");
    }
    TopoDS_Wire expanded;
    int wires = 0;
    for (TopExp_Explorer it(offset.Shape(), TopAbs_WIRE); it.More(); it.Next())
    {
        expanded = TopoDS::Wire(it.Current());
        ++wires;
    }
    if (wires != 1 || !expanded.Closed())
    {
        throw GeometryError("contour_failed", "外扩后轮廓分裂或产生额外闭环，请减小距离；整批未应用。");
    }
    BRepBuilderAPI_MakeFace face(profile.plane, expanded, true);
    for (TopExp_Explorer it(front, TopAbs_WIRE); it.More(); it.Next())
    {
        if (!it.Current().IsSame(outer))
        {
            face.Add(TopoDS::Wire(it.Current()));
        }
    }
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
    {
        throw GeometryError("contour_failed", "外扩后轮廓自交或与内孔冲突，整批未应用。");
    }
    const gp_Vec normal(profile.plane.Axis().Direction());
    const gp_Pnt back = BRepAdaptor_Surface(profile.back).Plane().Location();
    const gp_Vec thickness = normal * gp_Vec(profile.plane.Location(), back).Dot(normal);
    BRepPrimAPI_MakePrism prism(face.Face(), thickness, true);
    if (!prism.IsDone() || prism.Shape().IsNull() || prism.Shape().ShapeType() != TopAbs_SOLID ||
        !BRepCheck_Analyzer(prism.Shape()).IsValid())
    {
        throw GeometryError("contour_failed", "外扩未生成有效板件实体，整批未应用。");
    }
    GProp_GProps before, after, area;
    BRepGProp::VolumeProperties(source, before);
    BRepGProp::VolumeProperties(prism.Shape(), after);
    BRepGProp::SurfaceProperties(face.Face(), area);
    const double tolerance = std::max(1e-4, after.Mass() * 1e-6);
    if (after.Mass() <= before.Mass() || std::abs(after.Mass() - area.Mass() * profile.thickness) > tolerance)
    {
        throw GeometryError("contour_failed", "外扩结果的面积或板厚不正确，整批未应用。");
    }
    // 防止方向反转、复杂凹口偏移错误造成原有材料丢失。布尔校验不修改历史模型。
    BRepAlgoAPI_Cut remainder;
    remainder.SetNonDestructive(true);
    remainder.SetArguments({copy});
    remainder.SetTools({prism.Shape()});
    remainder.Build();
    if (!remainder.IsDone())
    {
        throw GeometryError("contour_failed", "无法验证外扩结果，整批未应用。");
    }
    GProp_GProps lost;
    BRepGProp::VolumeProperties(remainder.Shape(), lost);
    if (std::abs(lost.Mass()) > tolerance)
    {
        throw GeometryError("contour_failed", "外扩结果未包含原板件，请检查轮廓，整批未应用。");
    }
    return prism.Shape();
}
} // namespace reverse::detail
