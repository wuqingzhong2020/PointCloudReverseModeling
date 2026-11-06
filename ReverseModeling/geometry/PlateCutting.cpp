/* 文件描述：有限半空间切割以及屏幕端部框选到工程坐标切割面的转换。 */
#include "geometry/PlateEditing.h"
#include "geometry/PlateOutlineTreatment.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>

namespace reverse::detail
{
double plateVolume(const TopoDS_Shape &shape)
{
    GProp_GProps p;
    BRepGProp::VolumeProperties(shape, p);
    return std::abs(p.Mass());
}
std::vector<TopoDS_Shape> plateSolids(const TopoDS_Shape &shape)
{
    std::vector<TopoDS_Shape> result;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next())
    {
        if (!BRepCheck_Analyzer(it.Current()).IsValid() || plateVolume(it.Current()) < 1e-7)
            throw GeometryError("invalid_plate_result", "结果包含无效或退化实体，整批未应用。");
        result.push_back(it.Current());
    }
    // 几何位置决定子件顺序，不依赖 OCCT 布尔遍历次序。
    std::sort(result.begin(), result.end(),
              [](const auto &a, const auto &b)
              {
                  GProp_GProps pa, pb;
                  BRepGProp::VolumeProperties(a, pa);
                  BRepGProp::VolumeProperties(b, pb);
                  const auto ca = pa.CentreOfMass(), cb = pb.CentreOfMass();
                  return std::array{ca.X(), ca.Y(), ca.Z()} < std::array{cb.X(), cb.Y(), cb.Z()};
              });
    return result;
}
TopoDS_Shape plateHalf(const TopoDS_Shape &shape, const PlateCut &cut)
{
    const auto &v = cut.normal;
    const auto &p = cut.origin;
    if (!std::isfinite(v.x + v.y + v.z + p.x + p.y + p.z) || std::hypot(v.x, v.y, v.z) < 1e-10)
        throw GeometryError("invalid_cut", "截断面的原点/法向必须有限，法向不能为零。");
    gp_Pnt origin(p.x, p.y, p.z);
    gp_Dir normal(v.x, v.y, v.z);
    Bnd_Box box;
    BRepBndLib::Add(shape, box);
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    const double size = 2 * (origin.Distance(gp_Pnt(x0, y0, z0)) + origin.Distance(gp_Pnt(x1, y1, z1)) + 1);
    auto face = BRepBuilderAPI_MakeFace(gp_Pln(origin, normal), -size, size, -size, size).Face();
    const auto half = BRepPrimAPI_MakePrism(face, gp_Vec(normal) * (cut.positive ? size : -size)).Shape();
    BRepAlgoAPI_Common operation;
    operation.SetNonDestructive(true);
    operation.SetArguments({shape});
    operation.SetTools({half});
    operation.Build();
    if (!operation.IsDone())
        throw GeometryError("cut_failed", "截断布尔计算失败，请检查平面和板件。");
    return operation.Shape();
}
namespace
{
std::array<double, 2> project(const gp_Pnt &p, const CloudSelectionRect &r)
{
    const auto &m = r.viewProjection;
    const double w = m[12] * p.X() + m[13] * p.Y() + m[14] * p.Z() + m[15];
    if (w <= 1e-12)
        throw GeometryError("crop_behind_camera", "板件位于相机后方，请正视板件后框选。");
    return {(m[0] * p.X() + m[1] * p.Y() + m[2] * p.Z() + m[3]) / w,
            (m[4] * p.X() + m[5] * p.Y() + m[6] * p.Z() + m[7]) / w};
}
} // namespace
void resolvePlateCrop(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &request)
{
    if (!request.rectangle)
        return;
    const auto r = *request.rectangle;
    if (!std::all_of(r.viewProjection.begin(), r.viewProjection.end(), [](double x) { return std::isfinite(x); }) ||
        !std::isfinite(r.minX + r.maxX + r.minY + r.maxY) || r.minX >= r.maxX || r.minY >= r.maxY)
        throw GeometryError("invalid_rectangle", "框选矩形或投影参数无效。");
    request.cuts.clear();
    for (const auto &id : request.parts)
    {
        const auto &shape = shapes.at(id);
        const auto profile = plateProfile(shape);
        const auto ax = profile.plane.Position();
        gp_Dir primary = ax.XDirection();
        double longest = 0;
        for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
        {
            BRepAdaptor_Curve c(TopoDS::Edge(it.Current()));
            if (c.GetType() != GeomAbs_Line)
                continue;
            gp_Vec edge(c.Value(c.FirstParameter()), c.Value(c.LastParameter()));
            if (edge.Magnitude() > longest && std::abs(edge.Normalized().Dot(gp_Vec(ax.Direction()))) < 1e-6)
            {
                longest = edge.Magnitude();
                primary = gp_Dir(edge);
            }
        }
        const gp_Dir secondary(gp_Vec(ax.Direction()).Crossed(gp_Vec(primary)));
        std::vector<PlateCut> candidates;
        bool hit = false;
        for (const auto dir : {primary, secondary})
        {
            double low = 1e100, high = -1e100, selectedLow = 1e100, selectedHigh = -1e100;
            for (TopExp_Explorer it(shape, TopAbs_EDGE); it.More(); it.Next())
            {
                BRepAdaptor_Curve curve(TopoDS::Edge(it.Current()));
                const int segments = curve.GetType() == GeomAbs_Line ? 1 : 256;
                for (int i = 0; i < segments; ++i)
                {
                    const auto a = curve.Value(curve.FirstParameter() +
                                               (curve.LastParameter() - curve.FirstParameter()) * i / segments);
                    const auto b = curve.Value(curve.FirstParameter() +
                                               (curve.LastParameter() - curve.FirstParameter()) * (i + 1) / segments);
                    (void)project(a, r);
                    (void)project(b, r);
                    const double ta = gp_Vec(ax.Location(), a).Dot(gp_Vec(dir)),
                                 tb = gp_Vec(ax.Location(), b).Dot(gp_Vec(dir));
                    low = std::min({low, ta, tb});
                    high = std::max({high, ta, tb});
                    double enter = 0, leave = 1;
                    const auto &m = r.viewProjection;
                    // 齐次裁剪：直边与矩形边界精确求交，不按固定采样百分比切板。
                    const auto clip = [&](double fa, double fb)
                    {
                        if (fa < 0 && fb < 0)
                        {
                            enter = 2;
                            return;
                        }
                        if (fa < 0)
                            enter = std::max(enter, fa / (fa - fb));
                        if (fb < 0)
                            leave = std::min(leave, fa / (fa - fb));
                    };
                    const auto component = [&](int row, const gp_Pnt &p)
                    { return m[row * 4] * p.X() + m[row * 4 + 1] * p.Y() + m[row * 4 + 2] * p.Z() + m[row * 4 + 3]; };
                    for (int k = 0; k < 4; ++k)
                    {
                        const int row = k / 2;
                        const double limit = k == 0 ? r.minX : k == 1 ? r.maxX : k == 2 ? r.minY : r.maxY;
                        const double sign = k % 2 ? -1 : 1;
                        clip(sign * (component(row, a) - limit * component(3, a)),
                             sign * (component(row, b) - limit * component(3, b)));
                    }
                    if (enter <= leave)
                    {
                        selectedLow = std::min({selectedLow, ta + (tb - ta) * enter, ta + (tb - ta) * leave});
                        selectedHigh = std::max({selectedHigh, ta + (tb - ta) * enter, ta + (tb - ta) * leave});
                    }
                }
            }
            if (selectedLow > selectedHigh)
                continue;
            hit = true;
            const double eps = std::max(1e-6, (high - low) * 1e-8);
            const bool atLow = selectedLow <= low + eps, atHigh = selectedHigh >= high - eps;
            if (atLow == atHigh || selectedHigh - selectedLow < eps)
                continue;
            const auto point = ax.Location().Translated(gp_Vec(dir) * (atLow ? selectedHigh : selectedLow));
            candidates.push_back({id, {point.X(), point.Y(), point.Z()}, {dir.X(), dir.Y(), dir.Z()}, atLow});
        }
        if (!hit)
            continue;
        if (candidates.size() != 1)
            throw GeometryError("invalid_crop",
                                "部件 " + id +
                                    "：框选未形成唯一端部切割，请框住整个端部宽度，不能只框角落、中部或整件。");
        request.cuts.push_back(candidates.front());
    }
    if (request.cuts.empty())
        throw GeometryError("empty_crop", "框选没有命中所选板件的端部。");
    request.rectangle.reset();
}
} // namespace reverse::detail
