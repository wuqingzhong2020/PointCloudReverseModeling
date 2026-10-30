/*
 * 文件描述：
 * 声明点云导入、辅助 H 型钢建模、STEP 交换及有限板件偏差服务，隐藏第三方对象。
 */
#pragma once

#include "core/Types.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <stop_token>
#include <string_view>
#include <utility>

namespace reverse {

using ProgressCallback = std::function<void(double, std::string_view)>;

class GeometryError : public std::runtime_error {
public:
    GeometryError(std::string code, std::string message)
        : std::runtime_error(std::move(message)), code_(std::move(code)) {}
    [[nodiscard]] const std::string& code() const noexcept { return code_; }
private:
    std::string code_;
};

class Model {
public:
    struct Impl;
    explicit Model(std::shared_ptr<const Impl> implementation);
    [[nodiscard]] const ModelSummary& summary() const noexcept;
    [[nodiscard]] const MeshData& mesh() const noexcept;
private:
    std::shared_ptr<const Impl> implementation_;
    friend StepExportResult exportStep(const Model&, const std::filesystem::path&,
                                      std::stop_token, const ProgressCallback&);
    friend DeviationResult evaluateDeviation(const CloudData&, const Model&,
                                             std::stop_token, const ProgressCallback&);
};

[[nodiscard]] std::shared_ptr<const CloudData> loadPointCloud(
    const std::filesystem::path& path, LengthUnit unit,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

// PCA 只给出框架和包围尺寸初值；不识别板厚、肋板、孔洞或遮挡背面。
[[nodiscard]] BeamEstimate estimateBeam(
    const CloudData& cloud, std::stop_token stop = {}, const ProgressCallback& progress = {});

// 识别单侧板件；底板取两侧最大板厚并夹在翼板内侧，翼板下沿贴合底板底面。
[[nodiscard]] PlateRecognition recognizePlates(
    const CloudData &cloud, const BeamEstimate &estimate,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::shared_ptr<const Model> buildRecognizedPlates(
    const PlateRecognition &recognition, const PlateBuildOptions &options = {},
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::shared_ptr<const Model> buildBeam(
    const BeamFrame& frame, const BeamParameters& parameters,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

// 默认拒绝覆盖已有文件；临时写出、独立回读及几何检查通过后才提交目标文件。
[[nodiscard]] StepExportResult exportStep(
    const Model& model, const std::filesystem::path& path,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

// 当前仅适用于本服务生成的矩形板件表面，不等于任意 B-Rep 或通用网格距离服务。
[[nodiscard]] DeviationResult evaluateDeviation(
    const CloudData& cloud, const Model& model,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

} // namespace reverse
