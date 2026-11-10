/* 文件描述：同步模型编辑的快照隔离、提交版本检查、取消及可落盘诊断。 */
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "commands/ProjectOperations.h"
#include "geometry/Geometry.h"

namespace
{
using namespace reverse;

BeamParameters beamParameters()
{
    return {500, 120, 160, 8, 12, 0, {}};
}

class ProjectEditTransactions : public testing::Test
{
protected:
    void SetUp() override
    {
        (void)operations.submitReconstruct(beamParameters(), 0, BeamFrame{});
    }

    ProjectOperations operations;
    const PlateThicknessChange thickness{{"web"}, 10};
};

TEST_F(ProjectEditTransactions, PreviewDoesNotPublishOrCreateHistory)
{
    const auto before = operations.project();
    const auto preview = operations.previewThickness(thickness, before.revision);
    ASSERT_NE(preview, nullptr);
    EXPECT_NEAR(preview->summary().thicknessOverrides.at("web").thicknessMm, 10, 1e-8);
    const auto after = operations.project();
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.model, before.model);
    EXPECT_EQ(after.mesh, before.mesh);
    EXPECT_EQ(after.historySteps, before.historySteps);
}

TEST_F(ProjectEditTransactions, RevisionChangeDuringCalculationRejectsCommit)
{
    const auto before = operations.project();
    bool revised = false;
    rm::CommandContext context;
    context.progress = [&](double fraction, std::string_view) {
        if (fraction == 1.0 && !revised)
        {
            revised = true;
            PlaneSection section;
            section.offsetMm = 5;
            operations.setSection(section);
        }
    };
    operations.setContext(context);
    try
    {
        operations.applyThickness(thickness, before.revision);
        FAIL() << "An edit computed from an old revision must not be committed";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "stale_revision");
    }
    EXPECT_TRUE(revised);
    const auto after = operations.project();
    EXPECT_EQ(after.revision, before.revision + 1);
    EXPECT_EQ(after.section.offsetMm, 5);
    EXPECT_EQ(after.model, before.model);
    EXPECT_EQ(after.historySteps, before.historySteps);
}

TEST_F(ProjectEditTransactions, CancellationAtEndOfCalculationPreservesModel)
{
    const auto before = operations.project();
    std::stop_source cancellation;
    rm::CommandContext context;
    context.stop = cancellation.get_token();
    context.progress = [&](double fraction, std::string_view) {
        if (fraction == 1.0)
        {
            cancellation.request_stop();
        }
    };
    operations.setContext(context);
    try
    {
        operations.applyThickness(thickness, before.revision);
        FAIL() << "Cancelled geometry must not enter project history";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "cancelled");
    }
    EXPECT_TRUE(cancellation.stop_requested());
    EXPECT_EQ(operations.project().model, before.model);
    EXPECT_EQ(operations.project().revision, before.revision);
    EXPECT_EQ(operations.project().historySteps, before.historySteps);
}

TEST_F(ProjectEditTransactions, DiagnosticCanReadSnapshotAndCannotInvalidateCommit)
{
    const auto before = operations.project();
    std::string diagnostic;
    rm::CommandContext context;
    context.diagnostic = [&](std::string_view message) {
        diagnostic = message;
        EXPECT_EQ(operations.project().revision, before.revision + 1);
        throw std::runtime_error("Simulated diagnostic sink failure");
    };
    operations.setContext(context);
    EXPECT_NO_THROW(operations.applyThickness(thickness, before.revision));
    EXPECT_NE(diagnostic.find("model.commit operation=修改板厚"), std::string::npos);
    EXPECT_NEAR(operations.project().model->thicknessOverrides.at("web").thicknessMm, 10, 1e-8);
    EXPECT_EQ(operations.project().historySteps, before.historySteps + 1);
}

// 三个入口曾单独发布快照；逐个模拟计算回调更新工程，验证不会绕过版本保护。
TEST(ProjectCommitTransactions, RebuildTransformAndDeletionRejectStaleResults)
{
    for (const auto &kind : {"rebuild", "transform", "delete"})
    {
        SCOPED_TRACE(kind);
        ProjectOperations operations;
        (void)operations.submitReconstruct(beamParameters(), 0, BeamFrame{});
        const auto before = operations.project();
        bool revised = false;
        rm::CommandContext context;
        context.progress = [&](double fraction, std::string_view)
        {
            if (fraction == 1.0 && !revised)
            {
                revised = true;
                PlaneSection section;
                section.offsetMm = 5;
                operations.setSection(section);
            }
        };
        operations.setContext(context);
        try
        {
            if (std::string_view(kind) == "rebuild")
            {
                (void)operations.submitReconstruct(beamParameters(), before.revision, BeamFrame{});
            }
            else if (std::string_view(kind) == "transform")
            {
                ModelTransform move;
                move.offsetMm = {10, 0, 0};
                (void)operations.submitModelTransform({"web"}, move, before.revision);
            }
            else
            {
                (void)operations.submitDeleteModelParts({"web"}, before.revision);
            }
            FAIL() << "An old result must not overwrite the updated project";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "stale_revision");
        }
        EXPECT_TRUE(revised);
        EXPECT_EQ(operations.project().revision, before.revision + 1);
        EXPECT_EQ(operations.project().model, before.model);
        EXPECT_EQ(operations.project().historySteps, before.historySteps);
    }
}

