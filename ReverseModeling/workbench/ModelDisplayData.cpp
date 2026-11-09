/*
 * 文件描述：
 * 按三角形部件归属拆分显示网格，保留稳定部件 ID，并从建模快照取得板厚。
 * 生成与显示原点一致的标注锚点及统一灰色配色，不改变业务几何、STEP 或工程参数。
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "workbench/ModelDisplayData.h"
#include "geometry/ModelTransform.h"

namespace reverse::workbench
{
std::vector<ModelSelection> updateModelSelection(std::vector<ModelSelection> selected,
    const ModelSelection &clicked, CloudSelectionMode mode, bool toggle)
{
    if (mode == CloudSelectionMode::Clear) return {};
    if (toggle && clicked.partId.empty()) return selected;
    if (toggle)
        mode = std::find(selected.begin(), selected.end(), clicked) != selected.end()
            ? CloudSelectionMode::Remove : CloudSelectionMode::Add;
    if (mode == CloudSelectionMode::Replace) selected.clear();
    if (clicked.partId.empty()) return selected;
    if (mode == CloudSelectionMode::Remove) std::erase(selected, clicked);
    else if (std::find(selected.begin(), selected.end(), clicked) == selected.end()) selected.push_back(clicked);
    return selected;
}

std::vector<std::string> updateModelPartSelection(std::vector<std::string> selected,
    const std::string &clicked, CloudSelectionMode mode, bool toggle)
{
    std::vector<ModelSelection> objects;
    for (const auto &id : selected) objects.push_back({id, ModelPickMode::Body, 0});
    selected.clear();
    for (const auto &object : updateModelSelection(std::move(objects), {clicked, ModelPickMode::Body, 0}, mode, toggle))
        selected.push_back(object.partId);
    return selected;
}

std::vector<EdgeTreatmentTarget> edgeTreatmentTargets(
    const MeshData &mesh, const std::vector<ModelSelection> &selections)
{
    std::vector<EdgeTreatmentTarget> targets;
    for (const auto &selection : selections)
    {
        if (selection.kind != ModelPickMode::Edge || selection.partId.empty())
            throw std::invalid_argument("圆角 / 倒角只能选择实体边线。");
        std::size_t local = 0;
        std::uint32_t topology = 0;
        for (const auto &edge : mesh.edges)
            if (mesh.partIds.at(edge.partIndex) == selection.partId && local++ == selection.index)
            {
                topology = edge.topologyIndex;
                break;
            }
        if (topology == 0)
            throw std::invalid_argument("选中的边已失效，请重新选择：" + selection.partId);
        auto target = std::find_if(targets.begin(), targets.end(),
            [&](const auto &item) { return item.partId == selection.partId; });
        if (target == targets.end())
            targets.push_back({selection.partId, {topology}});
        else if (std::find(target->edges.begin(), target->edges.end(), topology) == target->edges.end())
            target->edges.push_back(topology);
    }
    return targets;
}

bool isValidModelSelection(const ModelDisplayPart &part, const ModelSelection &selection)
{
    if (selection.partId.empty() || part.id != selection.partId) return false;
    switch (selection.kind)
    {
    case ModelPickMode::Body: return selection.index == 0;
    case ModelPickMode::Vertex: return selection.index < part.keyPoints.size();
    case ModelPickMode::Edge:
        return selection.index < part.edges.size() && part.edges[selection.index].size() >= 2;
    case ModelPickMode::Face:
        return std::find(part.triangleFaceIndices.begin(), part.triangleFaceIndices.end(), selection.index)
            != part.triangleFaceIndices.end();
    }
    return false;
}

namespace
{
std::optional<double> partThickness(std::string_view id, const BeamParameters &parameters)
{
    if (id == "web")
    {
        return parameters.webThickness;
    }
    if (id == "flange-bottom" || id == "flange-top")
    {
        return parameters.flangeThickness;
    }
    if (id.starts_with("rib-"))
    {
        return parameters.ribThickness;
    }
    return std::nullopt;
}

std::array<ModelLabelAnchor, 2> labelFaces(
    const ModelDisplayPart &part, const BeamFrame &frame, const Vec3 &displayOrigin, int plateNormalAxis)
{
    const Vec3 origin{frame.originMm.x - displayOrigin.x, frame.originMm.y - displayOrigin.y,
                      frame.originMm.z - displayOrigin.z};
    const std::array axes{frame.xAxis, frame.yAxis, frame.zAxis};
    std::array<double, 3> minimum;
    std::array<double, 3> maximum;
    minimum.fill(std::numeric_limits<double>::max());
    maximum.fill(std::numeric_limits<double>::lowest());
    for (const auto &vertex : part.vertices)
    {
        const Vec3 delta{vertex.x - origin.x, vertex.y - origin.y, vertex.z - origin.z};
        for (std::size_t axis = 0; axis < axes.size(); ++axis)
        {
            const double value = delta.x * axes[axis].x + delta.y * axes[axis].y + delta.z * axes[axis].z;
            minimum[axis] = std::min(minimum[axis], value);
            maximum[axis] = std::max(maximum[axis], value);
        }
    }
    std::array<double, 3> position;
    for (std::size_t axis = 0; axis < axes.size(); ++axis)
    {
        position[axis] = (minimum[axis] + maximum[axis]) / 2.0;
    }
    // 长板错开纵向位置；标注锚点落在对应板件表面，避免全部叠在梁中心。
    std::size_t normalAxis = static_cast<std::size_t>(plateNormalAxis);
    if (part.id.starts_with("flange-"))
    {
        position[0] = minimum[0] + (maximum[0] - minimum[0]) * (part.id == "flange-bottom" ? 0.43 : 0.59);
        normalAxis = 2;
    }
    else if (part.id == "web")
    {
        position[0] = minimum[0] + (maximum[0] - minimum[0]) * 0.51;
        normalAxis = 1;
    }
    std::array<ModelLabelAnchor, 2> faces;
    for (std::size_t side = 0; side < faces.size(); ++side)
    {
        position[normalAxis] = side == 0 ? minimum[normalAxis] : maximum[normalAxis];
        const double sign = side == 0 ? -1.0 : 1.0;
        faces[side].normal = {axes[normalAxis].x * sign, axes[normalAxis].y * sign, axes[normalAxis].z * sign};
        auto &anchor = faces[side].position;
        anchor = origin;
        for (std::size_t axis = 0; axis < axes.size(); ++axis)
        {
            anchor.x += axes[axis].x * position[axis];
            anchor.y += axes[axis].y * position[axis];
            anchor.z += axes[axis].z * position[axis];
        }
    }
    return faces;
}
}

std::vector<ModelDisplayPart> makeModelDisplayParts(
    const MeshData &mesh, const ModelSummary &model, const Vec3 &displayOrigin)
{
    if (mesh.trianglePartIndices.size() != mesh.triangles.size() || mesh.partIds != model.partIds ||
        (!mesh.triangleFaceIndices.empty() && mesh.triangleFaceIndices.size() != mesh.triangles.size()))
    {
        throw std::invalid_argument("显示网格的部件归属与模型快照不一致。");
    }
    std::vector<ModelDisplayPart> parts(mesh.partIds.size());
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> vertexMaps(parts.size());
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        parts[index].id = mesh.partIds[index];
        parts[index].colorSrgb = ModelColors::surface;
        parts[index].thicknessMm = model.importedFormat.empty() ? partThickness(parts[index].id, model.parameters) : std::nullopt;
        // 封板/交叉板优化会失效原识别记录；编辑后的实体板厚才是当前显示依据。
        if (model.fromPointCloud && !model.thicknessOverrides.contains(parts[index].id) &&
            !model.obstacleParts.contains(parts[index].id) && !model.generatedPlates.contains(parts[index].id) && !model.mirroredParts.contains(parts[index].id))
        {
            const auto observed = std::find_if(model.plates.begin(), model.plates.end(),
                [&](const auto &plate) { return plate.id == parts[index].id; });
            if (observed == model.plates.end())
            {
                throw std::invalid_argument("识别模型缺少对应板件的板厚记录。");
            }
            parts[index].thicknessMm = observed->thicknessMm;
            parts[index].thicknessUnresolved = !observed->thicknessMm.has_value();
            parts[index].thicknessEstimated = observed->method == "inferred_side_plate_max";
        }
        if (const auto info = model.importedThicknesses.find(parts[index].id); info != model.importedThicknesses.end())
            parts[index].thicknessMm = info->second.thicknessMm;
        if (const auto info = model.thicknessOverrides.find(parts[index].id); info != model.thicknessOverrides.end())
        {
            parts[index].thicknessMm = info->second.thicknessMm;
            parts[index].thicknessUnresolved = false;
            parts[index].thicknessEstimated = false;
        }
        if (model.obstacleParts.contains(parts[index].id))
        {
            parts[index].thicknessMm.reset();
            parts[index].thicknessUnresolved = false;
            parts[index].thicknessEstimated = false;
        }
        if (parts[index].thicknessMm && (!std::isfinite(*parts[index].thicknessMm) || *parts[index].thicknessMm <= 0))
        {
            throw std::invalid_argument("模型快照包含无效的板厚标注值。");
        }
    }
    for (std::size_t index = 0; index < mesh.triangles.size(); ++index)
    {
        const auto partIndex = mesh.trianglePartIndices[index];
        if (partIndex >= parts.size())
        {
            throw std::invalid_argument("显示网格包含无效的部件索引。");
        }
        auto &part = parts[partIndex];
        auto &mapping = vertexMaps[partIndex];
        std::array<std::uint32_t, 3> triangle;
        for (std::size_t corner = 0; corner < triangle.size(); ++corner)
        {
            const auto sourceIndex = mesh.triangles[index][corner];
            if (sourceIndex >= mesh.vertices.size())
            {
                throw std::invalid_argument("显示网格包含无效的顶点索引。");
            }
            const auto [entry, inserted] =
                mapping.try_emplace(sourceIndex, static_cast<std::uint32_t>(part.vertices.size()));
            if (inserted)
            {
                const auto &point = mesh.vertices[sourceIndex];
                const Vec3 local{point.x - displayOrigin.x, point.y - displayOrigin.y, point.z - displayOrigin.z};
                if (!std::isfinite(local.x) || !std::isfinite(local.y) || !std::isfinite(local.z))
                {
                    throw std::invalid_argument("显示网格包含非有限坐标。");
                }
                if (part.vertices.empty())
                {
                    part.boundsMinMm = part.boundsMaxMm = point;
                }
                else
                {
                    part.boundsMinMm = {std::min(part.boundsMinMm.x, point.x),
                        std::min(part.boundsMinMm.y, point.y), std::min(part.boundsMinMm.z, point.z)};
                    part.boundsMaxMm = {std::max(part.boundsMaxMm.x, point.x),
                        std::max(part.boundsMaxMm.y, point.y), std::max(part.boundsMaxMm.z, point.z)};
                }
                part.vertices.push_back(local);
            }
            triangle[corner] = entry->second;
        }
        part.triangles.push_back(triangle);
        if (!mesh.triangleFaceIndices.empty())
        {
            part.triangleFaceIndices.push_back(mesh.triangleFaceIndices[index]);
        }
    }
    for (const auto &face : mesh.planarFaces)
    {
        if (face[0] >= parts.size()) throw std::invalid_argument("平面包含无效部件编号。");
        parts[face[0]].planarFaceIndices.push_back(face[1]);
    }
    for (const auto &keyPoint : mesh.keyPoints)
    {
        const auto &point = keyPoint.positionMm;
        const Vec3 local{point.x - displayOrigin.x, point.y - displayOrigin.y, point.z - displayOrigin.z};
        if (keyPoint.partIndex >= parts.size() || !std::isfinite(local.x) ||
            !std::isfinite(local.y) || !std::isfinite(local.z))
        {
            throw std::invalid_argument("模型关键点包含无效的部件索引或坐标。");
        }
        parts[keyPoint.partIndex].keyPoints.push_back(local);
    }
    for (const auto &edge : mesh.edges)
    {
        if (edge.partIndex >= parts.size() || edge.pointsMm.size() < 2)
        {
            throw std::invalid_argument("模型边包含无效的部件索引或折线。");
        }
        std::vector<Vec3> local;
        for (const auto &point : edge.pointsMm)
        {
            const Vec3 value{point.x - displayOrigin.x, point.y - displayOrigin.y, point.z - displayOrigin.z};
            if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.z))
            {
                throw std::invalid_argument("模型边包含非有限坐标。");
            }
            local.push_back(value);
        }
        parts[edge.partIndex].edges.push_back(std::move(local));
    }
    for (auto &part : parts)
    {
        if (part.vertices.empty())
        {
            throw std::invalid_argument("模型部件缺少显示网格。");
        }
        const auto observed = std::find_if(model.plates.begin(), model.plates.end(),
            [&](const auto &plate) { return plate.id == part.id; });
        const int normalAxis = observed != model.plates.end() && observed->uprightFootprint.empty()
            ? observed->normalAxis : 0;
        part.labelFaces = labelFaces(part, modelPartFrame(model, part.id), displayOrigin, normalAxis);
        const PlateThicknessInfo *anchor = nullptr;
        if (const auto info = model.thicknessOverrides.find(part.id); info != model.thicknessOverrides.end())
            anchor = &info->second;
        if (const auto info = model.importedThicknesses.find(part.id); info != model.importedThicknesses.end())
            anchor = &info->second;
        if (anchor)
        {
            const auto &value = *anchor;
            for (std::size_t side = 0; side < 2; ++side)
            {
                const double sign = side == 0 ? -1 : 1;
                auto &face = part.labelFaces[side];
                face.normal = {sign * value.normal.x, sign * value.normal.y, sign * value.normal.z};
                face.position = {value.centerMm.x - displayOrigin.x + face.normal.x * value.thicknessMm / 2,
                    value.centerMm.y - displayOrigin.y + face.normal.y * value.thicknessMm / 2,
                    value.centerMm.z - displayOrigin.z + face.normal.z * value.thicknessMm / 2};
            }
        }
    }
    return parts;
}
}
