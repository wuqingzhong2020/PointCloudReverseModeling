/*
 * 文件描述：从主面生成外扩贴板；下底板由单块水平板或两块竖直板确定底部轮廓。
 * 下底板允许 ±5° 倾斜，将真实外环投影到最低 Z 的 XY 平面后外扩并沿 -Z 拉伸。
 * 不改变源板，不用包围盒代替轮廓；非平面或不连续支撑面拒绝生成。
 */
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepOffsetAPI_MakeOffset.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomProjLib.hxx>
#include <Geom_Plane.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Ax3.hxx>

#include "geometry/Geometry.h"
#include "geometry/PlateGeneration.h"
#include "geometry/PlateOutlineTreatment.h"

namespace reverse::detail
{
namespace
{
constexpr double baseAngleToleranceDeg = 5.0;
constexpr double angleEpsilon = 1e-9;

std::string angleText(double degrees)
{
    std::ostringstream text;
    text.imbue(std::locale::classic());
    text << std::fixed << std::setprecision(3) << degrees;
    return text.str();
}

gp_Dir outward(const TopoDS_Face &face)
{
    auto n = BRepAdaptor_Surface(face).Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED)
    {
        n.Reverse();
    }
    return n;
}
TopoDS_Face bottomFace(const TopoDS_Shape &shape, const PlateProfile &profile, Vec3 direction, BasePlateMode mode)
{
    const gp_Vec v(direction.x, direction.y, direction.z);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y) || !std::isfinite(direction.z) ||
        v.SquareMagnitude() < 1e-12)
    {
        throw GeometryError("invalid_up", "参数 --up 必须是非零有限方向向量。");
    }
    const gp_Dir up(0, 0, 1);
    // 已录制的 --up [0,0,1] 仍可执行；其他方向不得悄悄改变世界 -Z 规则。
    if (gp_Dir(v).Dot(up) < 1 - 1e-12)
    {
        throw GeometryError("invalid_up", "下底板固定沿世界 -Z 生成；参数 --up 只能为世界 +Z 方向 [0,0,1]。");
    }
    const double radiansToDegrees = 180.0 / std::acos(-1.0);
    const double horizontalAngle = std::acos(std::clamp(
        std::abs(profile.plane.Axis().Direction().Dot(up)), 0.0, 1.0)) * radiansToDegrees;
    const double verticalAngle = 90.0 - horizontalAngle;
    const bool horizontal = mode == BasePlateMode::Horizontal;
    const double deviation = horizontal ? horizontalAngle : verticalAngle;
    if (deviation > baseAngleToleranceDeg + angleEpsilon)
    {
        throw GeometryError("unsuitable_base_plate",
                            std::string(horizontal ? "单块水平板方式：板件偏离水平 " : "两块竖直板方式：板件偏离竖直 ") +
                                angleText(deviation) + "°，超过 5° 容差；请切换生成方式、重新选板或旋转板件后重试。");
    }
    // 水平板取朝下主面；竖直板取两条底边之间的窄支撑面，最终都只生成一块新板。
    constexpr double angularTolerance = 1e-6;
    double bestAlignment = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane)
        {
            bestAlignment = std::max(bestAlignment, -outward(face).Dot(up));
        }
    }
    const double bottomAngle = std::acos(std::clamp(bestAlignment, 0.0, 1.0)) * radiansToDegrees;
    if (bottomAngle > baseAngleToleranceDeg + angleEpsilon)
    {
        throw GeometryError("unsuitable_bottom_face",
                            "朝下支撑面偏离 XY 平面 " + angleText(bottomAngle) +
                                "°，超过 5° 容差；请旋转板件或整理底边后重试。");
    }
    TopoDS_Face found;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane || bestAlignment <= angularTolerance ||
            -outward(face).Dot(up) < bestAlignment - angularTolerance)
        {
            continue;
        }
        // 支撑面的内侧必须容纳整个源实体，避免选到缺口内部或让新增板穿入原板。
        const auto inward = outward(face).Reversed();
        gp_Trsf toLocal;
        toLocal.SetTransformation(gp_Ax3(gp_Pnt(0, 0, 0), inward));
        Bnd_Box box;
        BRepBndLib::AddOptimal(BRepBuilderAPI_Transform(shape, toLocal, true).Shape(), box, false, false);
        double x0, y0, z0, x1, y1, z1;
        box.Get(x0, y0, z0, x1, y1, z1);
        const auto p = surface.Plane().Location();
        if (std::abs(gp_Vec(gp_Pnt(0, 0, 0), p).Dot(gp_Vec(inward)) - z0) > 1e-4)
        {
            continue;
        }
        if (!found.IsNull())
        {
            if (outward(found).Dot(outward(face)) < 1 - angularTolerance)
            {
                throw GeometryError("ambiguous_bottom",
                                    "有多个同样朝下的支撑方向，无法唯一确定下底面；请先整理底边。");
            }
            throw GeometryError("ambiguous_bottom",
                                "底部存在多个分离支撑面，无法唯一确定下底板；请先整理底边。");
        }
        found = face;
    }
    if (found.IsNull())
    {
        throw GeometryError("no_bottom_face",
                            "板件世界 -Z 方向没有连续平直的支撑面；请检查弧底或底部缺口，整理底边后重试。");
    }
    return found;
}