TaskSnapshot finished(CommandService &service, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto task = service.task(id);
        if (task.finished())
        {
            return task;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("Project diagnostic test timed out");
}

std::filesystem::path testPath(const QTemporaryDir &directory)
{
#ifdef _WIN32
    return std::filesystem::path(directory.path().toStdWString());
#else
    return std::filesystem::path(directory.path().toStdString());
#endif
}

TEST(ProjectDiagnostics, FileContainsTaskStagesAndCommittedModelRevision)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = testPath(directory);
    CommandService service({{}, path});
    ASSERT_EQ(finished(service, service.submitReconstruct(beamParameters(), 0, BeamFrame{})).state,
        TaskState::Succeeded);
    const auto taskId = service.submitThickness({{"web"}, 10}, service.project().revision);
    ASSERT_EQ(finished(service, taskId).state, TaskState::Succeeded);
    std::ifstream file(path / "commands.log", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("task.progress id=" + taskId), std::string::npos);
    EXPECT_NE(log.find("task.diagnostic id=" + taskId + " model.commit operation=修改板厚"), std::string::npos);
    EXPECT_NE(log.find("revision=2 parts=3"), std::string::npos);
    EXPECT_NE(log.find("task.end id=" + taskId), std::string::npos);
}

TEST(ProjectDiagnostics, FailedEditKeepsSnapshotAndRecordsReason)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = testPath(directory);
    CommandService service({{}, path});
    ASSERT_EQ(finished(service, service.submitReconstruct(beamParameters(), 0, BeamFrame{})).state,
        TaskState::Succeeded);
    const auto before = service.project();
    const auto taskId = service.submitThickness({{"web"}, -1}, before.revision);
    const auto task = finished(service, taskId);
    ASSERT_EQ(task.state, TaskState::Failed);
    ASSERT_FALSE(task.errorCode.empty());
    EXPECT_EQ(service.project().revision, before.revision);
    EXPECT_EQ(service.project().model, before.model);
    EXPECT_EQ(service.project().historySteps, before.historySteps);
    std::ifstream file(path / "commands.log", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("task.end id=" + taskId + " code=" + task.errorCode), std::string::npos);
    EXPECT_NE(log.find(task.message), std::string::npos);
    EXPECT_EQ(log.find("task.diagnostic id=" + taskId + " model.commit"), std::string::npos);
}

class CloudTransactions : public testing::Test
{
protected:
    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        input = testPath(directory) / "transaction.pcd";
        std::ofstream file(input);
        file << "VERSION .7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
                "WIDTH 8\nHEIGHT 1\nPOINTS 8\nDATA ascii\n";
        for (int x : {0, 100})
        {
            for (int y : {-20, 20})
            {
                for (int z : {-30, 30})
                {
                    file << x << ' ' << y << ' ' << z << '\n';
                }
            }
        }
        file.close();
        (void)operations.submitImport(input);
    }

    void changeRevisionDuringCalculation()
    {
        rm::CommandContext context;
        context.progress = [this](double, std::string_view) {
            if (!revised)
            {
                revised = true;
                PlaneSection section;
                section.offsetMm = 7;
                operations.setSection(section);
            }
        };
        operations.setContext(context);
    }

    void expectOnlySectionChanged(const ProjectSnapshot &before)
    {
        EXPECT_TRUE(revised);
        const auto after = operations.project();
        EXPECT_EQ(after.revision, before.revision + 1);
        EXPECT_EQ(after.section.offsetMm, 7);
        EXPECT_EQ(after.cloud, before.cloud);
        EXPECT_EQ(after.cloudId, before.cloudId);
        EXPECT_EQ(after.model, before.model);
        EXPECT_EQ(after.modelId, before.modelId);
        EXPECT_EQ(after.selection, before.selection);
        EXPECT_EQ(after.selectionRecipe, before.selectionRecipe);
        EXPECT_EQ(after.deviation, before.deviation);
        EXPECT_EQ(after.historySteps, before.historySteps);
    }

    template <typename Operation> void expectStale(Operation operation)
    {
        const auto before = operations.project();
        changeRevisionDuringCalculation();
        try
        {
            operation(before.revision);
            FAIL() << "An obsolete calculation must not replace current project state";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "stale_revision");
        }
        expectOnlySectionChanged(before);
    }

    QTemporaryDir directory;
    std::filesystem::path input;
    ProjectOperations operations;
    bool revised{};
};

