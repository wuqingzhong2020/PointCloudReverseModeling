/* 文件描述：连接板件查询、镜像补全、型钢和附加板生成，以及直接板件编辑。 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <Standard_Failure.hxx>

#include "geometry/MirrorCompletion.h"
#include "geometry/ModelState.h"
#include "geometry/PlateThickness.h"
#include "geometry/PlateEditing.h"
#include "geometry/PlateGeneration.h"
#include "geometry/StructuralSteel.h"
#include "geometry/assembly/AssemblyPlateEditing.h"

namespace reverse
{
using detail::model::appendMesh;
using detail::model::appendModelPart;
using detail::model::beginModelPresentation;
using detail::model::PartBounds;
using detail::model::bounds;
using detail::model::checkedVolume;
using detail::model::checkpoint;
using detail::model::Plate;
using detail::model::publishModel;

std::vector<std::string> connectedModelParts(const Model &model, const std::string &seedPartId, std::stop_token stop,
                                             const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "查找选中工件的相连部件");
    const auto &parts = model.implementation_->plates;
    if (model.summary().obstacleParts.contains(seedPartId))
        throw GeometryError("obstacle_selection", "请选择工件板件；障碍物仅用于避障，不参与镜像补全。");
    const auto seed = std::find_if(parts.begin(), parts.end(), [&](const auto &part) { return part.id == seedPartId; });
    if (seed == parts.end())
        throw GeometryError("unknown_part", "请先选择目标工件的一个部件。");
    constexpr double contactToleranceMm = 0.15;
    std::vector<Bnd_Box> boxes(parts.size());
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        checkpoint(stop, {}, 0, {});
        BRepBndLib::AddOptimal(parts[i].shape, boxes[i], false, false);
    }
    const auto seedIndex = static_cast<std::size_t>(seed - parts.begin());
    std::vector<std::size_t> queue{seedIndex};
    std::vector<bool> included(parts.size(), false);
    included[seedIndex] = true;
    for (std::size_t head = 0; head < queue.size(); ++head)
    {
        const auto current = queue[head];
        checkpoint(stop, progress, 0.95 * head / parts.size(), "查找相连部件：" + parts[current].id);
        for (std::size_t candidate = 0; candidate < parts.size(); ++candidate)
        {
            checkpoint(stop, {}, 0, {});
            if (included[candidate] || model.summary().obstacleParts.contains(parts[candidate].id) ||
                boxes[current].Distance(boxes[candidate]) > contactToleranceMm)
                continue;
            // 包围盒仅用于排除远离部件；重叠包围盒并不能证明实体相连。
            if (detail::mirrorPartsConnected(parts[current].shape, parts[candidate].shape, contactToleranceMm, stop))
            {
                included[candidate] = true;
                queue.push_back(candidate);
            }
        }
    }
    std::vector<std::string> result;
    for (std::size_t i = 0; i < parts.size(); ++i)
        if (included[i])
            result.push_back(parts[i].id);
    checkpoint(stop, progress, 1, "已选中相连工件（" + std::to_string(result.size()) + " 个部件）");
    return result;
}

std::shared_ptr<const Model> completeModelMirror(const Model &model, const MirrorCompletion &request,
                                                 std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查镜像板件与底板中面");
    const auto &source = *model.implementation_;
    if (request.planePartId.empty())
        throw GeometryError("no_mirror_plane", "请选择作为镜像面的底板（腹板）。");
    if (std::find(request.partIds.begin(), request.partIds.end(), request.planePartId) != request.partIds.end())
        throw GeometryError("invalid_parts", "底板（腹板）是镜像参考，不能同时作为待补全板件；请取消选中该板。");
    if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), request.planePartId) ==
        source.summary.partIds.end())
        throw GeometryError("unknown_mirror_plane", "参数 --plane-part 的参考板件不存在：" + request.planePartId);
    PlateThicknessInfo plane;
    try
    {
        plane = inspectModelThickness(model, {request.planePartId}, stop).at(request.planePartId);
    }
    catch (const GeometryError &error)
    {
        throw GeometryError(error.code(), "镜像参考板 --plane-part 无法使用：" + std::string(error.what()));
    }
    const auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(source);
    bool changed = false;
    for (const auto &[id, thickness] : selected)
    {
        checkpoint(stop, progress, 0.1, "镜像补全：" + id);
        const auto original =
            std::find_if(source.plates.begin(), source.plates.end(), [&](const auto &p) { return p.id == id; });
        try
        {
            const auto reflected = detail::mirrorPlate(original->shape, plane);
            const double reflectedVolume = checkedVolume(reflected);
            const double tolerance = std::max(1e-4, reflectedVolume * 1e-7);
            double covered = 0;
            // 同板原有部分及已存在的对称部件均参与去重，避免重复应用生成重叠板。
            for (const auto &part : result->plates)
            {
                if (part.shape.ShapeType() != TopAbs_SOLID)
                    continue;
                const double overlap = detail::mirrorOverlapVolume(part.shape, reflected, stop);
                if (part.id != id && overlap > tolerance && overlap < reflectedVolume - tolerance)
                    throw GeometryError("mirror_overlap",
                                        "镜像区域与部件 " + part.id + " 部分重叠，请先调整板件或镜像参考。");
                covered += overlap;
            }
            if (covered >= reflectedVolume - tolerance)
                continue;
            auto united = detail::uniteMirrorPlate(original->shape, reflected, stop);
            std::string targetId = id;
            if (!united.IsNull())
            {
                auto target = std::find_if(result->plates.begin(), result->plates.end(),
                                           [&](const auto &p) { return p.id == id; });
                target->shape = united;
                auto &completed = result->summary.mirrorCompletedParts;
                if (std::find(completed.begin(), completed.end(), id) == completed.end())
                    completed.push_back(id);
            }
            else
            {
                do
                {
                    targetId = "mirrored-part-" + std::to_string(result->summary.nextMirroredPartId++);
                } while (std::find(result->summary.partIds.begin(), result->summary.partIds.end(), targetId) !=
                         result->summary.partIds.end());
                result->plates.push_back({targetId, {}, {}, reflected});
                result->summary.partIds.push_back(targetId);
                result->summary.mirroredParts[targetId] = id;
                ++result->summary.solidCount;
                united = reflected;
            }
            // 手动板厚随补全更新锚点；导入实体的几何板厚在发布时统一计算。
            if (source.summary.importedFormat.empty() || source.summary.thicknessOverrides.contains(id))
                result->summary.thicknessOverrides[targetId] = detail::inspectPlateThickness(united);
            changed = true;
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + id + " 镜像补全失败：" + error.what());
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("mirror_failed", "部件 " + id + " 镜像补全失败，整批未应用：" + error.what());
        }
    }
    if (!changed)
        throw GeometryError("already_symmetric", "选中板件的镜像区域已存在，无需补全；原模型未修改。");
    beginModelPresentation(*result);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        checkpoint(stop, progress, 0.5 + 0.49 * i / result->plates.size(), "生成镜像显示网格");
        appendModelPart(*result, i, PartBounds::World);
    }
    checkpoint(stop, progress, 1, "镜像补全完成");
    return publishModel(std::move(result), stop);
}

std::shared_ptr<const Model> setModelStructuralSteel(const Model &model, const StructuralSteel &request,
                                                     std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查型钢开口面");
    const auto &source = *model.implementation_;
    if (source.summary.obstacleParts.contains(request.partId))
        throw GeometryError("not_plate", "避障包络不能设为型钢，请选择工件部件。");
    auto result = std::make_shared<Model::Impl>(source);
    const auto selected = std::find_if(result->plates.begin(), result->plates.end(),
                                       [&](const auto &p) { return p.id == request.partId; });
    if (selected == result->plates.end())
        throw GeometryError("unknown_part", "部件不存在：" + request.partId);
    try
    {
        std::vector<TopoDS_Shape> supports;
        for (const auto &p : source.plates)
        {
            if (p.id != request.partId && !source.summary.obstacleParts.contains(p.id))
                supports.push_back(p.shape);
        }
        const auto section = detail::structuralSteelSection(selected->shape, request, supports);
        selected->shape = detail::buildSteelSection(section);
        auto &summary = result->summary;
        summary.thicknessOverrides[request.partId] = detail::steelThickness(section);
        if (std::find(summary.contourExpandedParts.begin(), summary.contourExpandedParts.end(), request.partId) ==
            summary.contourExpandedParts.end())
            summary.contourExpandedParts.push_back(request.partId);
        // 源扫描轮廓已被型钢截面替换，保留点索引来源，但不再沿用原板厚/围板参数。
        for (auto &plate : summary.plates)
        {
            if (plate.id == request.partId)
            {
                plate.thicknessMm = request.thicknessMm;
                plate.uprightFootprint.clear();
            }
        }
        beginModelPresentation(*result);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            checkpoint(stop, progress, 0.5 + 0.49 * i / result->plates.size(), "生成型钢显示网格");
            appendModelPart(*result, i, PartBounds::World);
        }
        checkpoint(stop, progress, 1, "型钢调整完成");
        return publishModel(std::move(result), stop);
    }
    catch (const Standard_Failure &error)
    {
        throw GeometryError("steel_build_failed", "型钢生成失败，原模型未修改：" + std::string(error.what()));
    }
}

std::shared_ptr<const Model> generateModelPlate(const Model &model, const PlateGeneration &request,
                                                std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查附加板适用性");
    const auto &source = *model.implementation_;
    const bool pair = request.kind == PlateGenerationKind::Base && request.baseMode == BasePlateMode::VerticalPair;
    if (pair && (request.secondPartId.empty() || request.secondPartId == request.partId))
    {
        throw GeometryError("invalid_parts",
                            "两块竖直板方式需要两个不同的部件；请用 Shift 加选第二块板，或填写 --second-part。");
    }
    if (!pair && !request.secondPartId.empty())
    {
        throw GeometryError("invalid_parts",
                            "当前方式只允许一块板；请清空 --second-part 或选择 --mode vertical_pair。");
    }
    const auto selected =
        std::find_if(source.plates.begin(), source.plates.end(), [&](const auto &p) { return p.id == request.partId; });
    if (selected == source.plates.end())
        throw GeometryError("unknown_part", "部件不存在：" + request.partId);
    TopoDS_Shape secondSource;
    if (pair)
    {
        const auto second = std::find_if(source.plates.begin(), source.plates.end(),
                                         [&](const auto &p) { return p.id == request.secondPartId; });
        if (second == source.plates.end())
        {
            throw GeometryError("unknown_part", "第二块部件不存在：" + request.secondPartId);
        }
        secondSource = second->shape;
    }
    try
    {
        const auto shape = detail::generateAttachedPlate(selected->shape, request, secondSource);
        auto result = std::make_shared<Model::Impl>(source);
        const auto prefix = request.kind == PlateGenerationKind::Cover ? "generated-cover-" : "generated-base-";
        std::string id;
        do
        {
            id = prefix + std::to_string(result->summary.nextGeneratedPlateId++);
        } while (std::find(result->summary.partIds.begin(), result->summary.partIds.end(), id) !=
                 result->summary.partIds.end());
        const auto box = bounds(shape);
        result->plates.push_back({id, {box[0], box[1], box[2]}, {box[3], box[4], box[5]}, shape});
        result->summary.partIds.push_back(id);
        result->summary.generatedPlates[id] = {request.kind, request.partId, request.secondPartId, request.baseMode};
        const auto thickness = detail::inspectPlateThickness(shape);
        if (std::abs(thickness.thicknessMm - request.thicknessMm) > 1e-5)
            throw GeometryError("unsuitable_plate_dimensions",
                                "新增板厚大于轮廓窄边尺寸，不再是适合的板件；请减小板厚或增大外扩量。");
        result->summary.thicknessOverrides[id] = thickness;
        ++result->summary.solidCount;
        beginModelPresentation(*result);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            checkpoint(stop, progress, 0.5 + 0.49 * i / result->plates.size(), "生成附加板显示网格");
            appendModelPart(*result, i);
        }
        checkpoint(stop, progress, 1, "附加板生成完成：" + id);
        return publishModel(std::move(result), stop);
    }
    catch (const Standard_Failure &error)
    {
        throw GeometryError("plate_generation_failed", "生成板件失败，原模型未修改：" + std::string(error.what()));
    }
}

PlateEdit prepareDirectPlateEdit(const Model &model, PlateEdit request, bool all, std::stop_token stop,
                                 const std::function<void(std::string_view)> &output)
{
    std::map<std::string, TopoDS_Shape> shapes;
    for (const auto &part : model.implementation_->plates)
        shapes.emplace(part.id, part.shape);
    detail::prepareDirectPlateEdit(shapes, request, all, stop, output);
    return request;
}

EndCapInspection inspectModelEndCap(const Model &model, const std::string &part, std::stop_token stop)
{
    std::map<std::string, TopoDS_Shape> shapes;
    for (const auto &p : model.implementation_->plates)
        if (!model.summary().obstacleParts.contains(p.id))
            shapes.emplace(p.id, p.shape);
    return detail::inspectEndCapShapes(shapes, part, stop);
}

PlateEditResult editModelPlates(const Model &model, PlateEdit request, std::stop_token stop,
                                const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件编辑范围");
    std::map<std::string, TopoDS_Shape> shapes;
    for (const auto &p : model.implementation_->plates)
    {
        if ((request.operation == "cross" || request.operation == "cap") &&
            model.summary().obstacleParts.contains(p.id))
        {
            if (p.id == request.target ||
                std::find(request.parts.begin(), request.parts.end(), p.id) != request.parts.end())
                throw GeometryError("plate_obstacle", "障碍物不能参与板件优化，请选择实际板件。");
            continue;
        }
        shapes.emplace(p.id, p.shape);
    }
    auto edited = detail::editPlateShapes(shapes, request, stop);
    if (edited.replacements.empty())
        return {std::make_shared<const Model>(model.implementation_), std::move(request), std::move(edited.reports),
                false};
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    auto &summary = result->summary;
    result->plates.clear();
    result->mesh = {};
    summary.partIds.clear();
    summary.solidCount = summary.surfaceCount = 0;
    summary.volumeMm3 = 0;
    std::set<std::string> removed;
    for (const auto &old : model.implementation_->plates)
    {
        const auto found = edited.replacements.find(old.id);
        if (found == edited.replacements.end())
        {
            result->plates.push_back(old);
            continue;
        }
        const auto &replacements = found->second;
        if (replacements.size() != 1)
            removed.insert(old.id);
        for (const auto &shape : replacements)
        {
            Plate part = old;
            part.shape = shape;
            if (replacements.size() > 1)
                part.id = old.id + "-split-" + std::to_string(summary.nextSplitId++);
            const auto box = bounds(shape);
            part.minimum = {box[0], box[1], box[2]};
            part.maximum = {box[3], box[4], box[5]};
            result->plates.push_back(part);
            if (std::find(summary.contourExpandedParts.begin(), summary.contourExpandedParts.end(), part.id) ==
                summary.contourExpandedParts.end())
                summary.contourExpandedParts.push_back(part.id);
            summary.thicknessOverrides[part.id] = request.direct ? detail::assemblyPlateThickness(shape)
                                                  : request.operation == "repair"
                                                      ? detail::inspectRepairPlateThickness(shape)
                                                      : detail::inspectPlateThickness(shape);
            summary.editSources[part.id] = {old.id};
            if (request.operation == "cross" || request.operation == "cap")
            {
                result->cloudSources.erase(old.id);
                std::erase(summary.reconstructableParts, old.id);
                std::erase_if(summary.plates, [&](const auto &p) { return p.id == old.id; });
                summary.partTransforms.erase(old.id);
                summary.localSourceUnavailable[part.id] = "板件优化已改变连接或轮廓，请重新全局重建以更新支持点。";
            }
            if (request.operation == "merge" && part.id == request.target)
            {
                summary.editSources[part.id].insert(summary.editSources[part.id].end(), request.parts.begin(),
                                                    request.parts.end());
                result->cloudSources.erase(part.id);
                std::erase(summary.reconstructableParts, part.id);
                summary.localSourceUnavailable[part.id] = "合并校正后无法可靠沿用原点云索引，请重新全局重建。";
            }
            else if (replacements.size() > 1 && result->cloudSources.contains(old.id))
            {
                // 分割子件不能共用父件支持点，否则局部重建会恢复整块父板。
                summary.localSourceUnavailable[part.id] =
                    "分割后原支持点尚未重新分配，不能局部重建此子件；其他部件不受影响。";
            }
        }
    }
    std::erase_if(result->cloudSources, [&](const auto &p) { return removed.contains(p.first); });
    for (auto *ids : {&summary.reconstructableParts, &summary.locallyReconstructedParts, &summary.mirrorCompletedParts,
                      &summary.contourExpandedParts, &summary.holeEditedParts, &summary.edgeTreatedParts})
        std::erase_if(*ids, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.obstacleParts, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.plates, [&](const auto &p) { return removed.contains(p.id); });
    std::erase_if(summary.thicknessOverrides, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.partTransforms, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.generatedPlates, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.mirroredParts, [&](const auto &p) { return removed.contains(p.first); });
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.7 + 0.29 * i / result->plates.size(), "更新板件显示");
        part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
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
    checkpoint(stop, progress, 1, "板件编辑完成");
    return {publishModel(std::move(result), stop), std::move(request), std::move(edited.reports), true};
}

} // namespace reverse
