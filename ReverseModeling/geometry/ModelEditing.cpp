/* 文件描述：在模型副本上处理板厚、轮廓、边、孔、变换和删除，保留历史快照。 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <utility>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRep_Builder.hxx>
#include <Message_ProgressIndicator.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <gp_Trsf.hxx>

#include "geometry/ModelState.h"
#include "geometry/OtherModelRecognition.h"
#include "geometry/StructuralSteel.h"
#include "geometry/PlateContour.h"
#include "geometry/PlateHoles.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateThickness.h"

namespace reverse
{
using detail::model::appendMesh;
using detail::model::checkedVolume;
using detail::model::checkpoint;
using detail::model::publishModel;
using detail::model::recognizedShape;

namespace
{
class EdgeProgress final : public Message_ProgressIndicator
{
  public:
    explicit EdgeProgress(std::stop_token stop) : m_stop(stop) {}
    bool UserBreak() override
    {
        return m_stop.stop_requested();
    }
    void Show(const Message_ProgressScope &, bool) override {}

  private:
    std::stop_token m_stop;
};

TopoDS_Shape treatSolidEdges(const TopoDS_Shape &source, const EdgeTreatmentTarget &target, EdgeTreatmentKind kind,
                             double sizeMm, std::stop_token stop)
{
    if (source.ShapeType() != TopAbs_SOLID)
        throw GeometryError("not_solid", "圆角 / 倒角需要实体部件，观测面或开放曲面不支持此操作。");
    if (target.edges.empty())
        throw GeometryError("empty_edges", "参数 --edges 至少需要一条边。");
    // 在当前实体副本上处理，任何失败都不会触碰预览源和撤销快照。
    auto shape = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto selected = detail::plateOutlineCornerEdges(shape, target.edges);
    Handle(EdgeProgress) indicator = new EdgeProgress(stop);
    const auto build = [&](auto &operation)
    {
        for (const auto &edge : selected)
            operation.Add(sizeMm, edge);
        operation.Build(indicator->Start());
        checkpoint(stop, {}, 0, {});
        if (!operation.IsDone() || operation.Shape().IsNull())
        {
            std::ostringstream message;
            message << "板件轮廓" << (kind == EdgeTreatmentKind::Fillet ? "圆角 R=" : "倒角 D=") << sizeMm
                    << " mm 构建失败；所选板角的轮廓空间不足或相邻圆角相交，整批未应用。";
            throw GeometryError("edge_treatment_failed", message.str());
        }
        shape = operation.Shape();
        (void)checkedVolume(shape);
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() != 1 || TopExp_Explorer(shape, TopAbs_FACE, TopAbs_SOLID).More())
            throw GeometryError("invalid_solid", "未生成单个完整实体，整批未应用。");
        shape = solids(1);
    };
    if (kind == EdgeTreatmentKind::Fillet)
    {
        BRepFilletAPI_MakeFillet operation(shape);
        build(operation);
    }
    else
    {
        BRepFilletAPI_MakeChamfer operation(shape);
        build(operation);
    }
    return shape;
}
} // namespace

std::shared_ptr<const Model> treatModelEdges(const Model &model, const EdgeTreatment &request, std::stop_token stop,
                                             const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查圆角 / 倒角参数");
    if (!std::isfinite(request.sizeMm) || request.sizeMm <= 0)
        throw GeometryError("invalid_size", "参数 --size 必须为大于 0 的有限毫米值。");
    if (request.kind != EdgeTreatmentKind::Fillet && request.kind != EdgeTreatmentKind::Chamfer)
        throw GeometryError("invalid_kind", "仅支持圆角和等距倒角。");
    if (request.targets.empty())
        throw GeometryError("empty_selection", "请先选择实体边线。");
    const auto &source = *model.implementation_;
    auto result = std::make_shared<Model::Impl>(source);
    std::set<std::string> processed;
    std::string currentPart;
    try
    {
        for (const auto &target : request.targets)
        {
            currentPart = target.partId;
            checkpoint(stop, progress, 0.1 + 0.6 * processed.size() / request.targets.size(),
                       "计算圆角 / 倒角：" + currentPart);
            if (!processed.insert(currentPart).second)
                throw GeometryError("duplicate_part", "同一部件的边应合并在一个分组中。");
            const auto part = std::find_if(result->plates.begin(), result->plates.end(),
                                           [&](const auto &p) { return p.id == currentPart; });
            if (part == result->plates.end())
                throw GeometryError("unknown_part", "部件不存在。");
            part->shape = treatSolidEdges(part->shape, target, request.kind, request.sizeMm, stop);
            auto &treated = result->summary.edgeTreatedParts;
            if (std::find(treated.begin(), treated.end(), currentPart) == treated.end())
                treated.push_back(currentPart);
        }
        result->mesh = {};
        result->mesh.partIds = result->summary.partIds;
        result->summary.volumeMm3 = 0;
        BRep_Builder builder;
        builder.MakeCompound(result->compound);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            auto &p = result->plates[i];
            currentPart = p.id;
            checkpoint(stop, progress, 0.75 + 0.24 * i / result->plates.size(), "更新圆角 / 倒角显示网格");
            // 三角化也会修改 TShape；未编辑部件同样复制，避免触碰历史快照。
            p.shape = BRepBuilderAPI_Copy(p.shape, true, false).Shape();
            builder.Add(result->compound, p.shape);
            if (p.shape.ShapeType() == TopAbs_SOLID)
                result->summary.volumeMm3 += checkedVolume(p.shape);
            appendMesh(p.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        checkpoint(stop, progress, 1, "圆角 / 倒角计算完成");
        return publishModel(std::move(result), stop);
    }
    catch (const GeometryError &error)
    {
        if (error.code() == "cancelled")
            throw;
        throw GeometryError(error.code(), "部件 " + currentPart + "：" + error.what());
    }
    catch (const Standard_Failure &error)
    {
        if (stop.stop_requested())
            throw GeometryError("cancelled", "操作已取消。");
        throw GeometryError("edge_treatment_failed",
                            "部件 " + currentPart + " 板件轮廓圆角 / 倒角计算失败，请检查轮廓和数值，整批未应用：" +
                                error.what());
    }
}

std::map<std::string, PlateThicknessInfo>
inspectModelThickness(const Model &model, const std::vector<std::string> &partIds, std::stop_token stop)
{
    if (partIds.empty())
    {
        throw GeometryError("empty_selection", "参数 --parts 至少需要一个板件 ID。");
    }
    std::map<std::string, PlateThicknessInfo> result;
    for (const auto &id : partIds)
    {
        if (model.summary().obstacleParts.contains(id))
            throw GeometryError("not_plate", "部件 " + id + " 是避障包络，不作为板件调整或镜像补全。");
        checkpoint(stop, {}, 0, {});
        const auto &plates = model.implementation_->plates;
        const auto part = std::find_if(plates.begin(), plates.end(), [&](const auto &p) { return p.id == id; });
        if (part == plates.end())
        {
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
        }
        try
        {
            const auto &summary = model.summary();
            const auto observed = std::find_if(summary.plates.begin(), summary.plates.end(),
                                               [&](const auto &plate) { return plate.id == id; });
            std::optional<PlateThicknessInfo> local;
            if (observed != summary.plates.end() && !observed->uprightFootprint.empty())
            {
                const auto contains = [&](const auto &ids)
                { return std::find(ids.begin(), ids.end(), id) != ids.end(); };
                if (contains(summary.holeEditedParts) || contains(summary.edgeTreatedParts) ||
                    contains(summary.contourExpandedParts) || contains(summary.mirrorCompletedParts) ||
                    summary.editSources.contains(id))
                    throw GeometryError("unsupported_enclosure", "围板已经过轮廓编辑，无法按原始圆角轮廓统一改厚。");
                local = detail::enclosureThickness(*observed);
                if (!local)
                    throw GeometryError("unsupported_enclosure",
                                        "连接组未形成规则四圆角围板，不能把组的高度作为板厚。");
            }
            else if (observed != summary.plates.end() && !observed->thicknessMm &&
                     part->shape.ShapeType() == TopAbs_FACE)
            {
                local = PlateThicknessInfo{1.0,
                                           {(observed->minimumMm.x + observed->maximumMm.x) / 2,
                                            observed->maximumMm.y - 0.5,
                                            (observed->minimumMm.z + observed->maximumMm.z) / 2},
                                           {0, 1, 0}};
            }
            if (local)
            {
                const auto frame = modelPartFrame(summary, id);
                local->centerMm = transformPoint(frame, local->centerMm);
                local->normal = transformVector(frame, local->normal);
                result.emplace(id, *local);
            }
            else
                result.emplace(id, detail::inspectPlateThickness(part->shape));
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + id + "：" + error.what());
        }
    }
    return result;
}

std::shared_ptr<const Model> changeModelThickness(const Model &model, const PlateThicknessChange &request,
                                                  std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件和厚度");
    if (!std::isfinite(request.thicknessMm) || request.thicknessMm < 0.001 || request.thicknessMm > 1e6)
    {
        throw GeometryError("invalid_thickness", "参数 --thickness 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.volumeMm3 = 0;
    result->summary.solidCount = result->summary.surfaceCount = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.1 + 0.85 * i / result->plates.size(), "更新板厚：" + part.id);
        try
        {
            const auto info = selected.find(part.id);
            if (info != selected.end())
            {
                auto observed = std::find_if(result->summary.plates.begin(), result->summary.plates.end(),
                                             [&](const auto &plate) { return plate.id == part.id; });
                const auto frame = modelPartFrame(result->summary, part.id);
                if (auto steel = detail::inspectSteelSection(part.shape))
                {
                    steel->thickness = request.thicknessMm;
                    part.shape = detail::buildSteelSection(*steel);
                    info->second = detail::steelThickness(*steel);
                }
                else if (observed != result->summary.plates.end() &&
                         (!observed->uprightFootprint.empty() ||
                          (!observed->thicknessMm && part.shape.ShapeType() == TopAbs_FACE)))
                {
                    if (!observed->uprightFootprint.empty())
                        detail::resizeEnclosure(*observed, request.thicknessMm);
                    else
                    {
                        observed->minimumMm.y = observed->maximumMm.y - request.thicknessMm;
                        observed->thicknessMm = request.thicknessMm;
                        const auto up = frame.yAxis;
                        const double shift = (1 - request.thicknessMm) / 2;
                        info->second.centerMm = {info->second.centerMm.x + up.x * shift,
                                                 info->second.centerMm.y + up.y * shift,
                                                 info->second.centerMm.z + up.z * shift};
                    }
                    gp_Trsf placement;
                    placement.SetValues(frame.xAxis.x, frame.yAxis.x, frame.zAxis.x, frame.originMm.x, frame.xAxis.y,
                                        frame.yAxis.y, frame.zAxis.y, frame.originMm.y, frame.xAxis.z, frame.yAxis.z,
                                        frame.zAxis.z, frame.originMm.z);
                    part.shape = BRepBuilderAPI_Transform(recognizedShape(*observed, stop), placement, true).Shape();
                }
                else
                {
                    part.shape = detail::resizePlateThickness(part.shape, request.thicknessMm);
                    if (observed != result->summary.plates.end() && observed->id == "other-base")
                    {
                        // 底板向观测面下方增厚，焊接接触面与孔保持不动。
                        const auto up = frame.yAxis;
                        const double shift = (info->second.thicknessMm - request.thicknessMm) / 2;
                        gp_Trsf translation;
                        translation.SetTranslation(gp_Vec(up.x * shift, up.y * shift, up.z * shift));
                        part.shape = BRepBuilderAPI_Transform(part.shape, translation, true).Shape();
                        info->second.centerMm = {info->second.centerMm.x + up.x * shift,
                                                 info->second.centerMm.y + up.y * shift,
                                                 info->second.centerMm.z + up.z * shift};
                        observed->minimumMm.y = observed->maximumMm.y - request.thicknessMm;
                        observed->thicknessMm = request.thicknessMm;
                    }
                }
                info->second.thicknessMm = request.thicknessMm;
                result->summary.thicknessOverrides[part.id] = info->second;
            }
            else
            {
                // 网格生成也修改 TShape；复制未选部件，保证历史快照独立。
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            }
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                ++result->summary.solidCount;
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            else
                ++result->summary.surfaceCount;
            appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("thickness_failed", "部件 " + part.id + " 修改板厚失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "板厚修改完成");
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> expandModelContours(const Model &model, const PlateContourExpansion &request,
                                                 std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件和外扩距离");
    if (!std::isfinite(request.distanceMm) || request.distanceMm < 0.001 || request.distanceMm > 1e6)
    {
        throw GeometryError("invalid_distance", "参数 --distance 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.1 + 0.85 * i / result->plates.size(), "外扩轮廓：" + part.id);
        try
        {
            const auto info = selected.find(part.id);
            if (info != selected.end())
            {
                part.shape = detail::expandPlateContour(part.shape, request.distanceMm);
                if (std::find(result->summary.contourExpandedParts.begin(), result->summary.contourExpandedParts.end(),
                              part.id) == result->summary.contourExpandedParts.end())
                {
                    result->summary.contourExpandedParts.push_back(part.id);
                }
                // 已经手工修改的板厚标注跟随新轮廓质心；导入实体的几何板厚在发布时统一更新。
                if (result->summary.thicknessOverrides.contains(part.id))
                {
                    result->summary.thicknessOverrides[part.id] = detail::inspectPlateThickness(part.shape);
                }
            }
            else
            {
                // 网格生成也修改 TShape；复制未选部件，保证历史快照独立。
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            }
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + part.id + "：" + error.what());
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("contour_failed", "部件 " + part.id + " 外扩轮廓失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "板件轮廓外扩完成");
    return publishModel(std::move(result), stop);
}

std::vector<PlateHoleBase> inspectModelHoleBases(const Model &model, const std::string &partId)
{
    for (const auto &plate : model.implementation_->plates)
        if (plate.id == partId)
            return detail::plateHoleBases(plate.shape);
    throw GeometryError("unknown_part", "部件不存在：" + partId);
}

std::shared_ptr<const Model> addModelHole(const Model &model, const PlateHole &request, std::stop_token stop,
                                          const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查添加孔参数");
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    auto part = std::find_if(result->plates.begin(), result->plates.end(),
                             [&](const auto &p) { return p.id == request.partId; });
    if (part == result->plates.end())
        throw GeometryError("unknown_part", "部件不存在：" + request.partId);
    std::string currentPart = request.partId;
    try
    {
        part->shape = detail::cutPlateHole(part->shape, request, stop);
        auto &edited = result->summary.holeEditedParts;
        if (std::find(edited.begin(), edited.end(), request.partId) == edited.end())
            edited.push_back(request.partId);
        result->mesh = {};
        result->mesh.partIds = result->summary.partIds;
        result->summary.volumeMm3 = 0;
        BRep_Builder builder;
        builder.MakeCompound(result->compound);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            auto &p = result->plates[i];
            currentPart = p.id;
            checkpoint(stop, progress, 0.75 + 0.24 * i / result->plates.size(), "更新开孔显示网格");
            // 三角化也会修改 TShape；未编辑部件同样复制，避免触碰历史快照。
            p.shape = BRepBuilderAPI_Copy(p.shape, true, false).Shape();
            builder.Add(result->compound, p.shape);
            if (p.shape.ShapeType() == TopAbs_SOLID)
                result->summary.volumeMm3 += checkedVolume(p.shape);
            appendMesh(p.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        checkpoint(stop, progress, 1, "开孔完成");
        return publishModel(std::move(result), stop);
    }
    catch (const Standard_Failure &error)
    {
        if (stop.stop_requested())
            throw GeometryError("cancelled", "操作已取消。");
        throw GeometryError("hole_cut_failed", "部件 " + request.partId + " 开孔失败，模型未修改：" + error.what());
    }
}

std::shared_ptr<const Model> transformModelParts(const Model &model, const std::vector<std::string> &partIds,
                                                 const ModelTransform &request, std::stop_token stop,
                                                 const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查变换参数和部件");
    const auto delta = modelTransformFrame(request);
    const auto &source = *model.implementation_;
    const std::set<std::string> selected(partIds.begin(), partIds.end());
    if (selected.empty())
        throw GeometryError("empty_selection", "参数 --parts 至少需要一个部件 ID。");
    for (const auto &id : selected)
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
    if (isIdentityTransform(request))
        return std::make_shared<const Model>(model.implementation_);
    auto result = std::make_shared<Model::Impl>(source);
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    gp_Trsf transform;
    transform.SetValues(delta.xAxis.x, delta.yAxis.x, delta.zAxis.x, delta.originMm.x, delta.xAxis.y, delta.yAxis.y,
                        delta.zAxis.y, delta.originMm.y, delta.xAxis.z, delta.yAxis.z, delta.zAxis.z, delta.originMm.z);
    std::vector<bool> moved(source.plates.size());
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        checkpoint(stop, progress, 0.1 + 0.6 * i / result->plates.size(), "变换实体部件");
        auto &plate = result->plates[i];
        moved[i] = selected.contains(plate.id);
        if (moved[i])
        {
            // Copy=true 避免修改撤销历史共享的拓扑；刚体变换不改变体积和板厚。
            BRepBuilderAPI_Transform operation(plate.shape, transform, true);
            if (!operation.IsDone())
                throw GeometryError("transform_failed", "部件几何变换失败。");
            plate.shape = operation.Shape();
            auto &accumulated = result->summary.partTransforms[plate.id];
            accumulated = composeTransforms(delta, accumulated);
            if (auto info = result->summary.thicknessOverrides.find(plate.id);
                info != result->summary.thicknessOverrides.end())
            {
                info->second.centerMm = transformPoint(delta, info->second.centerMm);
                info->second.normal = transformVector(delta, info->second.normal);
            }
        }
        builder.Add(result->compound, plate.shape);
    }
    auto &mesh = result->mesh;
    std::vector<bool> vertices(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
    {
        if (i % 4096 == 0)
            checkpoint(stop, progress, 0.7 + 0.2 * i / mesh.triangles.size(), "同步显示和拾取几何");
        if (moved.at(mesh.trianglePartIndices.at(i)))
            for (const auto v : mesh.triangles[i])
                vertices.at(v) = true;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i)
        if (vertices[i])
            mesh.vertices[i] = transformPoint(delta, mesh.vertices[i]);
    for (auto &point : mesh.keyPoints)
        if (moved.at(point.partIndex))
            point.positionMm = transformPoint(delta, point.positionMm);
    for (auto &edge : mesh.edges)
        if (moved.at(edge.partIndex))
            for (auto &point : edge.pointsMm)
                point = transformPoint(delta, point);
    checkpoint(stop, progress, 1, "部件变换完成");
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> deleteModelParts(const Model &model, const std::vector<std::string> &partIds,
                                              std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查待删除部件");
    const auto &source = *model.implementation_;
    const std::set<std::string> removed(partIds.begin(), partIds.end());
    if (removed.empty())
        throw GeometryError("empty_selection", "参数 --parts 至少需要一个部件 ID。");
    for (const auto &id : removed)
    {
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
    }
    if (removed.size() == source.plates.size())
        return {};
    auto result = std::make_shared<Model::Impl>();
    auto &summary = result->summary;
    summary = source.summary;
    std::erase_if(summary.mirroredParts, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.mirrorCompletedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.generatedPlates, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.reconstructableParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.locallyReconstructedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.contourExpandedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.thicknessOverrides, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.holeEditedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.edgeTreatedParts, [&](const auto &id) { return removed.contains(id); });
    summary.partIds.clear();
    summary.solidCount = summary.surfaceCount = 0;
    summary.volumeMm3 = 0;
    std::erase_if(summary.obstacleParts, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.plates, [&](const auto &p) { return removed.contains(p.id); });
    std::erase_if(summary.partTransforms, [&](const auto &p) { return removed.contains(p.first); });
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    const auto absent = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> partMap(source.plates.size(), absent);
    for (std::size_t i = 0; i < source.plates.size(); ++i)
    {
        checkpoint(stop, progress, 0.1 + 0.3 * i / source.plates.size(), "移除模型部件");
        const auto &plate = source.plates[i];
        if (removed.contains(plate.id))
            continue;
        partMap[i] = static_cast<std::uint32_t>(result->plates.size());
        result->plates.push_back(plate);
        summary.partIds.push_back(plate.id);
        builder.Add(result->compound, plate.shape);
        if (plate.shape.ShapeType() == TopAbs_SOLID)
        {
            ++summary.solidCount;
            summary.volumeMm3 += checkedVolume(plate.shape);
        }
        else
            ++summary.surfaceCount;
    }
    // 同步裁剪中立显示网格，既不重新三角化共享 BRep，也不重新编号剩余部件 ID。
    auto &mesh = result->mesh;
    const auto &old = source.mesh;
    mesh.partIds = summary.partIds;
    std::vector<std::uint32_t> vertexMap(old.vertices.size(), absent);
    for (std::size_t i = 0; i < old.triangles.size(); ++i)
    {
        if (i % 4096 == 0)
            checkpoint(stop, progress, 0.4 + 0.5 * i / old.triangles.size(), "更新剩余部件网格");
        const auto part = partMap.at(old.trianglePartIndices.at(i));
        if (part == absent)
            continue;
        auto triangle = old.triangles[i];
        for (auto &v : triangle)
        {
            auto &mapped = vertexMap.at(v);
            if (mapped == absent)
            {
                mapped = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(old.vertices[v]);
            }
            v = mapped;
        }
        mesh.triangles.push_back(triangle);
        mesh.trianglePartIndices.push_back(part);
        mesh.triangleFaceIndices.push_back(old.triangleFaceIndices.at(i));
    }
    for (auto p : old.keyPoints)
    {
        p.partIndex = partMap.at(p.partIndex);
        if (p.partIndex != absent)
            mesh.keyPoints.push_back(p);
    }
    for (auto edge : old.edges)
    {
        edge.partIndex = partMap.at(edge.partIndex);
        if (edge.partIndex != absent)
            mesh.edges.push_back(std::move(edge));
    }
    for (auto face : old.planarFaces)
    {
        face[0] = partMap.at(face[0]);
        if (face[0] != absent)
            mesh.planarFaces.push_back(face);
    }
    checkpoint(stop, progress, 1, "部件删除完成");
    return publishModel(std::move(result), stop);
}

} // namespace reverse
