/* 文件描述：按板件连接关系识别腹板、两侧翼板与端部封板，按当前边缘调整错边或凸边，保留其余拓扑。 */
#include "geometry/PlateEditing.h"
#include "geometry/PlateOutlineTreatment.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
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
constexpr double epsilon = 1e-5;
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "封板优化已取消，未修改模型。");
}
struct Box
{
    double x0, y0, z0, x1, y1, z1;
};
struct Frame
{
    gp_Dir x, y, z;
    gp_Pnt origin;
    gp_Trsf local;
    Frame(gp_Dir normal, gp_Dir webNormal, gp_Pnt center, gp_Pnt webCenter) : x(normal), z(webNormal), origin(center)
    {
        // The assembly defines the frame, independently of scan pose or camera.
        if (gp_Vec(webCenter, center).Dot(gp_Vec(z)) < 0)
            z.Reverse();
        y = z.Crossed(x);
        z = x.Crossed(y);
        local.SetTransformation(gp_Ax3(origin, z, x));
    }
    gp_Pnt point(double a, double b, double c) const
    {
        return origin.Translated(gp_Vec(x) * a + gp_Vec(y) * b + gp_Vec(z) * c);
    }
    Box bounds(const TopoDS_Shape &shape) const
    {
        Bnd_Box box;
        const auto rotated = BRepBuilderAPI_Transform(shape, local, true).Shape();
        BRepBndLib::AddOptimal(rotated, box, false, false);
        if (box.IsVoid())
            throw GeometryError("cap_empty", "板件没有有效边界。");
        Box b;
        box.Get(b.x0, b.y0, b.z0, b.x1, b.y1, b.z1);
        return b;
    }
};
double gap(const TopoDS_Shape &a, const TopoDS_Shape &b)
{
    BRepExtrema_DistShapeShape distance(a, b);
    return distance.IsDone() ? distance.Value() : 1e100;
}
struct Context
{
    Frame frame;
    EndCapInspection info;
    Box cap{}, web{};
    std::array<Box, 2> wings{};
    std::array<int, 2> sides{0, 1};
    bool lowEnd{};
};
Context inspect(const std::map<std::string, TopoDS_Shape> &shapes, const std::string &id, std::stop_token stop)
{
    check(stop);
    if (!shapes.contains(id))
        throw GeometryError("cap_unknown", "请选择一块有效的封板实体。");
    const auto &cap = shapes.at(id);
    const auto profile = plateProfile(cap);
    if (plateSolids(cap).size() != 1)
        throw GeometryError("cap_not_solid", "封板必须为单个等厚实体。");
    GProp_GProps mass;
    BRepGProp::VolumeProperties(cap, mass);
    const auto center = mass.CentreOfMass();
    std::map<std::string, PlateProfile> profiles;
    for (const auto &[name, shape] : shapes)
    {
        check(stop);
        if (name == id)
            continue;
        try
        {
            profiles.emplace(name, plateProfile(shape));
        }
        catch (const GeometryError &)
        {
        }
    }
    std::optional<Context> result;
    double best = 1e100;
    bool ambiguous = false;
    for (const auto &[name, p] : profiles)
    {
        check(stop);
        if (std::abs(p.plane.Axis().Direction().Dot(profile.plane.Axis().Direction())) > epsilon)
            continue;
        const auto &shape = shapes.at(name);
        BRepGProp::VolumeProperties(shape, mass);
        Context candidate{
            Frame(profile.plane.Axis().Direction(), p.plane.Axis().Direction(), center, mass.CentreOfMass())};
        const auto &f = candidate.frame;
        candidate.info.part = id;
        const auto c = candidate.cap = f.bounds(cap);
        const auto w = candidate.web = f.bounds(shape);
        const double length = w.x1 - w.x0, width = w.y1 - w.y0;
        const double cx = (c.x0 + c.x1) / 2, cy = (c.y0 + c.y1) / 2;
        if (length <= width || c.y1 - c.y0 <= .35 * width || cy < w.y0 || cy > w.y1 ||
            std::min(std::abs(cx - w.x0), std::abs(cx - w.x1)) >= .1 * length || w.z1 > c.z1 - epsilon ||
            std::abs(c.z0 - w.z1) > 50 || gap(cap, shape) > 50)
            continue;
        candidate.info.web = name;
        candidate.lowEnd = std::abs(c.x0 - w.x0) < std::abs(c.x1 - w.x1);
        std::array<double, 2> scores{1e100, 1e100};
        std::array<bool, 2> conflicts{};
        for (const auto &[wingName, wingProfile] : profiles)
        {
            check(stop);
            if (wingName == name)
                continue;
            if (std::abs(wingProfile.plane.Axis().Direction().Dot(f.y)) < 1 - epsilon)
                continue;
            const auto &wingShape = shapes.at(wingName);
            const auto b = f.bounds(wingShape);
            if (b.x1 - b.x0 < .5 * (w.x1 - w.x0) || b.z1 <= c.z0 || b.z0 >= c.z1 || b.x1 < c.x0 - 100 ||
                b.x0 > c.x1 + 100 || gap(cap, wingShape) > 100)
                continue;
            const int side = (b.y0 + b.y1 < w.y0 + w.y1) ? 0 : 1;
            const double inner = side == 0 ? b.y1 : b.y0;
            const double score = std::abs(inner - (side == 0 ? w.y0 : w.y1));
            if (score > 100)
                continue;
            if (score < scores[side] - epsilon)
            {
                scores[side] = score;
                conflicts[side] = false;
                candidate.wings[side] = b;
                const auto anchor = f.point((c.x0 + c.x1) / 2, side == 0 ? c.y0 : c.y1, c.z1);
                candidate.info.edges[side] = {
                    {anchor.X(), anchor.Y(), anchor.Z()}, wingName, wingProfile.thickness * .75};
            }
            else if (std::abs(score - scores[side]) <= epsilon)
                conflicts[side] = true;
        }
        if (candidate.info.edges[0].wing.empty() || candidate.info.edges[1].wing.empty() || conflicts[0] ||
            conflicts[1])
            continue;
        // Stable part IDs keep edge numbers fixed under rotation, opposite cap
        // face selection, and subsequent edits that reorder OCCT faces.
        if (candidate.info.edges[0].wing > candidate.info.edges[1].wing)
        {
            std::swap(candidate.info.edges[0], candidate.info.edges[1]);
            std::swap(candidate.sides[0], candidate.sides[1]);
        }
        const double score = std::abs(c.z0 - w.z1);
        if (score < best - epsilon)
        {
            best = score;
            ambiguous = false;
            result = candidate;
        }
        else if (std::abs(score - best) <= epsilon)
            ambiguous = true;
    }
    if (!result || ambiguous)
        throw GeometryError("cap_invalid_connections",
                            "无法唯一确定封板连接的腹板和两侧翼板。请选择梁端肋板，并检查连接关系是否完整。");
    return *result;
}
TopoDS_Shape oneSolid(const TopoDS_Shape &shape)
{
    const auto solids = plateSolids(shape);
    if (solids.size() != 1 || plateVolume(solids.front()) <= epsilon || !BRepCheck_Analyzer(solids.front()).IsValid())
        throw GeometryError("cap_invalid_result", "封板优化产生空板、断开的板段或无效实体，未应用。");
    return solids.front();
}
// Resize only the chosen end. Extension uses its actual planar end face, preserving holes and other contours.
TopoDS_Shape resizeEnd(const TopoDS_Shape &shape, const Frame &f, bool alongX, bool low, double target)
{
    const auto box = f.bounds(shape);
    const double minimum = alongX ? box.x0 : box.y0, maximum = alongX ? box.x1 : box.y1;
    const double old = low ? minimum : maximum;
    if (std::abs(target - old) < epsilon)
        return shape;
    if ((low && target >= maximum - 1) || (!low && target <= minimum + 1))
        throw GeometryError("cap_excessive_distance", "距离会切除整块板或留下不足 1 mm 的连接，请减小距离。");
    auto direction = alongX ? f.x : f.y;
    if (low)
        direction.Reverse();
    const auto origin = alongX ? f.point(target, 0, 0) : f.point(0, target, 0);
    if ((low && target > old) || (!low && target < old))
        return oneSolid(plateHalf(
            shape, {{}, {origin.X(), origin.Y(), origin.Z()}, {direction.X(), direction.Y(), direction.Z()}, false}));
    std::vector<TopoDS_Face> ends;
    for (TopExp_Explorer it(shape, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            continue;
        auto normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        const auto p = surface.Plane().Location();
        const double coordinate = gp_Vec(f.origin, p).Dot(gp_Vec(alongX ? f.x : f.y));
        if (normal.Dot(direction) > 1 - epsilon && std::abs(coordinate - old) < epsilon)
            ends.push_back(face);
    }
    if (ends.empty())
        throw GeometryError("cap_nonplanar_end", "待延伸板端不是完整的平面端部，暂不能自动延伸。");
    auto result = shape;
    for (const auto &face : ends)
    {
        const auto prism = BRepPrimAPI_MakePrism(face, gp_Vec(direction) * std::abs(target - old)).Shape();
        BRepAlgoAPI_Fuse fuse;
        fuse.SetNonDestructive(true);
        fuse.SetArguments({result});
        fuse.SetTools({prism});
        fuse.Build();
        if (!fuse.IsDone())
            throw GeometryError("cap_extend_failed", "板端延伸失败，未应用。");
        result = fuse.Shape();
    }
    ShapeUpgrade_UnifySameDomain unify(result, true, true, false);
    unify.Build();
    return oneSolid(unify.Shape());
}
} // namespace
EndCapInspection inspectEndCapShapes(const std::map<std::string, TopoDS_Shape> &shapes, const std::string &part,
                                     std::stop_token stop)
{
    return inspect(shapes, part, stop).info;
}
PlateShapeEdit optimizeEndCapShapes(const std::map<std::string, TopoDS_Shape> &shapes, const PlateEdit &r,
                                    std::stop_token stop)
{
    if (r.parts.size() != 1 || r.capEdge < 1 || r.capEdge > 2)
        throw GeometryError("cap_invalid_selection", "请选择一块封板及边缘 1 或边缘 2。");
    if (!std::isfinite(r.capDistance) || r.capDistance < 1 || r.capDistance > (r.capConvex ? 200 : 100))
        throw GeometryError("cap_invalid_distance", "错边距离需为 1–100 mm，凸边距离需为 1–200 mm。");
    const auto c = inspect(shapes, r.parts.front(), stop);
    const int i = r.capEdge - 1;
    const bool low = c.sides[i] == 0;
    const auto &edge = c.info.edges[i];
    const double distance = !r.capConvex && r.capAutomatic ? edge.automaticDistance : r.capDistance;
    if (!std::isfinite(distance) || distance < 1 || distance > (r.capConvex ? 200 : 100))
        throw GeometryError("cap_auto_distance", "该侧翼板的自动距离超出范围，请切换为自定义距离。");
    const auto wing = c.wings[c.sides[i]];
    const double inner = low ? wing.y1 : wing.y0, outer = low ? wing.y0 : wing.y1;
    const double target = outer + (low ? -1 : 1) * (r.capConvex ? distance : -distance);
    const double capInside = c.lowEnd ? c.cap.x1 : c.cap.x0;
    PlateShapeEdit result;
    auto webShape = resizeEnd(shapes.at(c.info.web), c.frame, false, low, inner);
    webShape = resizeEnd(webShape, c.frame, true, c.lowEnd, capInside);
    check(stop);
    auto wingShape = resizeEnd(shapes.at(edge.wing), c.frame, true, c.lowEnd, capInside);
    auto capShape = resizeEnd(shapes.at(c.info.part), c.frame, false, low, target);
    check(stop);
    for (const auto &[id, shape] :
         std::map<std::string, TopoDS_Shape>{{c.info.web, webShape}, {edge.wing, wingShape}, {c.info.part, capShape}})
    {
        const auto &old = shapes.at(id);
        if (shape.IsSame(old))
            continue;
        if (std::abs(plateProfile(shape).thickness - plateProfile(old).thickness) > epsilon)
            throw GeometryError("cap_thickness_changed", "优化会改变板厚，已取消整个操作。");
        result.replacements[id] = {oneSolid(shape)};
    }
    result.reports.push_back("边缘 " + std::to_string(r.capEdge) + (r.capConvex ? " 凸边 " : " 错边 ") +
                             std::to_string(distance) + " mm；另一边缘的封板端部保持不变。");
    return result;
}
} // namespace reverse::detail
