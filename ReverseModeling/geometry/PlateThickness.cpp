/*
 * 文件描述：从板件两个平行主面读取真实板厚；直棱柱重新拉伸，样条/斜切板沿主面法线伸缩。
 * 固定中面，仅沿厚度方向改变尺寸；复制拓扑，保留平面轮廓及贯穿孔，不修改输入实体。
 */
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_GTransform.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GeomLib_IsPlanarSurface.hxx>
#include <Geom_BSplineSurface.hxx>
#include <Geom_BezierSurface.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Ax2.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Trsf.hxx>

#include "geometry/Geometry.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateThickness.h"
#include "geometry/StructuralSteel.h"

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
// CWRM 的扫描轮廓可能有样条侧面和斜接端面，不能用“所有侧面必须垂直”判断板厚。
// 寻找包住整个实体的两张平行主面；不以轴对齐包围盒的最短边代替板厚。
struct ThicknessProfile
{
    PlateProfile plate;
    gp_Vec direction;
    bool straightPrism{};
};

// 检查样条控制点的共面性，避免大坐标下解析平面转样条后被误判为曲面。
template <class Surface>
std::optional<gp_Pln> splinePlane(const Surface &surface, double tolerance)
{
    std::vector<gp_Pnt> points;
    for (int u = 1; u <= surface->NbUPoles(); ++u)
        for (int v = 1; v <= surface->NbVPoles(); ++v) points.push_back(surface->Pole(u, v));
    const auto origin = points.front();
    const auto farthest = *std::max_element(points.begin(), points.end(), [&](const auto &a, const auto &b) {
        return origin.SquareDistance(a) < origin.SquareDistance(b);
    });
    const gp_Vec axis(origin, farthest);
    gp_Vec normal;
    for (const auto &point : points)
    {
        const gp_Vec candidate = axis.Crossed(gp_Vec(origin, point));
        if (candidate.SquareMagnitude() > normal.SquareMagnitude()) normal = candidate;
    }
    if (normal.SquareMagnitude() <= 1e-20) return std::nullopt;
    const gp_Pln plane(origin, gp_Dir(normal));
    for (const auto &point : points) if (plane.Distance(point) > tolerance) return std::nullopt;
    return plane;
}

ThicknessProfile thicknessProfile(const TopoDS_Shape &shape)
{
    try
    {
        const auto plate = plateProfile(shape);
        return {plate, thicknessVector(plate), true};
    }
    catch (const GeometryError &error)
    {
        if (error.code() != "unsupported_plate_outline") throw;
    }
    struct PlanarFace { TopoDS_Face face; gp_Pln plane; double area; };
    std::vector<PlanarFace> faces;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        std::optional<gp_Pln> plane;
        if (surface.GetType() == GeomAbs_Plane) plane = surface.Plane();
        else
        {
            const double tolerance = std::max(1e-7, BRep_Tool::Tolerance(face));
            if (surface.GetType() == GeomAbs_BSplineSurface) plane = splinePlane(surface.BSpline(), tolerance);
            else if (surface.GetType() == GeomAbs_BezierSurface) plane = splinePlane(surface.Bezier(), tolerance);
            else
            {
                const GeomLib_IsPlanarSurface planar(BRep_Tool::Surface(face), tolerance);
                if (planar.IsPlanar()) plane = planar.Plan();
            }
        }
        if (!plane) continue;
        GProp_GProps area;
        BRepGProp::SurfaceProperties(face, area, 1e-9);
        if (area.Mass() > 1e-8) faces.push_back({face, *plane, area.Mass()});
    }
    std::stable_sort(faces.begin(), faces.end(), [](const auto &a, const auto &b) { return a.area > b.area; });
    for (std::size_t i = 0; i < faces.size(); ++i)
    {
        // 窄端面不应成为板厚主面；允许短板的侧面略大于主面。
        if (faces[i].area < faces.front().area * 0.5) break;
        const gp_Dir normal = faces[i].plane.Axis().Direction();
        for (std::size_t j = i + 1; j < faces.size(); ++j)
        {
            if (faces[j].area < faces.front().area * 0.5) break;
            if (std::abs(normal.Dot(faces[j].plane.Axis().Direction())) < 1 - 1e-10) continue;
            const double first = faces[i].plane.Location().XYZ().Dot(normal.XYZ());
            const double second = faces[j].plane.Location().XYZ().Dot(normal.XYZ());
            const double thickness = std::abs(second - first);
            if (thickness <= 1e-5) continue;
            gp_Trsf toPlane;
            toPlane.SetTransformation(gp_Ax3(gp_Pnt(), normal));
            Bnd_Box bounds;
            BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(shape, toPlane, false).Shape(), bounds, false, false);
            double x0, y0, z0, x1, y1, z1;
            bounds.Get(x0, y0, z0, x1, y1, z1);
            const double tolerance = std::max({1e-5, BRep_Tool::Tolerance(faces[i].face),
                                               BRep_Tool::Tolerance(faces[j].face), thickness * 1e-6});
            if (std::abs(z0 - std::min(first, second)) > tolerance ||
                std::abs(z1 - std::max(first, second)) > tolerance) continue;
            PlateProfile plate;
            plate.front = faces[i].face;
            plate.back = faces[j].face;
            plate.plane = faces[i].plane;
            plate.thickness = thickness;
            return {plate, gp_Vec(normal) * (second - first), false};
        }
    }
    throw GeometryError("unsupported_plate_outline", "未能确定包住实体的两个平行主面，无法计算或修改板厚。");
}
} // namespace

