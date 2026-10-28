/*
 * 文件描述：
 * 定义不依赖界面或第三方几何类型的点云、辅助建模、显示和质量数据契约。
 */
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace reverse {

enum class LengthUnit { Millimeter, Centimeter, Meter };

struct Vec3 {
    double x{};
    double y{};
    double z{};
};

struct PointSample {
    Vec3 position; // 局部坐标，单位 mm；项目坐标 = position + CloudData::originMm。
    double intensity{};
    std::uint64_t sourceIndex{};
};

struct CloudData {
    std::vector<PointSample> points;
    std::vector<std::string> fields;
    std::uint64_t sourcePointCount{};
    std::uint64_t invalidPointCount{};
    bool hasIntensity{};
    LengthUnit sourceUnit{LengthUnit::Millimeter};
    double sourceScaleToMm{1.0};
    Vec3 originMm;
    Vec3 boundsMinMm; // 相对于 originMm。
    Vec3 boundsMaxMm;
};

struct BeamFrame {
    Vec3 originMm; // 梁中心的项目坐标，单位 mm。
    Vec3 xAxis{1.0, 0.0, 0.0}; // 梁长方向。
    Vec3 yAxis{0.0, 1.0, 0.0}; // 翼板宽度方向。
    Vec3 zAxis{0.0, 0.0, 1.0}; // 截面高度方向。
};

struct BeamEstimate {
    BeamFrame frame;
    double length{};
    double width{};
    double height{};
    std::vector<std::string> warnings;
};

struct BeamParameters {
    double length{};
    double width{};
    double height{};
    double webThickness{};
    double flangeThickness{};
    double ribThickness{};
    std::vector<double> ribStations; // 距梁起点的距离，每站在腹板两侧各生成一块肋板。
    bool swapCrossSection{}; // 交换框架的 y/z 方向；尺寸仍使用用户提交值。
};

struct BeamParameterSuggestion {
    std::string profile{"reference-h-beam-v1"};
    BeamParameters parameters;
    std::vector<std::string> notes; // 参考板厚/站位的假设说明，不代表测量或自动识别结果。
};

struct MeshData {
    std::vector<Vec3> vertices; // 项目坐标，单位 mm；视图可减去局部原点。
    std::vector<std::array<std::uint32_t, 3>> triangles;
    std::vector<std::uint32_t> trianglePartIndices;
    std::vector<std::string> partIds;
};

struct ModelSummary {
    BeamFrame frame;
    BeamParameters parameters;
    std::uint64_t solidCount{};
    double volumeMm3{};
    std::vector<std::string> partIds;
};

struct StepExportResult {
    std::uint64_t solidCount{};
    double volumeMm3{};
    std::uint64_t fileBytes{};
    bool valid{};
};

struct DeviationResult {
    std::string method{"analytic_finite_plate_surface"};
    std::uint64_t pointCount{};
    double meanMm{};
    double rmsMm{};
    double p95Mm{};
    double maximumMm{};
    std::vector<double> distancesMm; // 与 CloudData::points 顺序一致，无显示采样。
    std::vector<std::uint32_t> nearestPartIndices;
};

} // namespace reverse