TEST_F(CloudTransactions, ImportDoesNotReplaceNewerProjectOrHistory)
{
    expectStale([&](std::uint64_t) { (void)operations.submitImport(input); });
}

TEST_F(CloudTransactions, CadImportDoesNotReplaceNewerProjectOrHistory)
{
    const auto cad = testPath(directory) / "transaction.step";
    const auto model = buildBeam(BeamFrame{}, beamParameters());
    (void)exportStep(*model, cad);
    expectStale([&](std::uint64_t) { (void)operations.submitImport(cad); });
}

TEST_F(CloudTransactions, RectangleSelectionDoesNotPublishStaleIndices)
{
    CloudSelectionRect rectangle;
    rectangle.viewProjection = {0.01, 0, 0, 0, 0, 0.01, 0, 0, 0, 0, 0.01, 0, 0, 0, 0, 1};
    rectangle.minX = rectangle.minY = -1;
    rectangle.maxX = rectangle.maxY = 1;
    expectStale([&](std::uint64_t revision) {
        (void)operations.submitCloudSelection(rectangle, CloudSelectionMode::Replace, revision);
    });
}

TEST_F(CloudTransactions, CloudEditDoesNotReplaceNewerCloudOrSelection)
{
    operations.selectIndices(rm::Array{rm::Value(std::uint64_t{0})}, CloudSelectionMode::Replace);
    expectStale([&](std::uint64_t revision) {
        (void)operations.submitCloudEdit(CloudEditMode::DeleteSelection, revision);
    });
}

TEST_F(CloudTransactions, DeviationDoesNotPublishReportForObsoleteRevision)
{
    (void)operations.submitReconstruct(beamParameters(), operations.project().revision, BeamFrame{});
    expectStale([&](std::uint64_t revision) { (void)operations.submitDeviation(revision); });
}

TEST_F(CloudTransactions, SelectionDiagnosticCanReadCommittedSnapshotAndThrow)
{
    bool reported = false;
    rm::CommandContext context;
    context.diagnostic = [&](std::string_view message) {
        reported = true;
        EXPECT_NE(message.find("cloud.selection"), std::string_view::npos);
        EXPECT_EQ(operations.project().selection->size(), 1u);
        throw std::runtime_error("diagnostic sink unavailable");
    };
    operations.setContext(context);
    EXPECT_NO_THROW(operations.selectIndices(rm::Array{rm::Value(std::uint64_t{0})}, CloudSelectionMode::Replace));
    EXPECT_TRUE(reported);
    EXPECT_EQ(operations.project().selection->size(), 1u);
}

