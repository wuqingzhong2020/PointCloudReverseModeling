/*
 * 文件描述：
 * 定义桌面模型的分部件网格、灰色实体及选择配色和板厚标注数据，不依赖 Qt 或 OCCT 显示类型。
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

namespace reverse::workbench
{
namespace ModelColors
{
inline constexpr std::array<double, 3> surface{0.86, 0.86, 0.86};
inline constexpr std::array<double, 3> outline{0.12, 0.13, 0.14};
inline constexpr std::array<double, 3> selectedSurface{0.62, 0.83, 0.87};
inline constexpr std::array<double, 3> selectedOutline{0.0, 0.48, 1.0};
inline constexpr std::array<double, 3> hoverSurface{0.79, 0.85, 0.87};
inline constexpr std::array<double, 3> hoverOutline{0.28, 0.55, 0.68};
}

enum class ModelPickMode { Body = 0, Vertex = 1, Edge = 2, Face = 3 };

struct ModelSelection
{
    std::string partId;
    ModelPickMode kind{ModelPickMode::Body};
    std::size_t index{}; // 部件内拓扑对象序号，界面显示时加一。
    bool operator==(const ModelSelection &) const = default;
};

// 单击替换，工具栏加/减选择；Shift 或 Ctrl 点击切换成员，点击空白时保留多选。
[[nodiscard]] std::vector<ModelSelection> updateModelSelection(std::vector<ModelSelection> selected,
    const ModelSelection &clicked, CloudSelectionMode mode, bool toggle = false);

[[nodiscard]] std::vector<std::string> updateModelPartSelection(std::vector<std::string> selected,
    const std::string &clicked, CloudSelectionMode mode, bool toggle = false);

// 将显示边编号还原为 BRep 边编号，按部件分组；过期或非边选择必须整批拒绝。
[[nodiscard]] std::vector<EdgeTreatmentTarget> edgeTreatmentTargets(
    const MeshData &mesh, const std::vector<ModelSelection> &selections);

struct ModelLabelAnchor
{
    Vec3 position;
    Vec3 normal; // 板件主表面的外法线。
};

struct ModelDisplayPart
{
    std::string id;
    std::vector<Vec3> vertices; // 已减去显示原点，单位 mm。
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<Vec3> keyPoints; // 真实拓扑顶点，坐标与显示网格一致，用于测量捕获。
    std::vector<std::vector<Vec3>> edges;
    std::vector<std::uint32_t> triangleFaceIndices;
    std::vector<std::uint32_t> planarFaceIndices;
    std::array<double, 3> colorSrgb;
    Vec3 boundsMinMm; // 部件三角网格的项目坐标包围盒，不减显示原点。
    Vec3 boundsMaxMm;
    std::optional<double> thicknessMm;
    bool thicknessUnresolved{};
    bool thicknessEstimated{}; // 由其他板件推算；保留来源供属性查看，三维标签统一只显示厚度。
    std::array<ModelLabelAnchor, 2> labelFaces; // 主表面两侧，每帧最多显示一侧。
};

[[nodiscard]] bool isValidModelSelection(const ModelDisplayPart &part, const ModelSelection &selection);

// 网格和摘要必须属于同一次建模；部件归属或索引不一致时抛出 std::invalid_argument。
// 板厚只取自已生成模型的参数，未知部件不猜测板厚。
[[nodiscard]] std::vector<ModelDisplayPart> makeModelDisplayParts(
    const MeshData &mesh, const ModelSummary &model, const Vec3 &displayOrigin);
}
