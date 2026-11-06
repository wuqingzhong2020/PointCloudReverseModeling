/*
 * 文件描述：从等厚板两个平行主面读取真实板厚，以完整主面（含内孔）重新拉伸。
 * 固定中面，仅沿厚度方向改变尺寸；复制拓扑，保留平面轮廓及贯穿孔，不修改输入实体。
 */
#include <algorithm>
#include <cmath>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GProp_GProps.hxx>
#include <gp_Trsf.hxx>

#include "geometry/Geometry.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateThickness.h"

namespace reverse::detail
{
namespace
{
gp_Vec thicknessVector(const PlateProfile &profile)
{
    const gp_Vec normal(profile.plane.Axis().Direction());
    const gp_Pnt back = BRepAdaptor_Surface(profile.back).Plane().Location();
    return normal * gp_Vec(profile.plane.Location(), back).Dot(normal);
}
} // namespace

PlateThicknessInfo inspectPlateThickness(const TopoDS_Shape &shape)
{
    if (shape.ShapeType() != TopAbs_SOLID)
    {
        throw GeometryError("not_solid", "修改板厚需要等厚实体板件，不能修改观测面或开放曲面。");
    }
    const auto profile = plateProfile(shape);
    const gp_Vec direction = thicknessVector(profile);
    GProp_GProps area;
    BRepGProp::SurfaceProperties(profile.front, area);
    const gp_Pnt center = area.CentreOfMass().Translated(direction * 0.5);
    const gp_Vec normal = direction.Normalized();
    return {profile.thickness, {center.X(), center.Y(), center.Z()}, {normal.X(), normal.Y(), normal.Z()}};
}

TopoDS_Shape resizePlateThickness(const TopoDS_Shape &source, double thicknessMm)
{
    if (!std::isfinite(thicknessMm) || thicknessMm < 0.001 || thicknessMm > 1e6)
    {
        throw GeometryError("invalid_thickness", "参数 --thickness 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    (void)inspectPlateThickness(source);
    const TopoDS_Shape copy = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto profile = plateProfile(copy);
    const gp_Vec normal = thicknessVector(profile).Normalized();
    gp_Trsf shift;
    shift.SetTranslation(normal * ((profile.thickness - thicknessMm) * 0.5));
    const TopoDS_Shape face = BRepBuilderAPI_Transform(profile.front, shift, true).Shape();
    BRepPrimAPI_MakePrism prism(face, normal * thicknessMm, true);
    if (!prism.IsDone() || prism.Shape().IsNull() || prism.Shape().ShapeType() != TopAbs_SOLID ||
        !BRepCheck_Analyzer(prism.Shape()).IsValid())
    {
        throw GeometryError("thickness_failed", "板厚修改未生成有效实体，整批未应用。");
    }
    // 体积应仅随板厚成比例变化；可同时检出内孔或轮廓意外丢失。
    GProp_GProps before, after;
    BRepGProp::VolumeProperties(source, before);
    BRepGProp::VolumeProperties(prism.Shape(), after);
    const double expected = before.Mass() * thicknessMm / profile.thickness;
    if (after.Mass() <= 0 || std::abs(after.Mass() - expected) > std::max(1e-4, expected * 1e-6))
    {
        throw GeometryError("thickness_failed", "修改板厚后的体积与轮廓不一致，整批未应用。");
    }
    return prism.Shape();
}
} // namespace reverse::detail
