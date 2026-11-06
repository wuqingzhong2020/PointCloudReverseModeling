/*
 * 文件描述：仅适配参考算法的输入、错误、原子提交与来源信息，不重新定义几何规则。
 * 原算法位于同目录 PlateGeometryUtils_*；运算使用快照拷贝，保持撤销前实体不变。
 */
#include <BRepBuilderAPI_Copy.hxx>
#include <Standard_Failure.hxx>
#include "geometry/assembly/AssemblyPlateEditing.h"
#include "geometry/assembly/PlateGeometryUtils.h"
namespace reverse::detail
{
namespace
{
using Algorithms = assembly::PlateGeometryUtils;
void check(std::stop_token stop)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "操作已取消，模型保持不变。");
}
void failure(const QString &error)
{
    throw GeometryError("plate_edit_failed", error.toUtf8().toStdString());
}
void validate(const std::map<std::string, TopoDS_Shape> &shapes, const PlateEdit &r)
{
    if (r.parts.empty() || (!(r.all && r.operation == "repair") && r.parts.size() != 1))
        throw GeometryError("single_source_required", "请只选择一个源板件。");
    for (const auto &id : r.parts)
        if (!shapes.contains(id))
            throw GeometryError("unknown_part", "部件不存在：" + id);
    if (r.operation == "extend" || r.operation == "merge" || r.operation == "split" || r.operation == "trim")
        if (!shapes.contains(r.target) || r.parts.front() == r.target)
            throw GeometryError("invalid_target", "请选择另一个模型作为目标板。");
}
}
void prepareAssemblyPlateEdit(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &r, std::stop_token stop)
{
    check(stop);
    if (r.all)
    {
        if (r.operation != "repair")
            throw GeometryError("invalid_plate_operation", "只有修复全部孔洞处理整个模型。");
        r.parts.clear();
        for (const auto &[id, shape] : shapes)
            r.parts.push_back(id);
    }
    validate(shapes, r);
    if (r.operation == "crop" && r.rectangle)
    {
        TopoDS_Shape result;
        PlateCut cut;
        QString error;
        const auto source = BRepBuilderAPI_Copy(shapes.at(r.parts.front()), true, false).Shape();
        if (!Algorithms::CropPlateByScreenBox(source, r, cut, result, &error))
            failure(error);
        r.cuts = {cut};
        r.rectangle.reset();
    }
    check(stop);
}
PlateShapeEdit editAssemblyPlateShapes(const std::map<std::string, TopoDS_Shape> &shapes, PlateEdit &r, std::stop_token stop)
{
    validate(shapes, r);
    PlateShapeEdit edited;
    for (const auto &id : r.parts)
    {
        check(stop);
        const auto source = BRepBuilderAPI_Copy(shapes.at(id), true, false).Shape();
        const auto target = r.target.empty() ? TopoDS_Shape{} : BRepBuilderAPI_Copy(shapes.at(r.target), true, false).Shape();
        TopoDS_Shape result;
        QString error;
        bool ok = false;
        if (r.operation == "extend")
            ok = Algorithms::ExtendPlateToTarget(source, target, result, &error);
        else if (r.operation == "merge")
            ok = Algorithms::MergePlateToTarget(source, target, result, &error);
        else if (r.operation == "split" || r.operation == "trim")
        {
            std::vector<TopoDS_Shape> parts;
            ok = Algorithms::SplitPlateToTarget(source, target, parts, &error);
            if (ok)
            {
                if (r.operation == "trim" && parts.size() > 1)
                    parts.resize(1); // 原分割算法已按体积降序排列；截取保留主体。
                for (const auto &part : parts)
                    for (const auto &solid : plateSolids(part))
                        edited.replacements[id].push_back(solid);
                edited.reports.push_back(id + (r.operation == "trim" ? "：截取完成，保留主体。" : "：分割完成。"));
                continue;
            }
        }
        else if (r.operation == "crop")
        {
            PlateCut cut;
            ok = Algorithms::CropPlateByScreenBox(source, r, cut, result, &error);
        }
        else if (r.operation == "repair")
        {
            int holes = 0, vertices = 0;
            ok = Algorithms::RepairPlateHolesAndBoundary(source, result, &holes, &vertices, &error);
            if (ok)
            {
                edited.reports.push_back(id + "：清除内部孔洞 " + std::to_string(holes) +
                    " 个，简化边缘顶点 " + std::to_string(vertices) + " 个。");
                if (r.all && holes == 0 && vertices == 0)
                    continue;
            }
            else if (r.all)
            {
                edited.reports.push_back(id + "：跳过，" + error.toUtf8().toStdString());
                continue;
            }
        }
        else
            throw GeometryError("invalid_plate_operation", "未知的直接板件操作。");
        if (!ok)
            failure(error);
        if (result.IsSame(source))
        {
            edited.reports.push_back(id + "：已贴合，无需延长。");
            continue;
        }
        const auto solids = plateSolids(result);
        if (solids.empty())
            throw GeometryError("invalid_plate_result", "操作未生成有效板件。");
        if (r.operation == "merge")
        {
            edited.replacements[id] = {};
            edited.replacements[r.target] = solids;
        }
        else
            edited.replacements[id] = solids;
    }
    check(stop);
    return edited;
}
PlateThicknessInfo assemblyPlateThickness(const TopoDS_Shape &shape)
{
    Algorithms::PlatePrincipalFrame frame;
    if (!Algorithms::ExtractPlatePrincipalFrame(shape, frame))
        throw GeometryError("invalid_plate_result", "结果无法提取板件主平面。");
    double u0, u1, v0, v1, t0, t1;
    Algorithms::ComputeShapeProjectedBounds(shape, frame.origin, frame.dirU, frame.dirV, frame.dirT,
                                           u0, u1, v0, v1, t0, t1);
    const auto center = frame.origin.Translated(gp_Vec(frame.dirU) * ((u0+u1)/2) +
        gp_Vec(frame.dirV) * ((v0+v1)/2) + gp_Vec(frame.dirT) * ((t0+t1)/2));
    return {t1-t0, {center.X(),center.Y(),center.Z()}, {frame.dirT.X(),frame.dirT.Y(),frame.dirT.Z()}};
}
}
