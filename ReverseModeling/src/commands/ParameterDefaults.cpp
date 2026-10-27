/*
 * 文件描述：
 * 按包围尺寸生成明确标识的参考板厚与肋板位置，并对小尺寸等比缩放以避免几何冲突。
 * 本策略不识别真实板厚、肋板或截面朝向，不提供结构设计尺寸。
 */
#include "reverse/ParameterDefaults.h"

#include <algorithm>
#include <cmath>

namespace reverse {
namespace {
double rounded(double value) { return std::round(value * 10000.0) / 10000.0; }
}

std::optional<BeamParameterSuggestion> suggestBeamParameters(const BeamEstimate& estimate) {
    for (const auto value : {estimate.length, estimate.width, estimate.height})
        if (!std::isfinite(value) || value < 0.01 || value > 1e9) return {};

    BeamParameterSuggestion result;
    auto& parameters = result.parameters;
    parameters.length = rounded(estimate.length);
    parameters.swapCrossSection = estimate.width > estimate.height;
    parameters.width = rounded(std::min(estimate.width, estimate.height));
    parameters.height = rounded(std::max(estimate.width, estimate.height));

    // 普通尺寸使用 8/12/8 mm；小尺寸共同缩放，保证板间空隙和两站肋板间距。
    const double scale = std::min({1.0, parameters.width / 32.0,
                                  parameters.height / 48.0, parameters.length / 48.0});
    parameters.webThickness = rounded(8.0 * scale);
    parameters.flangeThickness = rounded(12.0 * scale);
    parameters.ribThickness = rounded(8.0 * scale);
    parameters.ribStations = {rounded(parameters.length / 3.0), rounded(parameters.length * (2.0 / 3.0))};

    // 检查最终呈现值，防止显示舍入后变成零板厚或无效站位。
    if (parameters.webThickness <= 1e-5 || parameters.flangeThickness <= 1e-5 ||
        parameters.ribThickness <= 1e-5 || parameters.webThickness >= parameters.width ||
        parameters.flangeThickness * 2.0 >= parameters.height ||
        parameters.ribStations.front() < parameters.ribThickness / 2.0 ||
        parameters.ribStations.back() > parameters.length - parameters.ribThickness / 2.0 ||
        parameters.ribStations.back() - parameters.ribStations.front() < parameters.ribThickness + 1e-6)
        return {};

    result.notes.emplace_back("板厚采用初始参考值：腹板 8 mm、翼板 12 mm、肋板 8 mm；可按实际情况修改。");
    result.notes.emplace_back("参考肋板位置为梁长的 1/3 和 2/3，每处两块；不是点云识别出的肋板位置。");
    result.notes.emplace_back("初始方向按截面高度不小于翼板宽度选择；方向、尺寸和缺测结构仍需结合点云核对。");
    if (scale < 1.0)
        result.notes.emplace_back("当前尺寸较小，参考板厚已按同一比例缩小，以避免板厚与截面或肋板间距冲突。");
    return result;
}
} // namespace reverse