TEST_F(CloudTransactions, ImportAndCloudEditsWriteCommitDetailsToFile)
{
    const auto logDirectory = testPath(directory) / "logs";
    CommandService service({{}, logDirectory});
    ASSERT_EQ(finished(service, service.submitImport(input)).state, TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitLine("cloud.select_indices --indices [0] --mode replace")).state,
              TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitCloudEdit(CloudEditMode::DeleteSelection, service.project().revision)).state,
              TaskState::Succeeded);
    std::ifstream file(logDirectory / "commands.log", std::ios::binary);
    ASSERT_TRUE(file.is_open());
    const std::string log((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    EXPECT_NE(log.find("project.import operation=导入点云 revision=1 points=8"), std::string::npos);
    EXPECT_NE(log.find("cloud.selection operation=选择点云 revision=2 selected=1"), std::string::npos);
    EXPECT_NE(log.find("cloud.commit operation=删除点云 revision=3 points=7"), std::string::npos);
}

enum class InterruptedCloudOperation
{
    Import,
    CadImport,
    Selection,
    Edit,
    Deviation
};

class CloudCancellation : public CloudTransactions, public testing::WithParamInterface<InterruptedCloudOperation>
{
};

TEST_P(CloudCancellation, InFlightCancellationPreservesGeometrySelectionAndRedo)
{
    (void)operations.submitReconstruct(beamParameters(), operations.project().revision, BeamFrame{});
    operations.applyThickness({{"web"}, 10}, operations.project().revision);
    (void)operations.submitCloudHistory(CloudHistoryAction::Undo, operations.project().revision);
    operations.selectIndices(rm::Array{rm::Value(std::uint64_t{0})}, CloudSelectionMode::Replace);
    const auto before = operations.project();
    ASSERT_TRUE(before.canRedo);
    const auto cad = testPath(directory) / "cancel.step";
    if (GetParam() == InterruptedCloudOperation::CadImport)
    {
        (void)exportStep(*buildBeam(BeamFrame{}, beamParameters()), cad);
    }
    std::stop_source cancellation;
    bool interrupted = false;
    int commitMessages = 0;
    rm::CommandContext context;
    context.stop = cancellation.get_token();
    context.progress = [&](double fraction, std::string_view)
    {
        if (fraction >= 0.1)
        {
            interrupted = true;
            cancellation.request_stop();
        }
    };
    context.diagnostic = [&](std::string_view) { ++commitMessages; };
    operations.setContext(context);
    try
    {
        switch (GetParam())
        {
        case InterruptedCloudOperation::Import:
            (void)operations.submitImport(input);
            break;
        case InterruptedCloudOperation::CadImport:
            (void)operations.submitImport(cad);
            break;
        case InterruptedCloudOperation::Selection:
        {
            CloudSelectionRect rectangle;
            rectangle.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
            (void)operations.submitCloudSelection(rectangle, CloudSelectionMode::Replace, before.revision);
            break;
        }
        case InterruptedCloudOperation::Edit:
            (void)operations.submitCloudEdit(CloudEditMode::DeleteSelection, before.revision);
            break;
        case InterruptedCloudOperation::Deviation:
            (void)operations.submitDeviation(before.revision);
            break;
        }
        FAIL() << "Cancelled work must not be committed";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "cancelled");
    }
    EXPECT_TRUE(interrupted);
    EXPECT_EQ(commitMessages, 0);
    const auto after = operations.project();
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.cloud, before.cloud);
    EXPECT_EQ(after.cloudId, before.cloudId);
    EXPECT_EQ(after.model, before.model);
    EXPECT_EQ(after.modelId, before.modelId);
    EXPECT_EQ(after.mesh, before.mesh);
    EXPECT_EQ(after.selection, before.selection);
    EXPECT_EQ(after.selectionRecipe, before.selectionRecipe);
    EXPECT_EQ(after.deviation, before.deviation);
    EXPECT_EQ(after.historySteps, before.historySteps);
    EXPECT_EQ(after.canRedo, before.canRedo);
    EXPECT_EQ(after.redoLabel, before.redoLabel);
    operations.setContext({});
    EXPECT_NO_THROW((void)operations.submitCloudHistory(CloudHistoryAction::Redo, after.revision));
    EXPECT_NEAR(operations.project().model->thicknessOverrides.at("web").thicknessMm, 10, 1e-8);
}

INSTANTIATE_TEST_SUITE_P(AllOperations, CloudCancellation,
                         testing::Values(InterruptedCloudOperation::Import, InterruptedCloudOperation::CadImport,
                                         InterruptedCloudOperation::Selection, InterruptedCloudOperation::Edit,
                                         InterruptedCloudOperation::Deviation),
                         [](const testing::TestParamInfo<InterruptedCloudOperation> &info)
                         {
                             switch (info.param)
                             {
                             case InterruptedCloudOperation::Import:
                                 return "PcdImport";
                             case InterruptedCloudOperation::CadImport:
                                 return "CadImport";
                             case InterruptedCloudOperation::Selection:
                                 return "Selection";
                             case InterruptedCloudOperation::Edit:
                                 return "Edit";
                             case InterruptedCloudOperation::Deviation:
                                 return "Deviation";
                             }
                             return "Unknown";
                         });

TEST_F(CloudTransactions, CancelledIndexSelectionPreservesAllSelectionModes)
{
    operations.selectIndices(rm::Array{rm::Value(std::uint64_t{0})}, CloudSelectionMode::Replace);
    const auto before = operations.project();
    std::stop_source cancellation;
    cancellation.request_stop();
    rm::CommandContext context;
    context.stop = cancellation.get_token();
    operations.setContext(context);
    for (const auto mode :
         {CloudSelectionMode::Replace, CloudSelectionMode::Add, CloudSelectionMode::Remove, CloudSelectionMode::Clear})
    {
        SCOPED_TRACE(static_cast<int>(mode));
        try
        {
            operations.selectIndices(rm::Array{rm::Value(std::uint64_t{1})}, mode);
            FAIL() << "A cancelled selection must not change selected indices";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "cancelled");
        }
        EXPECT_EQ(operations.project().revision, before.revision);
        EXPECT_EQ(operations.project().selection, before.selection);
        EXPECT_EQ(operations.project().selectionRecipe, before.selectionRecipe);
    }
}
} // namespace
