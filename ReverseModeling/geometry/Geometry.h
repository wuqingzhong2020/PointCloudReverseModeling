/*
 * 文件描述：
 * 声明点云导入、辅助 H 型钢建模、STEP 交换及有限板件偏差服务，隐藏第三方对象。
 */
#pragma once

#include "core/Types.h"
#include "geometry/ModelTransform.h"

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

struct PlateEdit;
struct PlateEditResult;
class Model {
public:
    struct Impl;
    explicit Model(std::shared_ptr<const Impl> implementation);
    [[nodiscard]] const ModelSummary& summary() const noexcept;
    [[nodiscard]] const MeshData& mesh() const noexcept;
private:
    std::shared_ptr<const Impl> implementation_;
    friend PlateEditResult editModelPlates(const Model&, PlateEdit, std::stop_token, const ProgressCallback&);
    friend PlateEdit prepareDirectPlateEdit(const Model&, PlateEdit, bool, std::stop_token, const std::function<void(std::string_view)>&);
    friend std::shared_ptr<const Model> reconstructModelObstacles(const Model&, const CloudData&, const PlateRecognition&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> associateModelCloud(const Model&, const CloudData&, std::stop_token);
    friend std::shared_ptr<const Model> reconstructLocalParts(const Model&, const CloudData&,
        const LocalReconstruction&, std::stop_token, const ProgressCallback&);
    friend StepExportResult exportCwrm(const Model&, const std::filesystem::path&, std::stop_token, const ProgressCallback&);
    friend StepExportResult exportStep(const Model&, const std::filesystem::path&,
                                      std::stop_token, const ProgressCallback&);
    friend DeviationResult evaluateDeviation(const CloudData&, const Model&,
                                             std::stop_token, const ProgressCallback&);
    friend std::map<std::string, PlateThicknessInfo> inspectModelThickness(
        const Model&, const std::vector<std::string>&, std::stop_token);
    friend std::vector<std::string> connectedModelParts(const Model&, const std::string&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> completeModelMirror(
        const Model&, const MirrorCompletion&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> generateModelPlate(
        const Model&, const PlateGeneration&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> expandModelContours(
        const Model&, const PlateContourExpansion&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> changeModelThickness(
        const Model&, const PlateThicknessChange&, std::stop_token, const ProgressCallback&);
    friend std::vector<PlateHoleBase> inspectModelHoleBases(const Model&, const std::string&);
    friend std::shared_ptr<const Model> addModelHole(const Model&, const PlateHole&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> treatModelEdges(const Model&, const EdgeTreatment&,
        std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> transformModelParts(const Model&, const std::vector<std::string>&,
        const ModelTransform&, std::stop_token, const ProgressCallback&);
    friend std::shared_ptr<const Model> deleteModelParts(const Model&, const std::vector<std::string>&,
                                                       std::stop_token, const ProgressCallback&);
};

[[nodiscard]] std::shared_ptr<const CloudData> loadPointCloud(
    const std::filesystem::path& path, LengthUnit unit,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

[[nodiscard]] std::shared_ptr<const Model> loadCadModel(
    const std::filesystem::path& path, std::stop_token stop = {}, const ProgressCallback& progress = {});

[[nodiscard]] CloudExportResult exportPointCloud(
    const CloudData& cloud, const std::filesystem::path& path,
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

// 按稳定部件 ID 删除真实几何；保留部件的 ID/坐标/板厚不变，全部删除返回 nullptr。
[[nodiscard]] std::shared_ptr<const Model> deleteModelParts(
    const Model &model, const std::vector<std::string> &partIds,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::shared_ptr<const Model> transformModelParts(
    const Model &model, const std::vector<std::string> &partIds, const ModelTransform &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::shared_ptr<const Model> treatModelEdges(
    const Model &model, const EdgeTreatment &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::vector<PlateHoleBase> inspectModelHoleBases(const Model &model, const std::string &partId);
[[nodiscard]] std::shared_ptr<const Model> addModelHole(const Model &model, const PlateHole &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

[[nodiscard]] std::map<std::string, PlateThicknessInfo> inspectModelThickness(
    const Model &model, const std::vector<std::string> &partIds, std::stop_token stop = {});
// 从一个部件查找真实接触/相交的连通组，含种子；接触容差 0.15 mm，返回稳定部件顺序。
[[nodiscard]] std::vector<std::string> connectedModelParts(
    const Model &model, const std::string &seedPartId,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
[[nodiscard]] std::shared_ptr<const Model> completeModelMirror(
    const Model &model, const MirrorCompletion &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
[[nodiscard]] std::shared_ptr<const Model> generateModelPlate(
    const Model &model, const PlateGeneration &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
[[nodiscard]] std::shared_ptr<const Model> expandModelContours(
    const Model &model, const PlateContourExpansion &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
[[nodiscard]] std::shared_ptr<const Model> changeModelThickness(
    const Model &model, const PlateThicknessChange &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

// 将工件以外的有效扫描簇附加为独立避障实体，不生成工件板件或板厚。
[[nodiscard]] std::shared_ptr<const Model> reconstructModelObstacles(
    const Model &model, const CloudData &cloud, const PlateRecognition &recognition,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
[[nodiscard]] std::shared_ptr<const Model> associateModelCloud(const Model &model, const CloudData &cloud,
    std::stop_token stop = {});
[[nodiscard]] std::shared_ptr<const Model> reconstructLocalParts(const Model &model, const CloudData &cloud,
    const LocalReconstruction &request, std::stop_token stop = {}, const ProgressCallback &progress = {});

// 默认拒绝覆盖已有文件；临时写出、独立回读及几何检查通过后才提交目标文件。
[[nodiscard]] StepExportResult exportStep(
    const Model& model, const std::filesystem::path& path,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

// 当前仅适用于本服务生成的矩形板件表面，不等于任意 B-Rep 或通用网格距离服务。
[[nodiscard]] DeviationResult evaluateDeviation(
    const CloudData& cloud, const Model& model,
    std::stop_token stop = {}, const ProgressCallback& progress = {});

} // namespace reverse
