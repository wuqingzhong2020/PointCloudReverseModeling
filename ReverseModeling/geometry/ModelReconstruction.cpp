/* 文件描述：关联原始点云并进行局部重建、障碍物包络和选点建模，维护部件来源。 */

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <utility>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>

#include "geometry/LocalReconstruction.h"
#include "geometry/ModelState.h"
#include "geometry/PlateThickness.h"
#include "geometry/ObstacleReconstruction.h"
#include "geometry/PointModeling.h"

namespace reverse
{
using detail::model::appendMesh;
using detail::model::bounds;
using detail::model::checkedVolume;
using detail::model::checkpoint;
using detail::model::Plate;
using detail::model::publishModel;

std::shared_ptr<const Model> reconstructModelObstacles(const Model &model, const CloudData &cloud,
                                                       const PlateRecognition &recognition, std::stop_token stop,
                                                       const ProgressCallback &progress)
{
    if (!model.summary().obstacleParts.empty())
        throw GeometryError("obstacles_already_built", "请从当前点云重新构建工件及障碍物，避免重复包络。");
    std::vector<bool> solidParts;
    for (const auto &part : model.implementation_->plates)
        solidParts.push_back(part.shape.ShapeType() == TopAbs_SOLID ||
                             TopExp_Explorer(part.shape, TopAbs_SOLID).More());
    const auto obstacles = detail::reconstructObstacleShapes(
        cloud, model.mesh(), solidParts, recognition.scanSpacingMm, recognition.surfaceNoiseMm, stop, progress,
        &recognition, model.summary().recognitionOptions.obstacleExclusionDistanceMm);
    if (obstacles.empty())
        return std::make_shared<const Model>(model.implementation_);
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    BRep_Builder builder;
    // 新建 compound，不能修改历史快照共享的 TShape。
    builder.MakeCompound(result->compound);
    for (const auto &part : result->plates)
        builder.Add(result->compound, part.shape);
    std::size_t serial = 1;
    for (const auto &obstacle : obstacles)
    {
        checkpoint(stop, {}, 0, {});
        std::string id;
        do
        {
            id = "obstacle-" + std::to_string(serial++);
        } while (std::find(result->summary.partIds.begin(), result->summary.partIds.end(), id) !=
                 result->summary.partIds.end());
        const auto box = bounds(obstacle.shape);
        appendMesh(obstacle.shape, static_cast<std::uint32_t>(result->plates.size()), result->mesh);
        result->plates.push_back({id, {box[0], box[1], box[2]}, {box[3], box[4], box[5]}, obstacle.shape});
        result->summary.partIds.push_back(id);
        result->mesh.partIds.push_back(id);
        result->summary.obstacleParts.emplace(id, obstacle.info);
        ++result->summary.solidCount;
        result->summary.volumeMm3 += checkedVolume(obstacle.shape);
        builder.Add(result->compound, obstacle.shape);
    }
    result->summary.warnings.push_back(
        "已保留 " + std::to_string(obstacles.size()) +
        " 个独立障碍物避障包络；按观测点和采样余量包络，不表示未扫描背面的精确外形，不参与工件镜像补全。");
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> associateModelCloud(const Model &model, const CloudData &cloud, std::stop_token stop)
{
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    std::vector<detail::LocalPlateSource> sources;
    for (const auto &plate : model.summary().plates)
    {
        if (plate.uprightFootprint.empty())
        {
            sources.push_back({plate, modelPartFrame(model.summary(), plate.id), {}});
        }
        else
        {
            result->summary.localSourceUnavailable[plate.id] = "弯板/连接组不适用平板轮廓重建，可使用包围盒规则。";
        }
    }
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 4096 == 0)
            checkpoint(stop, {}, 0, {});
        const auto &p = cloud.points[i];
        double best = std::numeric_limits<double>::infinity();
        std::size_t chosen = sources.size();
        for (std::size_t j = 0; j < sources.size(); ++j)
        {
            const auto &plate = sources[j].plate;
            const auto point = inverseTransformPoint(
                sources[j].frame,
                {p.position.x + cloud.originMm.x, p.position.y + cloud.originMm.y, p.position.z + cloud.originMm.z});
            const double q[] = {point.x, point.y, point.z};
            const double low[] = {plate.minimumMm.x, plate.minimumMm.y, plate.minimumMm.z};
            const double high[] = {plate.maximumMm.x, plate.maximumMm.y, plate.maximumMm.z};
            double distance = 0;
            for (int a = 0; a < 3; ++a)
            {
                const double d = a == plate.normalAxis ? std::min(std::abs(q[a] - low[a]), std::abs(q[a] - high[a]))
                                                       : std::max({low[a] - q[a], q[a] - high[a], 0.0});
                distance += d * d;
            }
            // 仅关联原板面附近的点，远离原模型的杂点不参与后续局部重建。
            if (distance <= 25.0 && distance < best)
            {
                best = distance;
                chosen = j;
            }
        }
        if (chosen < sources.size())
            sources[chosen].indices.push_back(p.sourceIndex);
    }
    result->summary.reconstructableParts.clear();
    for (auto &source : sources)
    {
        std::sort(source.indices.begin(), source.indices.end());
        source.indices.erase(std::unique(source.indices.begin(), source.indices.end()), source.indices.end());
        if (source.indices.size() < 12)
            continue;
        const auto id = source.plate.id;
        result->cloudSources[id] = std::make_shared<const detail::LocalPlateSource>(std::move(source));
        result->summary.reconstructableParts.push_back(id);
    }
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> reconstructLocalParts(const Model &model, const CloudData &cloud,
                                                   const LocalReconstruction &request, std::stop_token stop,
                                                   const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查局部重建规则和点云关联");
    const std::set<std::string> types{"plate", "horizontal", "box"};
    const std::set<std::string> contours{
        "circle",  "rectangle", "chamfer_rectangle", "quarter_hole_rectangle", "rounded_rectangle", "convex",
        "polygon", "smooth"};
    const bool box = request.type == "box";
    if (!types.contains(request.type) || (!box && !contours.contains(request.contour)))
        throw GeometryError("invalid_rule", "局部重建类型或轮廓规则无效。");
    if (!box && (!std::isfinite(request.toleranceMm) || request.toleranceMm < 0.1 || request.toleranceMm > 1000 ||
                 !std::isfinite(request.cornerMm) || request.cornerMm <= 0 || request.cornerMm > 100000))
        throw GeometryError("invalid_rule", "轮廓容差须为 0.1–1000 mm，角部退距或半径须大于 0 且不超过 100000 mm。");
    std::set<std::string> selected(request.partIds.begin(), request.partIds.end());
    if (selected.empty() || selected.size() != request.partIds.size())
        throw GeometryError("invalid_parts", "请选择至少一个部件，部件 ID 不可重复。");
    const auto &source = *model.implementation_;
    for (const auto &id : selected)
    {
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "部件不存在：" + id);
        if (box)
            continue; // 包围盒表示当前模型占用空间，与板面点云关联无关。
        if (source.summary.obstacleParts.contains(id))
            throw GeometryError("obstacle_rule", "部件 " + id + " 是障碍物，请选择包围盒重建。");
        if (!source.cloudSources.contains(id))
            throw GeometryError("no_local_source", "部件 " + id + "：" +
                                                       (source.summary.localSourceUnavailable.contains(id)
                                                            ? source.summary.localSourceUnavailable.at(id)
                                                            : "没有足够的原始点云关联，不能局部重建。"));
        if (source.summary.partTransforms.contains(id))
            throw GeometryError("moved_local_source",
                                "部件 " + id + " 已移动或旋转，与原点云位置不一致；请撤销变换后再局部重建。");
    }
    auto result = std::make_shared<Model::Impl>(source);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.solidCount = 0;
    result->summary.surfaceCount = 0;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t index = 0; index < result->plates.size(); ++index)
    {
        auto &part = result->plates[index];
        checkpoint(stop, progress, 0.1 + 0.8 * index / result->plates.size(), "局部重建：" + part.id);
        try
        {
            if (selected.contains(part.id))
            {
                detail::LocalReconstructionShape rebuilt;
                std::size_t supportCount = 0;
                if (box)
                {
                    rebuilt.shape =
                        detail::rebuildOccupancyBox(part.shape, modelPartFrame(source.summary, part.id), stop);
                    auto &info = result->summary.obstacleParts[part.id];
                    info.boundingBox = true;
                    if (const auto support = source.cloudSources.find(part.id); support != source.cloudSources.end())
                        info.supportPoints = support->second->indices.size();
                    result->cloudSources.erase(part.id);
                    result->summary.localSourceUnavailable.erase(part.id);
                    std::erase(result->summary.reconstructableParts, part.id);
                    std::erase_if(result->summary.plates, [&](const auto &plate) { return plate.id == part.id; });
                    result->summary.generatedPlates.erase(part.id);
                    result->summary.importedThicknesses.erase(part.id);
                }
                else
                {
                    const auto &support = *source.cloudSources.at(part.id);
                    std::vector<Vec3> remaining;
                    for (std::size_t i = 0; i < cloud.points.size(); ++i)
                    {
                        if (i % 4096 == 0)
                            checkpoint(stop, {}, 0, {});
                        const auto &p = cloud.points[i];
                        if (std::binary_search(support.indices.begin(), support.indices.end(), p.sourceIndex))
                            remaining.push_back({p.position.x + cloud.originMm.x, p.position.y + cloud.originMm.y,
                                                 p.position.z + cloud.originMm.z});
                    }
                    auto thickness = support.plate.thicknessMm;
                    if (source.summary.thicknessOverrides.contains(part.id))
                        thickness = source.summary.thicknessOverrides.at(part.id).thicknessMm;
                    rebuilt = detail::rebuildLocalShape(remaining, support, thickness, request, stop);
                    supportCount = remaining.size();
                }
                part.shape = rebuilt.shape;
                const auto boxBounds = bounds(part.shape);
                part.minimum = {boxBounds[0], boxBounds[1], boxBounds[2]};
                part.maximum = {boxBounds[3], boxBounds[4], boxBounds[5]};
                std::erase(result->summary.edgeTreatedParts, part.id);
                std::erase(result->summary.holeEditedParts, part.id);
                std::erase(result->summary.contourExpandedParts, part.id);
                std::erase(result->summary.mirrorCompletedParts, part.id);
                if (std::find(result->summary.locallyReconstructedParts.begin(),
                              result->summary.locallyReconstructedParts.end(),
                              part.id) == result->summary.locallyReconstructedParts.end())
                    result->summary.locallyReconstructedParts.push_back(part.id);
                if (rebuilt.thickness)
                    result->summary.thicknessOverrides[part.id] = *rebuilt.thickness;
                else
                    result->summary.thicknessOverrides.erase(part.id);
                for (auto &plate : result->summary.plates)
                    if (plate.id == part.id)
                    {
                        plate.supportPoints = supportCount;
                        plate.method = "local_reconstruction";
                        if (result->summary.thicknessOverrides.contains(part.id))
                            plate.thicknessMm = result->summary.thicknessOverrides.at(part.id).thicknessMm;
                    }
            }
            else
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                ++result->summary.solidCount;
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            else
                ++result->summary.surfaceCount;
            appendMesh(part.shape, static_cast<std::uint32_t>(index), result->mesh);
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + part.id + "：" + error.what() + " 整批未应用。");
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("local_rebuild_failed",
                                "部件 " + part.id + " 局部重建失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "局部重建完成，未选部件保持不变");
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> buildModelByPoints(const Model *source, const PointModeling &request, std::stop_token stop,
                                                const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "计算选点轮廓");
    const auto tool = detail::pointModelShape(request);
    if (request.cut && !source)
        throw GeometryError("no_model", "切除需要已有模型。");
    auto result = source ? std::make_shared<Model::Impl>(*source->implementation_) : std::make_shared<Model::Impl>();
    auto &summary = result->summary;
    const auto clearGeometryInfo = [&](const std::string &id)
    {
        result->cloudSources.erase(id);
        std::erase(summary.reconstructableParts, id);
        std::erase_if(summary.plates, [&](const auto &p) { return p.id == id; });
        summary.thicknessOverrides.erase(id);
        summary.importedThicknesses.erase(id);
        summary.generatedPlates.erase(id);
        summary.partTransforms.erase(id);
        summary.localSourceUnavailable[id] = "选点切除已修改轮廓，原点云关联不再适用。";
    };
    if (request.cut)
    {
        if (request.cutScope != "all" && request.cutScope != "no_base" && request.cutScope != "manual")
            throw GeometryError("invalid_cut_scope", "切除范围无效。");
        const std::set<std::string> selected(request.targets.begin(), request.targets.end());
        if (request.cutScope == "manual")
        {
            if (selected.empty())
                throw GeometryError("no_cut_targets", "请选中要切除的模型部件。");
            for (const auto &id : selected)
                if (std::find(summary.partIds.begin(), summary.partIds.end(), id) == summary.partIds.end())
                    throw GeometryError("unknown_part", "切除对象不存在：" + id);
        }
        std::vector<Plate> parts;
        bool changed = false;
        for (const auto &old : result->plates)
        {
            checkpoint(stop, progress, 0.2, "切除：" + old.id);
            const bool base =
                old.id == "web" || old.id == "other-base" || (old.id.starts_with("tbar-") && old.id.ends_with("-base"));
            if (old.shape.ShapeType() != TopAbs_SOLID || (request.cutScope == "manual" && !selected.contains(old.id)) ||
                (request.cutScope == "no_base" && base))
            {
                parts.push_back(old);
                continue;
            }
            auto original = BRepBuilderAPI_Copy(old.shape, true, false).Shape();
            BRepAlgoAPI_Cut cut(original, tool);
            if (!cut.IsDone() || cut.Shape().IsNull() || !BRepCheck_Analyzer(cut.Shape()).IsValid())
                throw GeometryError("point_cut_failed", "切除失败，所有部件均未修改：" + old.id);
            double volume = 0;
            std::vector<TopoDS_Shape> solids;
            for (TopExp_Explorer it(cut.Shape(), TopAbs_SOLID); it.More(); it.Next())
            {
                volume += checkedVolume(it.Current());
                solids.push_back(it.Current());
            }
            if (std::abs(checkedVolume(old.shape) - volume) <= 1e-6)
            {
                parts.push_back(old);
                continue;
            }
            changed = true;
            clearGeometryInfo(old.id);
            for (std::size_t i = 0; i < solids.size(); ++i)
            {
                auto part = old;
                if (i)
                    part.id = old.id + "-split-" + std::to_string(summary.nextSplitId++);
                part.shape = solids[i];
                parts.push_back(part);
                summary.editSources[part.id] = {old.id};
                summary.contourExpandedParts.push_back(part.id);
                if (summary.obstacleParts.contains(old.id))
                    summary.obstacleParts[part.id] = summary.obstacleParts.at(old.id);
                else
                {
                    try
                    {
                        summary.thicknessOverrides[part.id] = detail::inspectPlateThickness(part.shape);
                    }
                    catch (const GeometryError &)
                    {
                    }
                }
            }
        }
        if (!changed)
            throw GeometryError("no_point_cut", "选点模型与切除范围没有实体交集，未修改模型。");
        result->plates = std::move(parts);
    }
    else
    {
        std::string id;
        do
        {
            id = (request.shape == "box" ? "point-obstacle-" : "point-model-") +
                 std::to_string(summary.nextGeneratedPlateId++);
        } while (std::find(summary.partIds.begin(), summary.partIds.end(), id) != summary.partIds.end());
        result->plates.push_back({id, {}, {}, tool});
        summary.contourExpandedParts.push_back(id);
        if (request.shape == "box")
            summary.obstacleParts[id] = {request.points.size(), 0, 0, true};
        else
        {
            try
            {
                summary.thicknessOverrides[id] = detail::inspectPlateThickness(tool);
            }
            catch (const GeometryError &)
            {
            }
        }
    }
    summary.partIds.clear();
    summary.solidCount = summary.surfaceCount = 0;
    summary.volumeMm3 = 0;
    result->mesh = {};
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.3 + 0.65 * i / result->plates.size(), "生成选点模型网格");
        part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
        const auto box = bounds(part.shape);
        part.minimum = {box[0], box[1], box[2]};
        part.maximum = {box[3], box[4], box[5]};
        builder.Add(result->compound, part.shape);
        summary.partIds.push_back(part.id);
        if (part.shape.ShapeType() == TopAbs_SOLID)
        {
            ++summary.solidCount;
            summary.volumeMm3 += checkedVolume(part.shape);
        }
        else
            ++summary.surfaceCount;
        appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
    }
    result->mesh.partIds = summary.partIds;
    const auto gone = [&](const auto &id)
    { return std::find(summary.partIds.begin(), summary.partIds.end(), id) == summary.partIds.end(); };
    std::erase_if(summary.obstacleParts, [&](const auto &p) { return gone(p.first); });
    std::erase_if(summary.contourExpandedParts, gone);
    checkpoint(stop, progress, 1, "选点建模完成");
    return publishModel(std::move(result), stop);
}

} // namespace reverse
