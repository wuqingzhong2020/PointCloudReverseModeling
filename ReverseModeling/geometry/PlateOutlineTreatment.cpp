/*
 * 文件描述：识别具有两个平行主面和直母线侧面的等厚板件。
 * 轮廓边映射到两端未光顺的贯穿厚度角边；短角边仅处理自身。
 * 不对板面边沿做截面磨圆，拒绝非等厚实体和已经光顺的轮廓，避免误改板厚。
 */
#include <algorithm>
#include <cmath>
#include <set>
#include <string>
#include <vector>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedDataMap.hxx>
#include <NCollection_IndexedMap.hxx>
#include <NCollection_List.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gp_Cylinder.hxx>
#include <gp_Lin.hxx>
#include <gp_Pln.hxx>

#include "geometry/Geometry.h"
#include "geometry/PlateOutlineTreatment.h"

namespace reverse::detail
{
namespace
{
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
using Ancestors = NCollection_IndexedDataMap<TopoDS_Shape, NCollection_List<TopoDS_Shape>, TopTools_ShapeMapHasher>;
constexpr double angularTolerance = 1e-6;
constexpr double distanceTolerance = 1e-5;

[[noreturn]] void unsupportedPlate()
{
    throw GeometryError("unsupported_plate_outline", "需要等厚实体板件（两个平行主面），不支持曲面板或变厚板。");
}

PlateProfile inspectProfile(const TopoDS_Shape &shape)
{
    PlateProfile profile;
    double largestArea = 0;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        if (BRepAdaptor_Surface(face).GetType() != GeomAbs_Plane)
        {
            continue;
        }
        GProp_GProps area;
        BRepGProp::SurfaceProperties(face, area);
        if (area.Mass() > largestArea)
        {
            largestArea = area.Mass();
            profile.front = face;
        }
    }
    if (profile.front.IsNull())
    {
        unsupportedPlate();
    }
    profile.plane = BRepAdaptor_Surface(profile.front).Plane();
    const auto normal = profile.plane.Axis().Direction();
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        if (face.IsSame(profile.front))
        {
            continue;
        }
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() == GeomAbs_Cylinder &&
            std::abs(surface.Cylinder().Axis().Direction().Dot(normal)) > 1 - angularTolerance)
        {
            continue;
        }
        if (surface.GetType() != GeomAbs_Plane)
        {
            unsupportedPlate();
        }
        const auto plane = surface.Plane();
        const double alignment = std::abs(normal.Dot(plane.Axis().Direction()));
        if (alignment < angularTolerance)
        {
            continue;
        }
        if (alignment < 1 - angularTolerance || !profile.back.IsNull())
        {
            unsupportedPlate();
        }
        GProp_GProps area;
        BRepGProp::SurfaceProperties(face, area);
        if (std::abs(area.Mass() - largestArea) > std::max(1e-4, largestArea * 1e-6))
        {
            unsupportedPlate();
        }
        profile.back = face;
        profile.thickness = profile.plane.Distance(plane.Location());
    }
    if (profile.back.IsNull() || profile.thickness <= distanceTolerance)
    {
        unsupportedPlate();
    }
    GProp_GProps volume;
    BRepGProp::VolumeProperties(shape, volume);
    const double expectedVolume = largestArea * profile.thickness;
    if (std::abs(volume.Mass() - expectedVolume) > std::max(1e-4, expectedVolume * 1e-6))
    {
        unsupportedPlate();
    }
    return profile;
}

bool isSharpCorner(const TopoDS_Edge &edge, const Ancestors &ancestors)
{
    const auto &faces = ancestors.FindFromKey(edge);
    if (faces.Size() != 2)
    {
        return false;
    }
    const BRepAdaptor_Surface a(TopoDS::Face(faces.First()));
    const BRepAdaptor_Surface b(TopoDS::Face(faces.Last()));
    return a.GetType() == GeomAbs_Plane && b.GetType() == GeomAbs_Plane &&
           std::abs(a.Plane().Axis().Direction().Dot(b.Plane().Axis().Direction())) < 1 - angularTolerance;
}

bool sharesVertex(const TopoDS_Edge &a, const TopoDS_Edge &b)
{
    for (TopExp_Explorer av(a, TopAbs_VERTEX); av.More(); av.Next())
    {
        for (TopExp_Explorer bv(b, TopAbs_VERTEX); bv.More(); bv.Next())
        {
            if (av.Current().IsSame(bv.Current()))
            {
                return true;
            }
        }
    }
    return false;
}
} // namespace

PlateProfile plateProfile(const TopoDS_Shape &shape)
{
    return inspectProfile(shape);
}

std::vector<TopoDS_Edge> plateOutlineCornerEdges(const TopoDS_Shape &shape,
                                               const std::vector<std::uint32_t> &indices)
{
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (const auto index : indices)
    {
        if (index == 0 || index > static_cast<std::uint32_t>(edges.Extent()))
        {
            throw GeometryError("unknown_edge", "边编号不存在（从 1 开始）：" + std::to_string(index));
        }
    }
    const auto profile = plateProfile(shape);
    const auto normal = profile.plane.Axis().Direction();
    ShapeMap boundary;
    TopExp::MapShapes(profile.front, TopAbs_EDGE, boundary);
    TopExp::MapShapes(profile.back, TopAbs_EDGE, boundary);
    Ancestors ancestors;
    TopExp::MapShapesAndUniqueAncestors(shape, TopAbs_EDGE, TopAbs_FACE, ancestors);
    std::set<int> corners;
    for (int i = 1; i <= edges.Extent(); ++i)
    {
        const auto edge = TopoDS::Edge(edges(i));
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line || BRep_Tool::Degenerated(edge))
        {
            continue;
        }
        const double length = curve.Value(curve.FirstParameter()).Distance(curve.Value(curve.LastParameter()));
        if (std::abs(curve.Line().Direction().Dot(normal)) > 1 - angularTolerance &&
            std::abs(length - profile.thickness) <= std::max(distanceTolerance, profile.thickness * 1e-6) &&
            isSharpCorner(edge, ancestors))
        {
            corners.insert(i);
        }
    }
    std::set<int> selectedCorners;
    for (const auto index : indices)
    {
        if (corners.contains(static_cast<int>(index)))
        {
            selectedCorners.insert(static_cast<int>(index));
            continue;
        }
        const auto edge = TopoDS::Edge(edges(static_cast<int>(index)));
        bool found = false;
        if (boundary.Contains(edge) && BRepAdaptor_Curve(edge).GetType() == GeomAbs_Line)
        {
            for (const int corner : corners)
            {
                if (sharesVertex(edge, TopoDS::Edge(edges(corner))))
                {
                    selectedCorners.insert(corner);
                    found = true;
                }
            }
        }
        if (!found)
        {
            throw GeometryError("not_outline_corner", "边 " + std::to_string(index) +
                " 没有可处理的板角；请选择直线轮廓边或板厚方向的角边。已有圆角请先撤销再调整。");
        }
    }
    std::vector<TopoDS_Edge> result;
    for (const int index : selectedCorners)
    {
        result.push_back(TopoDS::Edge(edges(index)));
    }
    return result;
}
} // namespace reverse::detail
