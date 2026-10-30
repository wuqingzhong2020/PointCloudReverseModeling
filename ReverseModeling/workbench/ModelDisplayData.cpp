/*
 * 文件描述：
 * 按三角形部件归属拆分显示网格，保留稳定部件 ID，并从建模快照取得板厚。
 * 生成与显示原点一致的标注锚点及非金色配色，不改变业务几何、STEP 或工程参数。
 */
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
namespace
{
std::array<double, 3> partColor(std::string_view id)
{
    constexpr std::array<unsigned int, 9> colors{
        0x80b8dc, 0xb4abc9, 0x8cc2bc, 0x8fc492, 0xd69d94, 0xb9a1d5, 0x92c9da, 0xd2abc5, 0x99b8c4};
    std::size_t index = 0;
    if (id.starts_with("imported-part-"))
    {
        unsigned int part = 1;
        std::from_chars(id.data() + 14, id.data() + id.size(), part);
        index = (part - 1) % colors.size();
    }
    else if (id == "flange-bottom")
    {
        index = 1;
    }
    else if (id == "flange-top")
    {
        index = 2;
    }
    else if (id != "web")
    {
        std::uint32_t hash = 2166136261u;
        for (const unsigned char character : id)
        {
            hash = (hash ^ character) * 16777619u;
        }
        index = 3 + hash % (colors.size() - 3);
        if (id.starts_with("rib-"))
        {
            unsigned int station = 0;
            const auto parsed = std::from_chars(id.data() + 4, id.data() + id.size(), station);
            const std::string_view side(parsed.ptr, static_cast<std::size_t>(id.data() + id.size() - parsed.ptr));
            if (parsed.ec == std::errc{} && station > 0 && (side == "-left" || side == "-right"))
            {
                index = 3 + ((static_cast<std::size_t>(station) - 1) * 2 + (side == "-right" ? 1 : 0))
                                % (colors.size() - 3);
            }
        }
    }
    const auto rgb = colors[index];
    return {((rgb >> 16) & 255) / 255.0, ((rgb >> 8) & 255) / 255.0, (rgb & 255) / 255.0};
}

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
    const ModelDisplayPart &part, const BeamFrame &frame, const Vec3 &displayOrigin)
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
    std::size_t normalAxis = 0;
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
    if (mesh.trianglePartIndices.size() != mesh.triangles.size() || mesh.partIds != model.partIds)
    {
        throw std::invalid_argument("显示网格的部件归属与模型快照不一致。");
    }
    std::vector<ModelDisplayPart> parts(mesh.partIds.size());
    std::vector<std::unordered_map<std::uint32_t, std::uint32_t>> vertexMaps(parts.size());
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        parts[index].id = mesh.partIds[index];
        parts[index].colorSrgb = partColor(parts[index].id);
        parts[index].thicknessMm = model.importedFormat.empty() ? partThickness(parts[index].id, model.parameters) : std::nullopt;
        if (model.fromPointCloud)
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
                part.vertices.push_back(local);
            }
            triangle[corner] = entry->second;
        }
        part.triangles.push_back(triangle);
    }
    for (auto &part : parts)
    {
        if (part.vertices.empty())
        {
            throw std::invalid_argument("模型部件缺少显示网格。");
        }
        part.labelFaces = labelFaces(part, model.frame, displayOrigin);
    }
    return parts;
}
}
