/* 文件描述：依据真实矩形侧面构造 U/L 截面并拉伸；支撑面必须覆盖开口底边，拒绝曲面及非矩形源件。 */
#include "geometry/StructuralSteel.h"
#include "geometry/Geometry.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace reverse::detail
{
namespace
{
constexpr double tolerance = 1e-5;
using Point2 = std::array<double, 2>;
std::vector<Point2> outline(const SteelSection &s)
{
    const auto w = s.width, h = s.height, d = s.thickness;
    if (s.kind == StructuralSteelKind::Channel)
    {
        return {{0, 0}, {w, 0}, {w, h}, {0, h}, {0, h - d}, {w - d, h - d}, {w - d, d}, {0, d}};
    }
    return {{w - d, 0}, {w, 0}, {w, h}, {0, h}, {0, h - d}, {w - d, h - d}};
}
std::vector<gp_Pnt> vertices(const TopoDS_Shape &shape)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> map;
    TopExp::MapShapes(shape, TopAbs_VERTEX, map);
    std::vector<gp_Pnt> points;
    for (int i = 1; i <= map.Extent(); ++i)
    {
        points.push_back(BRep_Tool::Pnt(TopoDS::Vertex(map(i))));
    }
    return points;
}
gp_Dir normal(const TopoDS_Face &face)
{
    auto n = BRepAdaptor_Surface(face).Plane().Axis().Direction();
    if (face.Orientation() == TopAbs_REVERSED)
        n.Reverse();
    return n;
}
bool planarStraight(const TopoDS_Shape &shape)
{
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() != GeomAbs_Plane)
            return false;
    }
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
    {
        if (BRepAdaptor_Curve(TopoDS::Edge(it.Current())).GetType() != GeomAbs_Line)
            return false;
    }
    return true;
}
std::pair<double, double> extent(const std::vector<gp_Pnt> &points, const gp_Pnt &origin, const gp_Dir &axis)
{
    double lo = std::numeric_limits<double>::max(), hi = -lo;
    for (const auto &p : points)
    {
        const auto value = gp_Vec(origin, p).Dot(gp_Vec(axis));
        lo = std::min(lo, value);
        hi = std::max(hi, value);
    }
    return {lo, hi};
}
bool onFace(const TopoDS_Face &face, const gp_Pnt &point)
{
    const auto state = BRepClass_FaceClassifier(face, point, tolerance).State();
    return state == TopAbs_IN || state == TopAbs_ON;
}
void validate(const SteelSection &s)
{
    const auto d = s.thickness;
    if (!std::isfinite(d) || d < 1 || d > 50)
        throw GeometryError("invalid_steel_thickness", "壁厚 D 必须在 1～50 mm 之间。");
    if (!std::isfinite(s.height) || s.height <= d * (s.kind == StructuralSteelKind::Channel ? 2 : 1) + tolerance ||
        s.height > 1e6 || s.width <= d + tolerance || s.length <= tolerance)
        throw GeometryError("invalid_steel_dimensions",
                            "槽钢高度须大于 2D，角钢高度须大于 D，截面宽度须大于 D；请调整壁厚或高度。");
}
} // namespace

TopoDS_Shape buildSteelSection(const SteelSection &s)
{
    validate(s);
    BRepBuilderAPI_MakePolygon polygon;
    for (const auto &p : outline(s))
    {
        polygon.Add(s.origin.Translated(gp_Vec(s.inward) * p[0] + gp_Vec(s.up) * p[1]));
    }
    polygon.Close();
    BRepBuilderAPI_MakeFace face(polygon.Wire());
    BRepPrimAPI_MakePrism prism(face.Face(), gp_Vec(s.along) * s.length, true);
    if (!prism.IsDone() || prism.Shape().ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(prism.Shape()).IsValid())
        throw GeometryError("steel_build_failed", "型钢实体生成失败，原部件未修改。");
    return prism.Shape();
}

