/* 文件描述：多个源板相对明确目标板的延长、截取、分割与目标厚度主导的合并。 */
#include "geometry/PlateEditing.h"
#include "geometry/assembly/AssemblyPlateEditing.h"
#include "geometry/PlateHoles.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateThickness.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>

namespace reverse::detail
{
namespace
{
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "操作已取消，整批未应用。");
}
gp_Pnt center(const TopoDS_Shape &s)
{
    GProp_GProps p;
    BRepGProp::SurfaceProperties(s, p);
    return p.CentreOfMass();
}
double signedDistance(const gp_Pln &p, const gp_Pnt &v)
{
    return gp_Vec(p.Location(), v).Dot(gp_Vec(p.Axis().Direction()));
}
PlateCut cutPlane(const std::string &id, const gp_Pln &p, bool positive)
{
    const auto o = p.Location();
    const auto n = p.Axis().Direction();
    return {id, {o.X(), o.Y(), o.Z()}, {n.X(), n.Y(), n.Z()}, positive};
}
TopoDS_Shape united(const TopoDS_Shape &a, const TopoDS_Shape &b)
{
    BRepAlgoAPI_Fuse op;
    op.SetNonDestructive(true);
    op.SetArguments({a});
    op.SetTools({b});
    op.Build();
    if (!op.IsDone())
        throw GeometryError("plate_fuse_failed", "板件融合失败，请检查接触范围。");
    ShapeUpgrade_UnifySameDomain unify(op.Shape(), true, true, false);
    unify.Build();
    const auto solids = plateSolids(unify.Shape());
    if (solids.size() != 1)
        throw GeometryError("disconnected_plate", "板件没有形成一个连通实体，请减小间隙或检查目标板。");
    return solids.front();
}
TopoDS_Face contactFace(const TopoDS_Shape &source, const TopoDS_Shape &target, PlateContact &contact)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(target, TopAbs_FACE, faces);
    if (contact.face)
    {
        if (contact.face > static_cast<unsigned>(faces.Extent()) ||
            BRepAdaptor_Surface(TopoDS::Face(faces(static_cast<int>(contact.face)))).GetType() != GeomAbs_Plane)
            throw GeometryError("invalid_target_face", "目标面不存在或不是平面，请重新选择目标面。");
        return TopoDS::Face(faces(static_cast<int>(contact.face)));
    }
    const auto profile = plateProfile(source);
    double best = 1e100;
    int found = 0;
    bool tied = false;
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const auto face = TopoDS::Face(faces(i));
        BRepAdaptor_Surface s(face);
        if (s.GetType() != GeomAbs_Plane ||
            std::abs(s.Plane().Axis().Direction().Dot(profile.plane.Axis().Direction())) > 0.999)
            continue;
        // 源板位于目标面的平面延长线上时，该侧面并不朝向源板。
        if (std::abs(signedDistance(s.Plane(), center(source))) < 1e-6)
            continue;
        const gp_Vec towardSource(center(target), center(source));
        if (towardSource.Magnitude() > 1e-6 &&
            std::abs(towardSource.Normalized().Dot(gp_Vec(s.Plane().Axis().Direction()))) < 1e-5)
            continue;
        BRepExtrema_DistShapeShape distance(source, face);
        if (!distance.IsDone())
            continue;
        const double d = distance.Value();
        if (d < best - 1e-5)
        {
            best = d;
            found = i;
            tied = false;
        }
        else if (std::abs(d - best) < 1e-5)
            tied = true;
    }
    if (!found || tied)
        throw GeometryError("ambiguous_contact", "目标接触面不唯一，请在面板明确选择目标面编号。");
    contact.face = found;
    return TopoDS::Face(faces(found));
}
TopoDS_Shape extend(const TopoDS_Shape &source, const TopoDS_Face &target, double maximum, double tolerance)
{
    const auto profile = plateProfile(source);
    const auto plane = BRepAdaptor_Surface(target).Plane();
    gp_Vec dir(plane.Axis().Direction()), normal(profile.plane.Axis().Direction());
    dir -= normal * dir.Dot(normal);
    if (dir.Magnitude() < 1e-6)
        throw GeometryError("invalid_extension_direction", "目标面平行源板主面，不能沿板厚方向延长。");
    dir.Normalize();
    if (signedDistance(plane, center(source)) * dir.Dot(gp_Vec(plane.Axis().Direction())) > 0)
        dir.Reverse();
    TopoDS_Face end;
    double best = 1e100;
    for (TopExp_Explorer it(source, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface s(face);
        if (s.GetType() != GeomAbs_Plane ||
            std::abs(s.Plane().Axis().Direction().Dot(profile.plane.Axis().Direction())) > 1e-5)
            continue;
        gp_Vec outward(s.Plane().Axis().Direction());
        if (face.Orientation() == TopAbs_REVERSED)
            outward.Reverse();
        if (outward.Dot(dir) < 0.5)
            continue;
        BRepExtrema_DistShapeShape d(face, target);
        if (d.IsDone() && d.Value() < best)
        {
            best = d.Value();
            end = face;
        }
    }
    if (end.IsNull())
        throw GeometryError("missing_plate_end", "未找到面向目标的平直板端，请换目标面。");
    double distance = 0, minimum = 1e100;
    for (TopExp_Explorer it(end, TopAbs_VERTEX); it.More(); it.Next())
    {
        const double d = -signedDistance(plane, BRep_Tool::Pnt(TopoDS::Vertex(it.Current()))) /
                         dir.Dot(gp_Vec(plane.Axis().Direction()));
        distance = std::max(distance, d);
        minimum = std::min(minimum, d);
    }
    if (minimum < -tolerance)
        throw GeometryError("already_crossing_target", "板端已越过目标面，请使用截取到板。");
    if (distance > maximum)
        throw GeometryError("extension_too_far", "所需延长 " + std::to_string(distance) + " mm 超过最大延长距离。");
    auto result = source;
    if (distance > 1e-7)
    {
        const auto swept = BRepPrimAPI_MakePrism(end, dir * (distance + std::max(1.0, tolerance)), true).Shape();
        const auto clipped = plateHalf(swept, cutPlane({}, plane, signedDistance(plane, center(source)) > 0));
        result = united(source, clipped);
    }
    // 比较整个端面的覆盖面积，不能用最近一点接触冒充整个板端贴合。
    double endArea = 0, coveredArea = 0;
    for (TopExp_Explorer it(result, TopAbs_FACE); it.More(); it.Next())
    {
        const auto face = TopoDS::Face(it.Current());
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane ||
            std::abs(surface.Plane().Axis().Direction().Dot(plane.Axis().Direction())) < 1 - 1e-8 ||
            std::abs(signedDistance(plane, surface.Plane().Location())) > 1e-6)
            continue;
        GProp_GProps area;
        BRepGProp::SurfaceProperties(face, area);
        endArea += area.Mass();
        BRepAlgoAPI_Common common;
        common.SetNonDestructive(true);
        common.SetArguments({face});
        common.SetTools({target});
        common.Build();
        if (common.IsDone())
        {
            BRepGProp::SurfaceProperties(common.Shape(), area);
            coveredArea += area.Mass();
        }
    }
    if (endArea < 1e-8 || coveredArea < endArea - std::max(1e-5, endArea * 1e-6))
        throw GeometryError("target_not_covering_end", "目标板没有完整覆盖源板端面，请换目标板或接触面。");
    return result;
}
gp_Dir longDirection(const TopoDS_Shape &shape, const gp_Dir &normal)
{
    double length = 0;
    gp_Dir direction(1, 0, 0);
    for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
    {
        BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
        if (c.GetType() != GeomAbs_Line)
            continue;
        gp_Vec v(c.Value(c.FirstParameter()), c.Value(c.LastParameter()));
        if (v.Magnitude() > length && std::abs(v.Normalized().Dot(gp_Vec(normal))) < 1e-6)
        {
            length = v.Magnitude();
            direction = gp_Dir(v);
        }
    }
    if (!length)
        throw GeometryError("merge_axes_missing", "没有可确认面内方向的直线边，不能自动校正合并方向。");
    return direction;
}
TopoDS_Shape alignSource(const TopoDS_Shape &source, const TopoDS_Shape &target, const PlateEdit &r)
{
    const auto a = inspectPlateThickness(source), b = inspectPlateThickness(target);
    gp_Dir na(a.normal.x, a.normal.y, a.normal.z), nb(b.normal.x, b.normal.y, b.normal.z);
    if (na.Dot(nb) < 0)
        na.Reverse();
    const double angle = na.Angle(nb);
    if (angle > r.normalAngle * std::numbers::pi / 180 + 1e-8)
        throw GeometryError("merge_angle", "板面夹角超出法向容差，不能合并。");
    auto shape = resizePlateThickness(source, b.thicknessMm);
    const gp_Pnt pivot(a.centerMm.x, a.centerMm.y, a.centerMm.z);
    if (angle > 1e-9)
    {
        gp_Trsf rotate;
        rotate.SetRotation(gp_Ax1(pivot, gp_Dir(gp_Vec(na).Crossed(gp_Vec(nb)))), angle);
        shape = BRepBuilderAPI_Transform(shape, rotate, true).Shape();
    }
    const auto da = longDirection(shape, nb), db = longDirection(target, nb);
    double inplane = std::acos(std::clamp(std::abs(da.Dot(db)), 0.0, 1.0));
    inplane = std::min(inplane, std::numbers::pi / 2 - inplane);
    if (inplane > r.inplaneAngle * std::numbers::pi / 180 + 1e-8)
        throw GeometryError("merge_inplane_angle", "面内方向超出容差，不能合并。");
    const double correction =
        std::remainder(std::atan2(gp_Vec(nb).Dot(gp_Vec(da).Crossed(gp_Vec(db))), da.Dot(db)), std::numbers::pi / 2);
    if (std::abs(correction) > 1e-9)
    {
        gp_Trsf rotate;
        rotate.SetRotation(gp_Ax1(pivot, nb), correction);
        shape = BRepBuilderAPI_Transform(shape, rotate, true).Shape();
    }
    gp_Trsf shift;
    shift.SetTranslation(gp_Vec(nb) * gp_Vec(pivot, gp_Pnt(b.centerMm.x, b.centerMm.y, b.centerMm.z)).Dot(gp_Vec(nb)));
    return BRepBuilderAPI_Transform(shape, shift, true).Shape();
}
} // namespace
void prepareDirectPlateEdit(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request,
                            bool all, std::stop_token stop, const std::function<void(std::string_view)> &)
{
    request.direct = true;
    request.all = all;
    prepareAssemblyPlateEdit(shapes, request, stop);
}