double lowestZ(const TopoDS_Shape &source)
{
    // 仅使用包围盒的最低 Z 定位，不用包围盒矩形替代源轮廓。
    Bnd_Box bounds;
    BRepBndLib::AddOptimal(source, bounds, false, false);
    double x0, y0, z0, x1, y1, z1;
    bounds.Get(x0, y0, z0, x1, y1, z1);
    return z0;
}

TopoDS_Face horizontalFootprint(const TopoDS_Face &support, double z)
{
    const gp_Pln plane(gp_Pnt(0, 0, z), gp_Dir(0, 0, 1));
    const Handle(Geom_Plane) surface = new Geom_Plane(plane);
    const auto front = TopoDS::Face(support.Oriented(TopAbs_FORWARD));
    const auto outer = BRepTools::OuterWire(front);
    BRepBuilderAPI_MakeWire wire;
    for (BRepTools_WireExplorer it(outer, front); it.More(); it.Next())
    {
        const auto edge = it.Current();
        double first, last;
        const auto curve = BRep_Tool::Curve(edge, first, last);
        if (curve.IsNull())
        {
            throw GeometryError("plate_generation_failed", "底部轮廓包含无效边，无法投影生成水平底板。");
        }
        // 先限定为实际边段再投影，允许投影曲线重参数化。
        // 无限直线保留原参数的投影会在倾斜边上产生错误端点，导致闭合轮廓断开。
        const Handle(Geom_TrimmedCurve) segment = new Geom_TrimmedCurve(curve, first, last);
        const auto projected = GeomProjLib::ProjectOnPlane(segment, surface, gp_Dir(0, 0, -1), false);
        if (projected.IsNull())
        {
            throw GeometryError("plate_generation_failed", "底部轮廓投影到 XY 平面失败，请检查底边。");
        }
        BRepBuilderAPI_MakeEdge builder(projected);
        if (!builder.IsDone())
        {
            throw GeometryError("plate_generation_failed", "底部轮廓投影后存在退化边，未生成底板。");
        }
        wire.Add(TopoDS::Edge(builder.Edge().Oriented(edge.Orientation())));
        if (!wire.IsDone())
        {
            throw GeometryError("plate_generation_failed", "底部轮廓投影后不连续，未生成底板。");
        }
    }
    if (!wire.IsDone() || !wire.Wire().Closed())
    {
        throw GeometryError("plate_generation_failed", "底部轮廓投影后不闭合，未生成底板。");
    }
    BRepBuilderAPI_MakeFace face(plane, wire.Wire(), true);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
    {
        throw GeometryError("plate_generation_failed", "底部轮廓投影后无效或自交，未生成底板。");
    }
    return face.Face();
}

std::vector<gp_Pnt> bottomCorners(const TopoDS_Face &face)
{
    std::vector<gp_Pnt> corners;
    for (BRepTools_WireExplorer it(BRepTools::OuterWire(face), face); it.More(); it.Next())
    {
        if (BRepAdaptor_Curve(it.Current()).GetType() != GeomAbs_Line)
        {
            throw GeometryError("unsupported_pair_bottom", "两块竖直板需要连续直线底边；弧底或开孔底边不适用。");
        }
        corners.push_back(BRep_Tool::Pnt(it.CurrentVertex()));
    }
    if (corners.size() != 4)
    {
        throw GeometryError("unsupported_pair_bottom", "两块竖直板需要完整的四边形底部支撑面；请先整理底边缺口。");
    }
    return corners;
}

