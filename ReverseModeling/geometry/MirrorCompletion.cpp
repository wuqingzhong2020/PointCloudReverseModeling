/*
 * 文件描述：使用底板中面反射完整 BRep（保留内孔和圆角），校验体积及实体有效性。
 * 布尔运算采用独立拓扑与可取消进度，不改写源模型；相连的同板镜像合并，分离部分独立保留。
 */
#include <algorithm>
#include <cmath>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <GProp_GProps.hxx>
#include <Message_ProgressIndicator.hxx>
#include <Message_ProgressScope.hxx>
#include <NCollection_List.hxx>
#include <TopExp_Explorer.hxx>
#include <gp_Ax2.hxx>
#include <gp_Trsf.hxx>

#include "geometry/Geometry.h"
#include "geometry/MirrorCompletion.h"
namespace reverse::detail
{
namespace
{
class MirrorProgress final : public Message_ProgressIndicator
{
  public:
    explicit MirrorProgress(std::stop_token stop) : m_stop(stop)
    {
    }
    bool UserBreak() override
    {
        return m_stop.stop_requested();
    }
    void Show(const Message_ProgressScope &, bool) override
    {
    }

  private:
    std::stop_token m_stop;
};
double volume(const TopoDS_Shape &shape)
{
    GProp_GProps props;
    BRepGProp::VolumeProperties(shape, props);
    return props.Mass();
}
template <class Operation> TopoDS_Shape booleanShape(const TopoDS_Shape &a, const TopoDS_Shape &b, std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "镜像补全已取消。");
    NCollection_List<TopoDS_Shape> arguments, tools;
    arguments.Append(a);
    tools.Append(b);
    Operation operation;
    operation.SetArguments(arguments);
    operation.SetTools(tools);
    operation.SetNonDestructive(true);
    Handle(MirrorProgress) progress = new MirrorProgress(stop);
    operation.Build(progress->Start());
    if (stop.stop_requested())
        throw GeometryError("cancelled", "镜像补全已取消。");
    if (!operation.IsDone() || operation.HasErrors())
        throw GeometryError("mirror_failed", "镜像几何交叠检查或合并失败，整批未应用。");
    // 合并布尔运算留下的共面分割，维持等厚板的两个完整主面。
    operation.SimplifyResult(true, true);
    return operation.Shape();
}
} // namespace
bool mirrorPartsConnected(const TopoDS_Shape &a, const TopoDS_Shape &b, double toleranceMm, std::stop_token stop)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "工件选择已取消。");
    BRepExtrema_DistShapeShape distance;
    distance.LoadS1(a);
    distance.LoadS2(b);
    Handle(MirrorProgress) progress = new MirrorProgress(stop);
    distance.Perform(progress->Start());
    if (stop.stop_requested()) throw GeometryError("cancelled", "工件选择已取消。");
    if (!distance.IsDone()) throw GeometryError("connection_failed", "无法判断部件连接关系，未执行镜像补全。");
    return distance.Value() <= toleranceMm;
}
TopoDS_Shape mirrorPlate(const TopoDS_Shape &source, const PlateThicknessInfo &plane)
{
    gp_Trsf transform;
    transform.SetMirror(gp_Ax2(gp_Pnt(plane.centerMm.x, plane.centerMm.y, plane.centerMm.z),
                               gp_Dir(plane.normal.x, plane.normal.y, plane.normal.z)));
    const auto result = BRepBuilderAPI_Transform(source, transform, true).Shape();
    const double before = volume(source), after = volume(result);
    if (result.IsNull() || result.ShapeType() != TopAbs_SOLID || !BRepCheck_Analyzer(result).IsValid() || after <= 0 ||
        std::abs(before - after) > std::max(1e-4, before * 1e-7))
        throw GeometryError("mirror_failed", "镜像未生成有效等体积实体，原模型未修改。");
    return result;
}
double mirrorOverlapVolume(const TopoDS_Shape &a, const TopoDS_Shape &b, std::stop_token stop)
{
    return std::max(0.0, volume(booleanShape<BRepAlgoAPI_Common>(a, b, stop)));
}
TopoDS_Shape uniteMirrorPlate(const TopoDS_Shape &a, const TopoDS_Shape &b, std::stop_token stop)
{
    const auto fused = booleanShape<BRepAlgoAPI_Fuse>(a, b, stop);
    TopoDS_Shape solid;
    int count = 0;
    for (TopExp_Explorer it(fused, TopAbs_SOLID); it.More(); it.Next())
    {
        solid = it.Current();
        ++count;
    }
    if (count == 2)
        return {};
    if (count != 1 || !BRepCheck_Analyzer(solid).IsValid())
        throw GeometryError("mirror_failed", "镜像合并结果不是有效实体，整批未应用。");
    return solid;
}
} // namespace reverse::detail
