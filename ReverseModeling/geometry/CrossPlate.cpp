/* 文件描述：按连续板两侧面切分交叉板，调整板段高度并处理附近底板；不修改输入拓扑。 */
#include "geometry/PlateEditing.h"
#include "geometry/PlateOutlineTreatment.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Bnd_OBB.hxx>
#include <GProp_GProps.hxx>
#include <IntCurvesFace_ShapeIntersector.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <optional>

namespace reverse::detail
{
namespace
{
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "交叉板优化已取消，未修改模型。");
}
gp_Pnt center(const TopoDS_Shape &shape)
{
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(shape, properties);
    return properties.CentreOfMass();
}
double distance(const gp_Pln &plane, const gp_Pnt &p)
{
    return gp_Vec(plane.Location(), p).Dot(gp_Vec(plane.Axis().Direction()));
}
double top(const TopoDS_Shape &shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (box.IsVoid())
        throw GeometryError("cross_missing_vertices", "板件缺少可用的高度边界。");
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    return z1;
}
TopoDS_Face bottom(const TopoDS_Shape &shape)
{
    double best = 1e100;
    TopoDS_Face result;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            continue;
        auto n = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            n.Reverse();
        const double z = center(face).Z();
        if (n.Z() < -0.866 && z < best)
        {
            result = face;
            best = z;
        }
    }
    if (result.IsNull())
        throw GeometryError("cross_missing_bottom", "打断板没有可确认的底面，不能调整高度差。");
    if (std::abs(BRepAdaptor_Surface(result).Plane().Axis().Direction().Z()) < 1 - 1e-6)
        throw GeometryError("cross_sloped_bottom", "高度差调整目前需要水平底面；请先放平工件，或关闭高度差。");
    return result;
}
struct Support
{
    double low{}, high{};
};
std::optional<Support> supportFor(const TopoDS_Shape &segment, const std::map<std::string, TopoDS_Shape> &shapes,
                                  const PlateEdit &r)
{
    const auto foot = bottom(segment);
    const auto footCenter = center(foot);
    double best = 51;
    std::optional<Support> result;
    for (const auto &[id, shape] : shapes)
    {
        if (id == r.target || id == r.parts.front())
            continue;
        try
        {
            const auto profile = plateProfile(shape);
            if (std::abs(profile.plane.Axis().Direction().Z()) < 1 - 1e-6)
                continue;
            auto face = profile.front;
            auto other = profile.back;
            if (center(face).Z() < center(other).Z())
                std::swap(face, other);
            GProp_GProps area;
            BRepGProp::SurfaceProperties(face, area);
            if (area.Mass() < 10000)
                continue;
            const double high = center(face).Z(), low = center(other).Z();
            const double gap = std::abs(footCenter.Z() - high);
            if (gap > 50 || gap >= best || low > footCenter.Z() + 1e-6)
                continue;
            // A distant horizontal plate must not act as support merely because its height matches.
            BRepClass_FaceClassifier covered(face, gp_Pnt(footCenter.X(), footCenter.Y(), high), 1e-6);
            if (covered.State() != TopAbs_IN && covered.State() != TopAbs_ON)
                continue;
            result = Support{low, high};
            best = gap;
        }
        catch (const GeometryError &)
        { /* Non-plate components cannot be a supporting web. */
        }
    }
    return result;
}
TopoDS_Shape oneSolid(const TopoDS_Shape &shape)
{
    const auto solids = plateSolids(shape);
    if (solids.size() != 1)
        throw GeometryError("cross_invalid_segment", "高度调整产生空板段或不连通板段，未应用。");
    return solids.front();
}
TopoDS_Shape adjust(const TopoDS_Shape &source, const TopoDS_Shape &a, const PlateProfile &profile,
                    const gp_Pnt &crossCenter, const std::map<std::string, TopoDS_Shape> &shapes, const PlateEdit &r)
{
    if (r.heightOffset == 0)
        return source;
    const auto foot = bottom(source);
    double bTop = -1e100;
    for (TopExp_Explorer it(source, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane ||
            std::abs(surface.Plane().Axis().Direction().Dot(profile.plane.Axis().Direction())) < 1 - 1e-6)
            continue;
        const auto p = center(face);
        if (std::min(std::abs(distance(profile.plane, p)),
                     std::abs(distance(BRepAdaptor_Surface(profile.back).Plane(), p))) <= 1.5)
            bTop = std::max(bTop, top(face));
    }
    if (bTop == -1e100)
        bTop = top(source);
    double aTop = -1e100;
    IntCurvesFace_ShapeIntersector ray;
    ray.Load(a, 1e-7);
    ray.Perform(gp_Lin(crossCenter, gp_Dir(0, 0, 1)), 0, 1e6);
    if (ray.IsDone())
        for (int i = 1; i <= ray.NbPnt(); ++i)
            aTop = std::max(aTop, ray.Pnt(i).Z());
    if (aTop == -1e100)
        aTop = top(a);
    const double delta = aTop - bTop - r.heightOffset;
    if (std::abs(delta) < .1)
        return source;
    auto shape = source;
    const auto support = supportFor(source, shapes, r);
    const double footZ = center(foot).Z();
    if (support && delta > 0 && footZ + delta > support->high + .5)
    {
        // Extend the bottom before lifting, so the raised segment still reaches the supporting web.
        const auto extension = BRepPrimAPI_MakePrism(foot, gp_Vec(0, 0, support->high - footZ - delta)).Shape();
        BRepAlgoAPI_Fuse fuse;
        fuse.SetNonDestructive(true);
        fuse.SetArguments({source});
        fuse.SetTools({extension});
        fuse.Build();
        if (!fuse.IsDone())
            throw GeometryError("cross_extend_failed", "打断板底部延长失败。");
        ShapeUpgrade_UnifySameDomain unify(fuse.Shape(), true, true, false);
        unify.Build();
        shape = oneSolid(unify.Shape());
    }
    gp_Trsf transform;
    transform.SetTranslation(gp_Vec(0, 0, delta));
    shape = BRepBuilderAPI_Transform(shape, transform, true).Shape();
    // Native behavior permits penetration into the web, but never below its bottom face.
    if (support && delta < 0 && footZ + delta < support->low - .5)
        shape = plateHalf(shape, {{}, {0, 0, support->low}, {0, 0, 1}, true});
    return oneSolid(shape);
}
} // namespace
PlateShapeEdit optimizeCrossPlateShapes(const std::map<std::string, TopoDS_Shape> &shapes, const PlateEdit &r,
                                        std::stop_token stop)
{
    check(stop);
    if (r.parts.size() != 1 || r.parts.front() == r.target || !shapes.contains(r.target) ||
        !shapes.contains(r.parts.front()))
        throw GeometryError("invalid_cross_selection", "请选择一个连续板 A 和一个不同的打断板 B。");
    if (!std::isfinite(r.heightOffset) || r.heightOffset < 0 || r.heightOffset > 10)
        throw GeometryError("invalid_height_offset", "高度差必须是 0–10 mm 的有限数；0 表示关闭。");
    const auto &a = shapes.at(r.target), &b = shapes.at(r.parts.front());
    const auto pa = plateProfile(a), pb = plateProfile(b);
    const auto na = pa.plane.Axis().Direction(), nb = pb.plane.Axis().Direction();
    if (std::abs(na.Z()) > .866 || std::abs(nb.Z()) > .866 || std::abs(na.Dot(nb)) > .999)
        throw GeometryError("cross_invalid_orientation", "需要两块交叉立板；底板或平行板不能作为交叉板。");
    BRepExtrema_DistShapeShape near(a, b);
    if (!near.IsDone() || near.Value() >= 1.0)
        throw GeometryError("cross_no_contact", "A 与 B 没有交叉或贴合（间距需小于 1 mm）。");
    const auto front = pa.plane, back = BRepAdaptor_Surface(pa.back).Plane();
    std::vector<TopoDS_Shape> pieces;
    int discarded = 0;
    for (const auto &plane : {front, back})
    {
        check(stop);
        const auto p = plane.Location();
        const auto n = plane.Axis().Direction();
        const bool atFront = std::abs(distance(front, p)) < 1e-7;
        const bool outsidePositive = distance(plane, (atFront ? back : front).Location()) < 0;
        const auto half = plateHalf(b, {{}, {p.X(), p.Y(), p.Z()}, {n.X(), n.Y(), n.Z()}, outsidePositive});
        for (const auto &solid : plateSolids(half))
        {
            Bnd_OBB box;
            BRepBndLib::AddOBB(solid, box, true, false, true);
            if (plateVolume(solid) < 1000 - 1e-6 ||
                2 * std::min({box.XHSize(), box.YHSize(), box.ZHSize()}) < 10 - 1e-6)
            {
                ++discarded;
                continue;
            }
            pieces.push_back(solid);
        }
    }
    if (pieces.empty())
        throw GeometryError("cross_no_valid_segments", "打断后没有有效板段（体积至少 1000 mm³，最小尺寸至少 10 mm）。");
    BRepAlgoAPI_Common common;
    common.SetNonDestructive(true);
    common.SetArguments({a});
    common.SetTools({b});
    common.Build();
    gp_Pnt crossing = center(a);
    if (common.IsDone() && plateVolume(common.Shape()) > 1e-7)
    {
        GProp_GProps mass;
        BRepGProp::VolumeProperties(common.Shape(), mass);
        crossing = mass.CentreOfMass();
    }
    for (auto &piece : pieces)
    {
        check(stop);
        piece = adjust(piece, a, pa, crossing, shapes, r);
    }
    std::sort(pieces.begin(), pieces.end(),
              [](const auto &x, const auto &y)
              {
                  const auto a = center(x), b = center(y);
                  return std::array{a.X(), a.Y(), a.Z()} < std::array{b.X(), b.Y(), b.Z()};
              });
    check(stop);
    PlateShapeEdit result;
    result.replacements[r.parts.front()] = std::move(pieces);
    result.reports.push_back("连续板 A 保持不变；B 生成 " + std::to_string(result.replacements.begin()->second.size()) +
                             " 个板段。");
    if (discarded)
        result.reports.push_back("已按交叉板规则滤除 " + std::to_string(discarded) + " 个小板段（1000 mm³ / 10 mm）。");
    return result;
}
} // namespace reverse::detail