PlateThicknessInfo inspectPlateThickness(const TopoDS_Shape &shape)
{
    if (shape.ShapeType() != TopAbs_SOLID)
    {
        throw GeometryError("not_solid", "修改板厚需要等厚实体板件，不能修改观测面或开放曲面。");
    }
    if (const auto steel = inspectSteelSection(shape)) return steelThickness(*steel);
    const auto detected = thicknessProfile(shape);
    const auto &profile = detected.plate;
    const gp_Vec direction = detected.direction;
    GProp_GProps frontArea, backArea;
    BRepGProp::SurfaceProperties(profile.front, frontArea, 1e-9);
    BRepGProp::SurfaceProperties(profile.back, backArea, 1e-9);
    const gp_Pnt center((frontArea.CentreOfMass().XYZ() + backArea.CentreOfMass().XYZ()) * 0.5);
    const gp_Vec normal = direction.Normalized();
    return {profile.thickness, {center.X(), center.Y(), center.Z()}, {normal.X(), normal.Y(), normal.Z()}};
}

TopoDS_Shape resizePlateThickness(const TopoDS_Shape &source, double thicknessMm)
{
    if (!std::isfinite(thicknessMm) || thicknessMm < 0.001 || thicknessMm > 1e6)
    {
        throw GeometryError("invalid_thickness", "参数 --thickness 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    if (auto steel = inspectSteelSection(source))
    {
        steel->thickness = thicknessMm;
        return buildSteelSection(*steel);
    }
    (void)inspectPlateThickness(source);
    const TopoDS_Shape copy = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto detected = thicknessProfile(copy);
    const auto &profile = detected.plate;
    const gp_Vec normal = detected.direction.Normalized();
    TopoDS_Shape result;
    if (detected.straightPrism)
    {
        gp_Trsf shift;
        shift.SetTranslation(normal * ((profile.thickness - thicknessMm) * 0.5));
        const TopoDS_Shape face = BRepBuilderAPI_Transform(profile.front, shift, true).Shape();
        BRepPrimAPI_MakePrism prism(face, normal * thicknessMm, true);
        if (prism.IsDone()) result = prism.Shape();
    }
    else
    {
        // 保留扫描样条、斜切边和孔，仅沿两主面的法线相对中面伸缩。
        const gp_Pnt center = profile.plane.Location().Translated(detected.direction * 0.5);
        gp_GTrsf scale;
        scale.SetAffinity(gp_Ax2(center, gp_Dir(normal)), thicknessMm / profile.thickness);
        BRepBuilderAPI_GTransform transformed(copy, scale, true);
        if (transformed.IsDone()) result = transformed.Shape();
    }
    if (result.IsNull() || result.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(result).IsValid())
    {
        throw GeometryError("thickness_failed", "板厚修改未生成有效实体，整批未应用。");
    }
    // 体积应仅随板厚成比例变化；可同时检出内孔或轮廓意外丢失。
    GProp_GProps before, after;
    BRepGProp::VolumeProperties(source, before, 1e-9);
    BRepGProp::VolumeProperties(result, after, 1e-9);
    const double expected = before.Mass() * thicknessMm / profile.thickness;
    if (after.Mass() <= 0 || std::abs(after.Mass() - expected) > std::max(1e-4, expected * 1e-6))
    {
        throw GeometryError("thickness_failed", "修改板厚后的体积与轮廓不一致，整批未应用。");
    }
    return result;
}
} // namespace reverse::detail
