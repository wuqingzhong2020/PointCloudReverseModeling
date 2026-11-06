/* 文件描述：保留主面外轮廓曲线和厚度修复贯穿孔；可显式填补有限尺寸的边缘缺口。 */
#include "geometry/PlateEditing.h"
#include "geometry/PlateOutlineTreatment.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>

namespace reverse::detail
{
namespace
{
double area(const TopoDS_Shape &s)
{
    GProp_GProps p;
    BRepGProp::SurfaceProperties(s, p);
    return std::abs(p.Mass());
}
// 导入的板侧壁可能以 B-spline 保存，不能只按曲面类型判断是否为板。
// 选择面积和体积相符的两个平行主面；修复时只添加孔内材料，不替换原板外形。
PlateProfile repairProfile(const TopoDS_Shape &shape)
{
    if (shape.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(shape).IsValid())
        throw GeometryError("unsupported_plate", "需要有效的等厚实体板件。");
    std::vector<TopoDS_Face> faces;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
        if (BRepAdaptor_Surface(TopoDS::Face(it.Current())).GetType() == GeomAbs_Plane)
            faces.push_back(TopoDS::Face(it.Current()));
    PlateProfile best;
    double score = 0;
    for (std::size_t i = 0; i < faces.size(); ++i)
        for (std::size_t j = i + 1; j < faces.size(); ++j)
        {
            const auto a = BRepAdaptor_Surface(faces[i]).Plane(), b = BRepAdaptor_Surface(faces[j]).Plane();
            const double aa = area(faces[i]), ab = area(faces[j]), thickness = a.Distance(b.Location());
            if (std::abs(a.Axis().Direction().Dot(b.Axis().Direction())) < 1 - 1e-8 || thickness < 1e-5 ||
                std::abs(aa - ab) > std::max(1e-4, std::max(aa, ab) * .001))
                continue;
            const double expected = (aa + ab) * .5 * thickness;
            if (std::abs(plateVolume(shape) - expected) > std::max(1e-4, expected * .001))
                continue;
            if (aa + ab > score)
            {
                best = {faces[i], faces[j], a, thickness};
                score = aa + ab;
            }
        }
    if (best.front.IsNull())
        throw GeometryError("unsupported_plate", "未找到面积和体积一致的平行主面；不支持变厚板、盲孔或开放曲面。");
    return best;
}
TopoDS_Face fillNotches(TopoDS_Face face, const PlateEdit &r, int &count)
{
    std::vector<TopoDS_Edge> edges;
    for (BRepTools_WireExplorer it(BRepTools::OuterWire(face), face); it.More(); it.Next())
        edges.push_back(it.Current());
    for (std::size_t start = 0; start < edges.size(); ++start)
    {
        for (std::size_t n = std::min<std::size_t>(3, edges.size() - 1); n > 0; --n)
        {
            const auto a = BRep_Tool::Pnt(TopExp::FirstVertex(edges[start], true));
            const auto b = BRep_Tool::Pnt(TopExp::LastVertex(edges[(start + n - 1) % edges.size()], true));
            const double span = a.Distance(b);
            if (span < 1e-6 || span > r.notchWidth)
                continue;
            if (n > 1)
            {
                const BRepAdaptor_Curve before(edges[(start + edges.size() - 1) % edges.size()]),
                    after(edges[(start + n) % edges.size()]);
                const gp_Dir chord(gp_Vec(a, b));
                // 多边形缺口必须回到同一条外边；不只补出矩形缺口的一半三角形。
                if (before.GetType() != GeomAbs_Line || after.GetType() != GeomAbs_Line ||
                    std::abs(before.Line().Direction().Dot(chord)) < 1 - 1e-6 ||
                    std::abs(after.Line().Direction().Dot(chord)) < 1 - 1e-6)
                    continue;
            }
            BRepBuilderAPI_MakeWire wire;
            double depth = 0;
            for (std::size_t j = 0; j < n; ++j)
            {
                const auto e = edges[(start + j) % edges.size()];
                wire.Add(e);
                BRepAdaptor_Curve c(e);
                for (int k = 0; k <= 12; ++k)
                    depth = std::max(depth, gp_Vec(a, c.Value(c.FirstParameter() +
                                                              (c.LastParameter() - c.FirstParameter()) * k / 12))
                                                    .Crossed(gp_Vec(a, b))
                                                    .Magnitude() /
                                                span);
            }
            if (depth < r.simplifyTolerance || depth > r.notchDepth)
                continue;
            wire.Add(BRepBuilderAPI_MakeEdge(b, a).Edge());
            if (!wire.IsDone())
                continue;
            BRepBuilderAPI_MakeFace patch(BRepAdaptor_Surface(face).Plane(), wire.Wire(), true);
            if (!patch.IsDone() || !BRepCheck_Analyzer(patch.Face()).IsValid())
                continue;
            BRepAlgoAPI_Common overlap;
            overlap.SetNonDestructive(true);
            overlap.SetArguments({face});
            overlap.SetTools({patch.Face()});
            overlap.Build();
            if (!overlap.IsDone())
                throw GeometryError("repair_failed", "无法验证缺口范围。");
            // 凸角的封闭区域在板内，绝不作为缺口填充。
            if (area(overlap.Shape()) > std::max(1e-6, area(patch.Face()) * 1e-6))
                continue;
            BRepAlgoAPI_Fuse fuse;
            fuse.SetNonDestructive(true);
            fuse.SetArguments({face});
            fuse.SetTools({patch.Face()});
            fuse.Build();
            if (!fuse.IsDone())
                throw GeometryError("repair_failed", "边缘缺口修复失败。");
            ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, false);
            unify.Build();
            TopExp_Explorer it(unify.Shape(), TopAbs_FACE);
            if (!it.More())
                continue;
            const auto merged = TopoDS::Face(it.Current());
            it.Next();
            if (it.More())
                continue;
            face = merged;
            ++count;
            start += n - 1;
            break;
        }
    }
    return face;
}
} // namespace
PlateThicknessInfo inspectRepairPlateThickness(const TopoDS_Shape &source)
{
    const auto p = repairProfile(source);
    GProp_GProps props;
    BRepGProp::SurfaceProperties(p.front, props);
    const auto n = p.plane.Axis().Direction();
    const double d = gp_Vec(p.plane.Location(), BRepAdaptor_Surface(p.back).Plane().Location()).Dot(gp_Vec(n));
    const auto c = props.CentreOfMass().Translated(gp_Vec(n) * d * .5);
    return {p.thickness, {c.X(), c.Y(), c.Z()}, {n.X(), n.Y(), n.Z()}};
}
TopoDS_Shape repairPlate(const TopoDS_Shape &source, const PlateEdit &r, std::string &report)
{
    const auto copy = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto profile = repairProfile(copy);
    const auto front = TopoDS::Face(profile.front.Oriented(TopAbs_FORWARD));
    const auto outer = BRepTools::OuterWire(front);
    BRepBuilderAPI_MakeFace build(profile.plane, outer, true);
    int holes = 0, notches = 0;
    for (TopExp_Explorer it(front, TopAbs_WIRE); it.More(); it.Next())
    {
        if (it.Current().IsSame(outer))
            continue;
        const auto wire = TopoDS::Wire(it.Current());
        const double size = area(BRepBuilderAPI_MakeFace(profile.plane, wire, true).Face());
        if (r.maxHoleArea > 0 && size > r.maxHoleArea)
            build.Add(wire);
        else
            ++holes;
    }
    auto face = build.Face();
    if (r.boundary)
        face = fillNotches(face, r, notches);
    report = "修复贯穿孔 " + std::to_string(holes) + " 个，边缘缺口 " + std::to_string(notches) + " 个。";
    if (!holes && !notches)
    {
        report = "无需修改（没有符合范围的孔洞/缺口）。";
        return source;
    }
    const gp_Vec normal(profile.plane.Axis().Direction());
    const auto back = BRepAdaptor_Surface(profile.back).Plane().Location();
    BRepAlgoAPI_Cut added;
    added.SetNonDestructive(true);
    added.SetArguments({face});
    added.SetTools({front});
    added.Build();
    if (!added.IsDone())
        throw GeometryError("repair_failed", "无法确定需填补的孔洞区域。");
    auto repaired = copy;
    for (TopExp_Explorer it(added.Shape(), TopAbs_FACE); it.More(); it.Next())
    {
        BRepPrimAPI_MakePrism prism(it.Current(), normal * gp_Vec(profile.plane.Location(), back).Dot(normal), true);
        if (!prism.IsDone())
            throw GeometryError("repair_failed", "孔洞补片拉伸失败。");
        BRepAlgoAPI_Common overlap;
        overlap.SetNonDestructive(true);
        overlap.SetArguments({copy});
        overlap.SetTools({prism.Shape()});
        overlap.Build();
        if (!overlap.IsDone() || plateVolume(overlap.Shape()) > std::max(1e-5, plateVolume(prism.Shape()) * .001))
            throw GeometryError("unsupported_hole", "孔洞不是贯穿孔，或两侧轮廓不一致；原模型保持不变。");
        BRepAlgoAPI_Fuse fuse;
        fuse.SetNonDestructive(true);
        fuse.SetArguments({repaired});
        fuse.SetTools({prism.Shape()});
        fuse.Build();
        if (!fuse.IsDone())
            throw GeometryError("repair_failed", "孔洞补片与板件合并失败。");
        repaired = fuse.Shape();
    }
    ShapeUpgrade_UnifySameDomain unify(repaired, true, true, false);
    unify.Build();
    auto solids = plateSolids(unify.Shape());
    if (solids.size() != 1 || !BRepCheck_Analyzer(solids.front()).IsValid())
        throw GeometryError("repair_failed", "修复后无法形成单个有效等厚板件。");
    if (plateVolume(solids.front()) <= plateVolume(source))
        throw GeometryError("repair_failed", "修复未增加有效材料，请检查孔洞轮廓。");
    return solids.front();
}
} // namespace reverse::detail