PlateShapeEdit editPlateShapes(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &r, std::stop_token stop)
{
    if (r.operation == "cross")
        return optimizeCrossPlateShapes(shapes, r, stop);
    if (r.operation == "cap")
        return optimizeEndCapShapes(shapes, r, stop);
    if (r.direct)
        return editAssemblyPlateShapes(shapes, r, stop);
    if (r.parts.empty())
        throw GeometryError("missing_parts", "请选择至少一个实体板件。");
    std::set<std::string> ids(r.parts.begin(), r.parts.end());
    if (ids.size() != r.parts.size())
        throw GeometryError("duplicate_part", "部件列表不能重复。");
    for (const auto &id : ids)
        if (!shapes.contains(id))
            throw GeometryError("unknown_part", "部件不存在：" + id);
    const bool relation =
        r.operation == "extend" || r.operation == "trim" || r.operation == "merge" || r.operation == "split";
    if (relation && (!shapes.contains(r.target) || ids.contains(r.target)))
        throw GeometryError("invalid_target", "请选择不在源列表中的目标板。");
    for (const auto v : {r.tolerance, r.maxDistance, r.clearance, r.normalAngle, r.inplaneAngle, r.maxGap,
                         r.maxHoleArea, r.notchWidth, r.notchDepth, r.simplifyTolerance})
        if (!std::isfinite(v) || v < 0 || v > 1e9)
            throw GeometryError("invalid_plate_parameter", "距离、角度和面积必须为非负有限数。");
    if (r.operation == "crop")
        resolvePlateCrop(shapes, r);
    const auto validateIds = [&](const auto &items, const auto &key)
    {
        std::set<std::string> seen;
        for (const auto &item : items)
            if (!ids.contains(key(item)) || !seen.insert(key(item)).second)
                throw GeometryError("invalid_part_parameters", "逐板参数包含重复或不在源列表的部件。");
    };
    validateIds(r.contacts, [](const auto &c) { return c.part; });
    validateIds(r.cuts, [](const auto &c) { return c.part; });
    validateIds(r.holes, [](const auto &h) { return h.partId; });
    PlateShapeEdit result;
    TopoDS_Shape merged = relation ? BRepBuilderAPI_Copy(shapes.at(r.target), true, false).Shape() : TopoDS_Shape{};
    for (const auto &id : ids)
    {
        check(stop);
        try
        {
            const auto &source = shapes.at(id);
            if (r.operation != "repair")
                (void)inspectPlateThickness(source);
            auto shape = BRepBuilderAPI_Copy(source, true, false).Shape();
            std::vector<TopoDS_Shape> replacements;
            if (r.operation == "repair")
            {
                std::string report;
                shape = repairPlate(source, r, report);
                result.reports.push_back(id + "：" + report);
                if (shape.IsSame(source))
                    continue;
            }
            else if (r.operation == "holes")
            {
                const auto it =
                    std::find_if(r.holes.begin(), r.holes.end(), [&](const auto &h) { return h.partId == id; });
                if (it == r.holes.end())
                    throw GeometryError("missing_hole", "缺少该板的底边/孔设置。");
                shape = cutPlateHole(shape, *it, stop);
            }
            else if (r.operation == "crop" || r.operation == "trim")
            {
                const auto it = std::find_if(r.cuts.begin(), r.cuts.end(), [&](const auto &c) { return c.part == id; });
                if (it == r.cuts.end())
                {
                    if (r.operation == "crop")
                    {
                        result.reports.push_back(id + "：未命中，保持不变。");
                        continue;
                    }
                    throw GeometryError("missing_cut", "请明确该板截断面的保留侧。");
                }
                PlateCut cut = *it;
                if (r.operation == "trim")
                {
                    auto contact =
                        std::find_if(r.contacts.begin(), r.contacts.end(), [&](const auto &c) { return c.part == id; });
                    if (contact == r.contacts.end())
                        throw GeometryError("missing_contact", "请选择目标接触面。");
                    const auto face = contactFace(shape, shapes.at(r.target), *contact);
                    cut = cutPlane(id, BRepAdaptor_Surface(face).Plane(), cut.positive);
                    const double sign = cut.positive ? 1 : -1;
                    cut.origin.x += cut.normal.x * r.clearance * sign;
                    cut.origin.y += cut.normal.y * r.clearance * sign;
                    cut.origin.z += cut.normal.z * r.clearance * sign;
                    *it = cut;
                }
                shape = plateHalf(shape, cut);
                if (plateSolids(shape).size() != 1)
                    throw GeometryError("invalid_end_cut", "切割会整件删除或生成多块，请改用删除/分割到板。");
                if (std::abs(plateVolume(shape) - plateVolume(source)) < 1e-5)
                {
                    result.reports.push_back(id + "：切割面未切除材料，保持不变。");
                    continue;
                }
            }
            else if (r.operation == "split")
            {
                const auto &target = shapes.at(r.target);
                const auto profile = plateProfile(target);
                BRepAlgoAPI_Common overlap;
                overlap.SetNonDestructive(true);
                overlap.SetArguments({shape});
                overlap.SetTools({target});
                overlap.Build();
                if (!overlap.IsDone() || plateVolume(overlap.Shape()) < 1e-6)
                    throw GeometryError("no_intersection", "目标板未穿入源板，不能分割。");
                const auto front = profile.plane, back = BRepAdaptor_Surface(profile.back).Plane();
                const double direction = signedDistance(front, back.Location());
                for (auto cut : {cutPlane(id, front, direction < 0),
                                 cutPlane(id, back, signedDistance(back, front.Location()) < 0)})
                {
                    const double sign = cut.positive ? 1 : -1;
                    cut.origin.x += cut.normal.x * r.clearance * sign;
                    cut.origin.y += cut.normal.y * r.clearance * sign;
                    cut.origin.z += cut.normal.z * r.clearance * sign;
                    auto halves = plateSolids(plateHalf(shape, cut));
                    replacements.insert(replacements.end(), halves.begin(), halves.end());
                }
                if (replacements.empty())
                    throw GeometryError("empty_split", "目标厚度覆盖整个源板，不能整件删除。");
                std::sort(replacements.begin(), replacements.end(),
                          [](const auto &a, const auto &b)
                          {
                              const auto x = center(a), y = center(b);
                              return std::array{x.X(), x.Y(), x.Z()} < std::array{y.X(), y.Y(), y.Z()};
                          });
            }
            else if (r.operation == "extend")
            {
                auto contact =
                    std::find_if(r.contacts.begin(), r.contacts.end(), [&](const auto &c) { return c.part == id; });
                if (contact == r.contacts.end())
                {
                    r.contacts.push_back({id, 0});
                    contact = std::prev(r.contacts.end());
                }
                shape = extend(shape, contactFace(shape, shapes.at(r.target), *contact), r.maxDistance, r.tolerance);
                if (std::abs(plateVolume(shape) - plateVolume(source)) < 1e-5)
                {
                    result.reports.push_back(id + "：已贴合，无变化。");
                    continue;
                }
            }
            else if (r.operation == "merge")
            {
                shape = alignSource(shape, shapes.at(r.target), r);
                BRepExtrema_DistShapeShape d(shape, shapes.at(r.target));
                if (!d.IsDone() || d.Value() > r.maxGap + 1e-6)
                    throw GeometryError("merge_gap", "源板与目标板的间隙超出允许值。");
                if (d.Value() > 1e-6)
                {
                    PlateContact c{id, 0};
                    shape = extend(shape, contactFace(shape, shapes.at(r.target), c), r.maxGap, 1e-6);
                }
                merged = united(merged, shape);
                result.replacements[id] = {};
                continue;
            }
            else
                throw GeometryError("unknown_plate_operation", "未知板件编辑操作。");
            if (replacements.empty())
                replacements = plateSolids(shape);
            result.replacements[id] = std::move(replacements);
            if (r.operation != "repair")
                result.reports.push_back(id + "：计算完成，生成 " + std::to_string(result.replacements[id].size()) +
                                         " 个部件。");
        }
        catch (const GeometryError &e)
        {
            throw GeometryError(e.code(), "部件 " + id + "：" + e.what() + " 整批未应用。");
        }
    }
    if (r.operation == "merge")
        result.replacements[r.target] = {merged};
    check(stop);
    return result;
}
} // namespace reverse::detail
