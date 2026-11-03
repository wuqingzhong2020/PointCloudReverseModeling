/* 文件描述：同步产品业务，与命令描述和参数生成解耦。 */
#pragma once
#include "commandManager/CommandManager.h"
#include "commands/CommandService.h"
namespace reverse
{
class ProjectOperations
{
  public:
    ProjectOperations();
    ~ProjectOperations(); // 请求取消并等待工作线程结束；工作线程不访问 UI。
    ProjectOperations(const ProjectOperations &) = delete;
    ProjectOperations &operator=(const ProjectOperations &) = delete;

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
    [[nodiscard]] std::string submitExport(const std::filesystem::path &path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitExportCloud(const std::filesystem::path &path, std::uint64_t expectedRevision);
    [[nodiscard]] std::string submitDeviation(std::uint64_t expectedRevision);
    void setContext(const rm::CommandContext &context);
    void selectIndices(const rm::Array &indices, CloudSelectionMode mode);
    [[nodiscard]] ProjectSnapshot project() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace reverse