TopoDS_Face pairedFootprint(const TopoDS_Shape &first, const PlateProfile &firstProfile,
                          const TopoDS_Shape &second, const PlateProfile &secondProfile, Vec3 up)
{
    const double z = std::min(lowestZ(first), lowestZ(second));
    const auto a = bottomCorners(horizontalFootprint(
        bottomFace(first, firstProfile, up, BasePlateMode::VerticalPair), z));
    const auto b = bottomCorners(horizontalFootprint(
        bottomFace(second, secondProfile, up, BasePlateMode::VerticalPair), z));
    const auto n1 = firstProfile.plane.Axis().Direction();
    const auto n2 = secondProfile.plane.Axis().Direction();
    const gp_Dir across(n1.X(), n1.Y(), 0), otherAcross(n2.X(), n2.Y(), 0);
    const double angle = std::acos(std::clamp(std::abs(across.Dot(otherAcross)), 0.0, 1.0)) * 180 / std::acos(-1.0);
    if (angle > baseAngleToleranceDeg + angleEpsilon)
    {
        throw GeometryError("unsuitable_plate_pair", "两块竖直板的水平方向夹角为 " + angleText(angle) +
            "°，超过 5°；请选择相对平行的两侧板件。");
    }
    const gp_Dir along(-across.Y(), across.X(), 0);
    const auto interval = [](const auto &points, const gp_Dir &axis)
    {
        double minimum = gp_Vec(gp_Pnt(), points.front()).Dot(gp_Vec(axis)), maximum = minimum;
        for (const auto &point : points)
        {
            const double t = gp_Vec(gp_Pnt(), point).Dot(gp_Vec(axis));
            minimum = std::min(minimum, t);
            maximum = std::max(maximum, t);
        }
        return std::pair{minimum, maximum};
    };
    const auto [a0, a1] = interval(a, across);
    const auto [b0, b1] = interval(b, across);
    const auto [s0, s1] = interval(a, along);
    const auto [t0, t1] = interval(b, along);
    if (std::max(a0, b0) <= std::min(a1, b1) + 1e-5 || std::min(s1, t1) <= std::max(s0, t0) + 1e-5)
    {
        throw GeometryError("unsuitable_plate_pair", "两块板的底部需要横向分开、沿长度有重叠；不能选择重叠、首尾相接或交叉的板件。");
    }
    // 用实际底部角点的外包络跨接两板及其中间间隔，不用世界轴包围盒替代轮廓。
    auto points = a;
    points.insert(points.end(), b.begin(), b.end());
    std::sort(points.begin(), points.end(), [](const auto &p, const auto &q)
    {
        return p.X() < q.X() || (p.X() == q.X() && p.Y() < q.Y());
    });
    const auto cross = [](const gp_Pnt &p, const gp_Pnt &q, const gp_Pnt &r)
    {
        return (q.X() - p.X()) * (r.Y() - p.Y()) - (q.Y() - p.Y()) * (r.X() - p.X());
    };
    std::vector<gp_Pnt> hull;
    for (const auto &point : points)
    {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), point) <= 1e-8)
        {
            hull.pop_back();
        }
        hull.push_back(point);
    }
    const auto lowerSize = hull.size();
    for (auto it = points.rbegin() + 1; it != points.rend(); ++it)
    {
        while (hull.size() > lowerSize && cross(hull[hull.size() - 2], hull.back(), *it) <= 1e-8)
        {
            hull.pop_back();
        }
        hull.push_back(*it);
    }
    hull.pop_back();
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto &point : hull)
    {
        polygon.Add(point);
    }
    polygon.Close();
    BRepBuilderAPI_MakeFace face(gp_Pln(gp_Pnt(0, 0, z), gp_Dir(0, 0, 1)), polygon.Wire(), true);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
    {
        throw GeometryError("plate_generation_failed", "两块竖直板的底部包络无效，未生成底板。");
    }
    return face.Face();
}
} // namespace
TopoDS_Shape generateAttachedPlate(const TopoDS_Shape &source, const PlateGeneration &r, const TopoDS_Shape &secondSource)
{
    const bool cover = r.kind == PlateGenerationKind::Cover;
    const double minOffset = cover ? -100 : 4, maxOffset = cover ? 100 : 1000;
    const double minThickness = cover ? 1 : 4, maxThickness = cover ? 100 : 50;
    if (!std::isfinite(r.offsetMm) || r.offsetMm < minOffset || r.offsetMm > maxOffset)
    {
        throw GeometryError("invalid_offset", cover ? "参数 --offset 必须在 -100 到 100 mm 之间。"
                                                    : "参数 --offset 必须在 4 到 1000 mm 之间。");
    }
    if (!std::isfinite(r.thicknessMm) || r.thicknessMm < minThickness || r.thicknessMm > maxThickness)
    {
        throw GeometryError("invalid_thickness", cover ? "参数 --thickness 必须在 1 到 100 mm 之间。"
                                                       : "参数 --thickness 必须在 4 到 50 mm 之间。");
    }
    if (source.IsNull() || source.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(source).IsValid())
    {
        throw GeometryError("not_solid", "需要有效的等厚实体板件，不能选择点云、观测面或开放曲面。");
    }
    const auto copy = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto profile = plateProfile(copy);
    TopoDS_Face selected;
    if (cover)
    {
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
        TopExp::MapShapes(copy, TopAbs_FACE, faces);
        if (!r.faceIndex || r.faceIndex > static_cast<std::uint32_t>(faces.Extent()))
        {
            throw GeometryError("unknown_face", "参数 --face 的面编号不存在（从 1 开始）。");
        }
        selected = TopoDS::Face(faces(static_cast<int>(r.faceIndex)));
        if (!selected.IsSame(profile.front) && !selected.IsSame(profile.back))
        {
            throw GeometryError("not_plate_main_face",
                                "请选择板件的平面主面，薄侧面、孔壁和圆弧面不能生成外扩贴板。");
        }
    }
    else
    {
        if (r.baseMode == BasePlateMode::VerticalPair)
        {
            if (secondSource.IsNull() || secondSource.ShapeType() != TopAbs_SOLID ||
                !BRepCheck_Analyzer(secondSource).IsValid())
            {
                throw GeometryError("not_solid", "第二块板必须是有效的等厚实体，不能选择曲面或点云。");
            }
            const auto second = BRepBuilderAPI_Copy(secondSource, true, false).Shape();
            selected = pairedFootprint(copy, profile, second, plateProfile(second), r.up);
        }
        else
        {
            selected = horizontalFootprint(bottomFace(copy, profile, r.up, r.baseMode), lowestZ(copy));
        }
    }
    const auto normal = cover ? outward(selected) : gp_Dir(0, 0, -1);
    const auto front = TopoDS::Face(selected.Oriented(TopAbs_FORWARD));
    const auto plane = BRepAdaptor_Surface(front).Plane();
    const auto outer = BRepTools::OuterWire(front);
    TopoDS_Wire expanded = outer;
    if (std::abs(r.offsetMm) > 1e-9)
    {
        BRepOffsetAPI_MakeOffset offset(BRepBuilderAPI_MakeFace(plane, outer, true).Face(),
                                        GeomAbs_Intersection);
        offset.Perform(r.offsetMm);
        if (!offset.IsDone() || offset.Shape().IsNull())
        {
            throw GeometryError("plate_generation_failed", "外轮廓偏移失败，请调整外扩量。");
        }
        int count = 0;
        for (TopExp_Explorer it(offset.Shape(), TopAbs_WIRE); it.More(); it.Next())
        {
            expanded = TopoDS::Wire(it.Current());
            ++count;
        }
        if (count != 1 || !expanded.Closed())
        {
            throw GeometryError("plate_generation_failed", "轮廓偏移后消失、分裂或产生多环，请调整外扩量。");
        }
    }
    // 附加板独立成形，只采用外轮廓，不把原板的封闭内孔复制到新板。
    BRepBuilderAPI_MakeFace face(plane, expanded, true);
    if (!face.IsDone() || !BRepCheck_Analyzer(face.Face()).IsValid())
    {
        throw GeometryError("plate_generation_failed", "偏移轮廓自交或无效，未生成板件。");
    }
    BRepPrimAPI_MakePrism prism(
        face.Face(), gp_Vec(normal) * (cover && r.reversed ? -r.thicknessMm : r.thicknessMm), true);
    if (!prism.IsDone() || prism.Shape().ShapeType() != TopAbs_SOLID ||
        !BRepCheck_Analyzer(prism.Shape()).IsValid())
    {
        throw GeometryError("plate_generation_failed", "未生成有效的独立实体板件。");
    }
    GProp_GProps area, volume;
    BRepGProp::SurfaceProperties(face.Face(), area);
    BRepGProp::VolumeProperties(prism.Shape(), volume);
    const double expected = area.Mass() * r.thicknessMm;
    if (expected <= 1e-6 || std::abs(volume.Mass() - expected) > std::max(1e-4, expected * 1e-6))
    {
        throw GeometryError("plate_generation_failed", "生成板件的体积或厚度不正确，未应用。");
    }
    return prism.Shape();
}
} // namespace reverse::detail