SteelSection structuralSteelSection(const TopoDS_Shape &source, const StructuralSteel &request,
                                    const std::vector<TopoDS_Shape> &supports)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(source, TopAbs_FACE, faces);
    if (!request.faceIndex || request.faceIndex > static_cast<unsigned>(faces.Extent()))
        throw GeometryError("invalid_face", "请选择源部件的开口侧面。");
    const auto points = vertices(source);
    if (source.ShapeType() != TopAbs_SOLID || faces.Extent() != 6 || points.size() != 8 || !planarStraight(source))
        throw GeometryError("unsupported_steel_source",
                            "设为型钢需要水平放置的矩形实体；扫描部件可先局部重建为平板，选择矩形轮廓。");
    const auto face = TopoDS::Face(faces(static_cast<int>(request.faceIndex)));
    const auto outward = normal(face);
    SteelSection s;
    s.kind = request.kind;
    bool bottomFound = false;
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const auto n = normal(TopoDS::Face(faces(i)));
        if (n.Z() < -std::cos(5.0 * std::acos(-1.0) / 180.0))
        {
            s.up = n.Reversed();
            bottomFound = true;
            break;
        }
    }
    if (!bottomFound)
        throw GeometryError("inclined_steel_source", "矩形实体底面倾角超过 5°，请先旋转放平。");
    if (std::abs(outward.Dot(s.up)) > 1e-6)
        throw GeometryError("invalid_opening_face", "请选择与底面垂直的开口侧面，不能选择顶面或底面。");
    s.inward = outward.Reversed();
    s.along = s.inward.Crossed(s.up);
    const auto origin = points.front();
    const auto [xl, xh] = extent(points, origin, s.along);
    const auto [yl, yh] = extent(points, origin, s.inward);
    const auto [zl, zh] = extent(points, origin, s.up);
    s.origin = origin.Translated(gp_Vec(s.along) * xl + gp_Vec(s.inward) * yl + gp_Vec(s.up) * zl);
    s.length = xh - xl;
    s.width = yh - yl;
    s.height = zh - zl;
    s.thickness = request.thicknessMm;
    // 八个顶点必须恰好构成所选局部坐标中的矩形盒，禁止斜切、多棱体按包围盒替换。
    for (const auto &p : points)
    {
        const auto v = gp_Vec(s.origin, p);
        for (const auto [value, size] : {std::pair{v.Dot(gp_Vec(s.along)), s.length},
                                         std::pair{v.Dot(gp_Vec(s.inward)), s.width}, std::pair{v.Dot(gp_Vec(s.up)), s.height}})
        {
            // 扫描拟合/交换精度允许 0.01 mm 角点偏差，不能把微小拟合误差当成斜切结构。
            if (std::min(std::abs(value), std::abs(value - size)) > 0.01)
                throw GeometryError("unsupported_steel_source", "源部件不是规则矩形实体，角点偏差 " +
                    std::to_string(std::min(std::abs(value), std::abs(value-size))) + " mm；不能按包围盒替代真实轮廓。");
        }
    }
    // 沿真实底面法线找下方平行支撑；保留线扫拟合产生的微倾斜，不强行对齐世界 Z。
    double supportDistance = -50.0 - tolerance;
    bool found = false;
    for (const auto &support : supports)
    {
        for (TopExp_Explorer it(support, TopAbs_FACE); it.More(); it.Next())
        {
            const auto f = TopoDS::Face(it.Current());
            const BRepAdaptor_Surface surface(f);
            if (surface.GetType() != GeomAbs_Plane || normal(f).Dot(s.up) < 1 - 1e-6)
                continue;
            const double distance = gp_Vec(s.origin, surface.Plane().Location()).Dot(gp_Vec(s.up));
            if (distance > tolerance || distance <= supportDistance)
                continue;
            bool covered = true;
            for (double fraction : {0.0, 0.25, 0.5, 0.75, 1.0})
            {
                const auto point = s.origin.Translated(gp_Vec(s.along) * s.length * fraction + gp_Vec(s.up) * distance);
                if (!onFace(f, point))
                {
                    covered = false;
                    break;
                }
            }
            if (covered)
            {
                supportDistance = distance;
                found = true;
            }
        }
    }
    if (found)
    {
        s.height -= supportDistance;
        s.origin.Translate(gp_Vec(s.up) * supportDistance);
    }
    if (!request.autoHeight)
    {
        if (!std::isfinite(request.heightMm) || request.heightMm < 1 || request.heightMm > 1000)
            throw GeometryError("invalid_steel_height", "自定义高度 H 必须在 1～1000 mm 之间。");
        s.height = request.heightMm;
    }
    validate(s);
    return s;
}

