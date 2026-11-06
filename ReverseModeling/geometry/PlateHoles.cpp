/* 文件描述：在板件主面定位直线底边，以独立刀具体贯穿板厚切除缺口，保持源模型不可变。 */
#include "geometry/PlateHoles.h"
#include "geometry/Geometry.h"
#include "geometry/PlateOutlineTreatment.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass_FaceClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepTools.hxx>
#include <GProp_GProps.hxx>
#include <Message_ProgressIndicator.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <algorithm>
#include <cmath>
#include <gp_Ax2.hxx>
#include <tuple>

namespace reverse::detail
{
namespace
{
using ShapeMap = NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher>;
Vec3 vec(const gp_XYZ &p)
{
    return {p.X(), p.Y(), p.Z()};
}
gp_Vec vector(Vec3 p)
{
    return {p.x, p.y, p.z};
}
gp_Pnt point(Vec3 p)
{
    return {p.x, p.y, p.z};
}
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "操作已取消。");
}
class CutProgress final : public Message_ProgressIndicator
{
  public:
    explicit CutProgress(std::stop_token stop) : m_stop(stop) {}
    bool UserBreak() override
    {
        return m_stop.stop_requested();
    }
    void Show(const Message_ProgressScope &, bool) override {}

  private:
    std::stop_token m_stop;
};
double volume(const TopoDS_Shape &shape)
{
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    return properties.Mass();
}
} // namespace

std::vector<PlateHoleBase> plateHoleBases(const TopoDS_Shape &shape)
{
    if (shape.ShapeType() != TopAbs_SOLID)
        throw GeometryError("not_solid", "添加孔需要实体板件，独立曲面不能开孔。");
    const auto profile = plateProfile(shape);
    const auto normal = profile.plane.Axis().Direction();
    const auto backPlane = BRepAdaptor_Surface(profile.back).Plane();
    gp_Vec thickness(normal);
    if (gp_Vec(profile.plane.Location(), backPlane.Location()).Dot(thickness) < 0)
        thickness.Reverse();
    ShapeMap edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    std::vector<PlateHoleBase> result;
    const auto outer = BRepTools::OuterWire(profile.front);
    for (TopExp_Explorer it(outer, TopAbs_EDGE); it.More(); it.Next())
    {
        const auto edge = TopoDS::Edge(it.Current());
        const BRepAdaptor_Curve curve(edge);
        if (curve.GetType() != GeomAbs_Line)
            continue;
        auto first = curve.Value(curve.FirstParameter()), second = curve.Value(curve.LastParameter());
        const double length = first.Distance(second);
        if (length < 1e-4)
            continue;
        // 固定端点编号；屏幕翻转不改变命令中的端点含义。
        if (std::tuple(first.X(), first.Y(), first.Z()) > std::tuple(second.X(), second.Y(), second.Z()))
            std::swap(first, second);
        const gp_Vec along = gp_Vec(first, second).Normalized();
        gp_Vec inward = gp_Vec(normal).Crossed(along).Normalized();
        const gp_Pnt middle((first.XYZ() + second.XYZ()) * 0.5);
        const double epsilon = std::max(1e-4, std::min(0.01, length * 1e-4));
        if (BRepClass_FaceClassifier(profile.front, middle.Translated(inward * epsilon), 1e-7).State() != TopAbs_IN)
            inward.Reverse();
        if (BRepClass_FaceClassifier(profile.front, middle.Translated(inward * epsilon), 1e-7).State() != TopAbs_IN)
            continue;
        result.push_back({static_cast<std::uint32_t>(edges.FindIndex(edge)), vec(first.XYZ()), vec(second.XYZ()),
                          vec(inward.XYZ()), vec(thickness.XYZ()), profile.thickness});
    }
    std::sort(result.begin(), result.end(), [](const auto &a, const auto &b) { return a.edge < b.edge; });
    if (result.empty())
        throw GeometryError("no_bottom_edge", "该板件没有可用的直线底边。");
    return result;
}

