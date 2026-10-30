/*
 * 文件描述：
 * 定义桌面模型的分部件网格、稳定配色和板厚标注数据，不依赖 Qt 或 OCCT 显示类型。
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "core/Types.h"

namespace reverse::workbench
{
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
    std::array<double, 3> colorSrgb;
    Vec3 boundsMinMm; // 部件三角网格的项目坐标包围盒，不减显示原点。
    Vec3 boundsMaxMm;
    std::optional<double> thicknessMm;
    bool thicknessUnresolved{};
    bool thicknessEstimated{}; // 由其他板件推算，标签必须与本板识别厚度区分。
    std::array<ModelLabelAnchor, 2> labelFaces; // 主表面两侧，每帧最多显示一侧。
};

// 网格和摘要必须属于同一次建模；部件归属或索引不一致时抛出 std::invalid_argument。
// 板厚只取自已生成模型的参数，未知部件不猜测板厚。
[[nodiscard]] std::vector<ModelDisplayPart> makeModelDisplayParts(
    const MeshData &mesh, const ModelSummary &model, const Vec3 &displayOrigin);
}
