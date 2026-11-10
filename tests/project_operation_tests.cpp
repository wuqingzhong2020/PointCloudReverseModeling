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
} // namespace