std::vector<TopoDS_Shape> plateHoleTools(const PlateHoleBase &base, const PlateHole &request)
{
    const auto positive = [](double value) { return std::isfinite(value) && value > 1e-5 && value <= 1e6; };
    if (!positive(request.shape == PlateHoleShape::Rectangle ? request.widthMm : request.sizeMm) ||
        (request.shape == PlateHoleShape::Rectangle && !positive(request.heightMm)))
        throw GeometryError("invalid_hole_size", "孔的半径、尺寸、宽度和高度必须为有效正数（mm）。");
    if (!std::isfinite(request.offsetMm))
        throw GeometryError("invalid_offset", "偏移必须为有限毫米值。");
    if ((request.kind == PlateHoleKind::Weld && request.shape == PlateHoleShape::Rectangle) ||
        (request.kind == PlateHoleKind::Bottom && request.shape == PlateHoleShape::Triangle) ||
        (request.kind != PlateHoleKind::Weld && request.kind != PlateHoleKind::Bottom) ||
        (request.shape != PlateHoleShape::Circle && request.shape != PlateHoleShape::Triangle &&
         request.shape != PlateHoleShape::Rectangle))
        throw GeometryError("invalid_hole_shape", "过焊孔支持圆形/三角形；底边孔支持圆形/矩形。");
    if (request.kind == PlateHoleKind::Weld && !request.first && !request.second)
        throw GeometryError("empty_hole_positions", "请至少选择一个过焊孔位置。");
    if (request.reference != HoleReference::Start && request.reference != HoleReference::Middle &&
        request.reference != HoleReference::End)
        throw GeometryError("invalid_reference", "底边参考点无效。");
    if (request.bottomEdge != base.edge || !positive(base.thicknessMm))
        throw GeometryError("invalid_bottom_edge", "底边或板厚信息无效，请重新选择板件。");
    std::vector<TopoDS_Shape> tools;
    const auto first = point(base.firstMm), second = point(base.secondMm);
    const double length = first.Distance(second);
    const auto along = gp_Vec(first, second).Normalized(), inward = vector(base.inward),
               normal = vector(base.thicknessDirection);
    const double margin = std::max(1.0, base.thicknessMm * 0.1);
    const auto prism = [&](const std::vector<gp_Pnt> &points)
    {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto &p : points)
            polygon.Add(p.Translated(-normal * margin));
        polygon.Close();
        return BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(polygon.Wire()).Face(),
                                     normal * (base.thicknessMm + 2 * margin))
            .Shape();
    };
    const auto circle = [&](const gp_Pnt &center)
    {
        return BRepPrimAPI_MakeCylinder(gp_Ax2(center.Translated(-normal * margin), gp_Dir(normal)), request.sizeMm,
                                        base.thicknessMm + 2 * margin)
            .Shape();
    };
    if (request.kind == PlateHoleKind::Weld)
    {
        if (request.sizeMm >= length || (request.first && request.second && 2 * request.sizeMm >= length))
            throw GeometryError("overlapping_holes", "过焊孔尺寸超出底边或两端孔相交，请减小尺寸。");
        for (const bool atEnd : {false, true})
        {
            if (!(atEnd ? request.second : request.first))
                continue;
            const auto corner = atEnd ? second : first;
            const auto direction = atEnd ? -along : along;
            if (request.shape == PlateHoleShape::Circle)
                tools.push_back(circle(corner));
            else
                tools.push_back(prism({corner, corner.Translated(direction * request.sizeMm),
                           corner.Translated(inward * request.sizeMm)}));
        }
    }
    else
    {
        const double position = request.reference == HoleReference::Start ? request.offsetMm
                                : request.reference == HoleReference::End ? length - request.offsetMm
                                                                          : length / 2 + request.offsetMm;
        if (position < -1e-7 || position > length + 1e-7)
            throw GeometryError("hole_offset_outside", "偏移后的孔中心必须位于所选底边范围内。");
        const auto center = first.Translated(along * position);
        if (request.shape == PlateHoleShape::Circle)
            tools.push_back(circle(center));
        else
        {
            const auto left = center.Translated(-along * (request.widthMm / 2));
            const auto right = center.Translated(along * (request.widthMm / 2));
            tools.push_back(prism({left.Translated(-inward * margin), right.Translated(-inward * margin),
                       right.Translated(inward * request.heightMm), left.Translated(inward * request.heightMm)}));
        }
    }
    return tools;
}

TopoDS_Shape cutPlateHole(const TopoDS_Shape &source, const PlateHole &request, std::stop_token stop)
{
    check(stop);
    // 复制后再布尔运算，预览与失败均不会修改源实体、三角化或撤销快照。
    auto shape = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto bases = plateHoleBases(shape);
    const auto found =
        std::find_if(bases.begin(), bases.end(), [&](const auto &b) { return b.edge == request.bottomEdge; });
    if (found == bases.end())
        throw GeometryError("invalid_bottom_edge", "底边编号无效；请选择主面外轮廓上的直线边。");
    // 显示预览与正式开孔共用同一套刀具构造，避免孔径、偏移或方向不一致。
    const auto tools = plateHoleTools(*found, request);
    for (const auto &tool : tools)
    {
        check(stop);
        const double before = volume(shape);
        BRepAlgoAPI_Cut operation;
        NCollection_List<TopoDS_Shape> arguments, booleanTools;
        arguments.Append(shape);
        booleanTools.Append(tool);
        operation.SetArguments(arguments);
        operation.SetTools(booleanTools);
        operation.SetNonDestructive(true);
        Handle(CutProgress) progress = new CutProgress(stop);
        operation.Build(progress->Start());
        check(stop);
        if (!operation.IsDone() || operation.Shape().IsNull())
            throw GeometryError("hole_cut_failed", "开孔计算失败，请检查底边和尺寸；模型未修改。");
        operation.SimplifyResult(true, true);
        TopoDS_Shape solid;
        int count = 0;
        for (TopExp_Explorer it(operation.Shape(), TopAbs_SOLID); it.More(); it.Next())
        {
            solid = it.Current();
            ++count;
        }
        if (count != 1 || !BRepCheck_Analyzer(solid).IsValid() || volume(solid) <= 1e-6)
            throw GeometryError("hole_splits_plate", "孔尺寸过大或会切断板件，请减小尺寸/调整偏移；模型未修改。");
        if (before - volume(solid) <= std::max(1e-6, before * 1e-10))
            throw GeometryError("hole_no_intersection", "该位置没有可切除的材料，可能已开孔或偏移超出板件。");
        shape = solid;
    }
    check(stop);
    return shape;
}
} // namespace reverse::detail