// 按完整的 6/8 点正交截面识别，不把端面之间的拉伸长度误报为壁厚；STEP/CWRM 回读同样适用。
std::optional<SteelSection> inspectSteelSection(const TopoDS_Shape &shape)
{
    if (shape.ShapeType() != TopAbs_SOLID)
        return std::nullopt;
    const auto points = vertices(shape);
    if ((points.size() != 12 && points.size() != 16) || !planarStraight(shape))
        return std::nullopt;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        const auto endPoints = vertices(face);
        if (endPoints.size() * 2 != points.size())
            continue;
        const auto along = normal(face).Reversed();
        for (TopExp_Explorer edge(face, TopAbs_EDGE); edge.More(); edge.Next())
        {
            const auto line = BRepAdaptor_Curve(TopoDS::Edge(edge.Current())).Line();
            for (double sign : {-1.0, 1.0})
            {
                SteelSection s;
                s.kind = points.size() == 16 ? StructuralSteelKind::Channel : StructuralSteelKind::Angle;
                s.along = along;
                s.inward = gp_Dir(gp_Vec(line.Direction()) * sign);
                for (double upSign : {-1.0, 1.0})
                {
                    s.up = gp_Dir(gp_Vec(s.along.Crossed(s.inward)) * upSign);
                    const auto p = endPoints.front();
                    const auto [xl, xh] = extent(points, p, s.along);
                    const auto [yl, yh] = extent(points, p, s.inward);
                    const auto [zl, zh] = extent(points, p, s.up);
                    s.origin = p.Translated(gp_Vec(s.along) * xl + gp_Vec(s.inward) * yl + gp_Vec(s.up) * zl);
                    s.length = xh - xl;
                    s.width = yh - yl;
                    s.height = zh - zl;
                    for (const auto &candidate : endPoints)
                    {
                        s.thickness = s.width - gp_Vec(s.origin, candidate).Dot(gp_Vec(s.inward));
                        if (s.thickness < tolerance || s.thickness >= s.width - tolerance ||
                            s.height <= s.thickness * (s.kind == StructuralSteelKind::Channel ? 2 : 1) + tolerance)
                            continue;
                        bool matches = true;
                        for (const auto &uv : outline(s))
                        {
                            for (double x : {0.0, s.length})
                            {
                                const auto expected = s.origin.Translated(
                                    gp_Vec(s.along) * x + gp_Vec(s.inward) * uv[0] + gp_Vec(s.up) * uv[1]);
                                if (std::none_of(points.begin(), points.end(),
                                                 [&](const auto &v) { return v.Distance(expected) < tolerance; }))
                                    matches = false;
                            }
                        }
                        if (matches)
                            return s;
                    }
                }
            }
        }
    }
    return std::nullopt;
}
PlateThicknessInfo steelThickness(const SteelSection &s)
{
    const auto center =
        s.origin.Translated(gp_Vec(s.along) * (s.length / 2) + gp_Vec(s.inward) * (s.width - s.thickness / 2) +
                            gp_Vec(s.up) * (s.height / 2));
    return {s.thickness, {center.X(), center.Y(), center.Z()}, {s.inward.X(), s.inward.Y(), s.inward.Z()}};
}
} // namespace reverse::detail
