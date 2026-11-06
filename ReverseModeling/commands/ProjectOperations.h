/* 文件描述：同步产品业务，与命令描述和参数生成解耦。 */
#pragma once
#include "geometry/PlateEditing.h"
#include "commandManager/CommandManager.h"
#include "commands/CommandService.h"
namespace reverse
{
class TaskOutput;
class ProjectOperations
{
  public:
    ProjectOperations();
    ~ProjectOperations(); // 请求取消并等待工作线程结束；工作线程不访问 UI。
    ProjectOperations(const ProjectOperations &) = delete;
    ProjectOperations &operator=(const ProjectOperations &) = delete;

    [[nodiscard]] PlateEditResult previewPlateEdit(const PlateEdit &request, std::uint64_t revision);
    void applyPlateEdit(const PlateEdit &request, std::uint64_t revision);
    [[nodiscard]] std::string submitImport(const std::filesystem::path &path, LengthUnit unit);
    [[nodiscard]] std::string submitCloudSelection(const CloudSelectionRect &rectangle, CloudSelectionMode mode,
                                                   std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitCloudEdit(CloudEditMode mode, std::uint64_t expectedRevision,
                                             std::optional<CloudSelectionRecipe> selectionOverride = {});
    [[nodiscard]] std::string submitCloudHistory(CloudHistoryAction action, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitReconstruct(const BeamParameters &parameters, std::uint64_t expectedRevision,
                                                std::optional<BeamFrame> frameOverride = {});
    [[nodiscard]] std::string submitRecognizedReconstruct(bool mirrorSymmetry, std::uint64_t expectedRevision,
                                                          double endOverhangToleranceMm = 30.0);
    [[nodiscard]] std::shared_ptr<const Model> previewLocalReconstruction(const LocalReconstruction &request, std::uint64_t revision);
    void applyLocalReconstruction(const LocalReconstruction &request, std::uint64_t revision);
    [[nodiscard]] std::string submitExport(const std::filesystem::path &path, std::uint64_t expectedRevision);
    void exportCwrmModel(const std::filesystem::path &path, std::uint64_t revision);
    void exportSampledModel(const std::filesystem::path &path, double spacing, std::uint64_t revision);
    void convertFile(const std::filesystem::path &input, const std::filesystem::path &output, double spacing);
    [[nodiscard]] std::string submitExportCloud(const std::filesystem::path &path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitDeviation(std::uint64_t expectedRevision);
    [[nodiscard]] std::shared_ptr<const Model> previewEdgeTreatment(const EdgeTreatment &request, std::uint64_t revision);
    [[nodiscard]] std::map<std::string, PlateThicknessInfo> inspectThickness(
        const std::vector<std::string> &parts, std::uint64_t revision);
    [[nodiscard]] std::shared_ptr<const Model> previewThickness(const PlateThicknessChange &request, std::uint64_t revision);
    void applyThickness(const PlateThicknessChange &request, std::uint64_t revision);
    [[nodiscard]] std::shared_ptr<const Model> previewMirrorCompletion(const MirrorCompletion &request, std::uint64_t revision);
    void applyMirrorCompletion(const MirrorCompletion &request, std::uint64_t revision);
    [[nodiscard]] std::shared_ptr<const Model> previewPlateGeneration(const PlateGeneration &request, std::uint64_t revision);
    void applyPlateGeneration(const PlateGeneration &request, std::uint64_t revision);
    [[nodiscard]] std::shared_ptr<const Model> previewContourExpansion(const PlateContourExpansion &request, std::uint64_t revision);
    void applyContourExpansion(const PlateContourExpansion &request, std::uint64_t revision);
    [[nodiscard]] std::vector<PlateHoleBase> inspectHoleBases(const std::string &partId, std::uint64_t revision);
    [[nodiscard]] std::shared_ptr<const Model> previewHole(const PlateHole &request, std::uint64_t revision);
    void applyHole(const PlateHole &request, std::uint64_t revision);
    void applyEdgeTreatment(const EdgeTreatment &request, std::uint64_t revision);
    [[nodiscard]] std::string submitModelTransform(const std::vector<std::string> &partIds,
        const ModelTransform &request, std::uint64_t revision);
    [[nodiscard]] std::string submitDeleteModelParts(const std::vector<std::string> &partIds, std::uint64_t revision);
    void exportTaskOutput(TaskOutput &output, std::uint64_t expectedRevision);
    void setReferencePlane(std::optional<std::array<Vec3, 3>> points);
    void setSection(PlaneSection section);
    void setContext(const rm::CommandContext &context);
    void selectIndices(const rm::Array &indices, CloudSelectionMode mode);
    [[nodiscard]] ProjectSnapshot project() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverse
