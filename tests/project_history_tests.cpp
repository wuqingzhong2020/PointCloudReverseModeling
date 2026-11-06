/* 文件描述：统一工程历史、精确几何恢复及独立录制回放的单元测试。 */
#include "commands/CommandService.h"
#include "commands/MacroRecorder.h"
#include "commands/ProjectOperations.h"
#include "commands/ReplayJournal.h"
#include "geometry/Geometry.h"
#include "CommandModules.h"
#include <gtest/gtest.h>
#include <chrono>
#include <fstream>
#include <thread>

namespace
{
using namespace reverse;
TaskSnapshot finish(CommandService &service, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto task = service.task(id);
        if (task.finished()) return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("History command timed out");
}
TaskSnapshot ok(CommandService &service, const std::string &id)
{
    auto task = finish(service, id);
    if (!task.result.ok()) throw std::runtime_error(task.errorCode + ": " + task.message);
    return task;
}
std::string read(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
class ProjectHistoryTest : public testing::Test
{
  protected:
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("rm-history-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path input = directory / std::filesystem::path(u8"中文 点云.pcd");
    BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    void SetUp() override
    {
        std::filesystem::create_directory(directory);
        std::ofstream file(input);
        file << "VERSION .7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 8\nHEIGHT 1\nPOINTS 8\nDATA ascii\n";
        for (int x : {0, 100}) for (int y : {-20, 20}) for (int z : {-30, 30})
            file << x << ' ' << y << ' ' << z << '\n';
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    void import(CommandService &s) { ok(s, s.submitImport(input)); }
    void build(CommandService &s) { ok(s, s.submitReconstruct(parameters, s.project().revision, BeamFrame{})); }
    void run(CommandService &s, const std::string &line) { ok(s, s.submitLine(line)); }
};

TEST_F(ProjectHistoryTest, MixedEditsRestoreExactGeometryParametersAndMonotonicRevision)
{
    CommandService s;
    import(s);
    const auto original = s.project();
    run(s, "cloud.crop --indices '[0,1,2,3,4,5]'");
    const auto cropped = s.project();
    build(s);
    const auto built = s.project();
    ok(s, s.submitDeviation(s.project().revision));
    ok(s, s.submitExport(directory / "before.step", s.project().revision));
    ok(s, s.submitDeleteModelParts({"web", "rib-1-right"}, s.project().revision));
    const auto deleted = s.project();
    ASSERT_EQ(deleted.model->solidCount, 5);
    run(s, "project.undo");
    EXPECT_EQ(s.project().model, built.model);
    EXPECT_EQ(s.project().mesh, built.mesh);
    EXPECT_EQ(s.project().modelId, built.modelId);
    ASSERT_TRUE(s.project().committedParameters);
    EXPECT_DOUBLE_EQ(s.project().committedParameters->webThickness, 4);
    EXPECT_FALSE(s.project().deviation);
    EXPECT_FALSE(s.project().lastExport);
    EXPECT_FALSE(s.project().selection);
    EXPECT_GT(s.project().revision, deleted.revision);
    ok(s, s.submitExport(directory / "restored.step", s.project().revision));
    const auto exported = loadCadModel(directory / "restored.step");
    EXPECT_EQ(exported->summary().solidCount, built.model->solidCount);
    EXPECT_NEAR(exported->summary().volumeMm3, built.model->volumeMm3, 1e-4);
    run(s, "project.undo");
    EXPECT_FALSE(s.project().model);
    EXPECT_EQ(s.project().cloud, cropped.cloud);
    run(s, "project.undo");
    EXPECT_EQ(s.project().cloud, original.cloud);
    EXPECT_FALSE(s.project().canUndo);
    EXPECT_FALSE(s.project().canReset);
    for (int i = 0; i < 3; ++i) run(s, "project.redo");
    EXPECT_EQ(s.project().model, deleted.model);
    EXPECT_EQ(s.project().cloud, cropped.cloud);
    EXPECT_EQ(s.project().model->partIds, deleted.model->partIds);
    EXPECT_TRUE(std::filesystem::exists(directory / "before.step"));
}

TEST_F(ProjectHistoryTest, ImportedCadAndDeleteAllResetRemainUndoableWithoutSourceFile)
{
    const auto model = buildBeam(BeamFrame{}, parameters);
    const auto path = directory / "import.step";
    (void)exportStep(*model, path);
    CommandService s;
    ok(s, s.submitImport(path));
    const auto baseline = s.project();
    ok(s, s.submitDeleteModelParts(baseline.model->partIds, baseline.revision));
    EXPECT_FALSE(s.project().model);
    EXPECT_TRUE(s.project().canReset);
    std::filesystem::rename(path, directory / "moved.step");
    run(s, "project.reset");
    EXPECT_EQ(s.project().model, baseline.model);
    EXPECT_EQ(s.project().mesh, baseline.mesh);
    EXPECT_FALSE(s.project().canReset);
    run(s, "project.undo");
    EXPECT_FALSE(s.project().model);
    run(s, "project.undo");
    EXPECT_EQ(s.project().model, baseline.model);
    const auto revision = s.project().revision;
    run(s, "project.reset"); // 已在基线的重置不得清掉后续重做。
    EXPECT_EQ(s.project().revision, revision);
    EXPECT_TRUE(s.project().canRedo);
    run(s, "project.redo");
    EXPECT_FALSE(s.project().model);
    run(s, "project.redo");
    ok(s, s.submitExport(directory / "roundtrip.step", s.project().revision));
    EXPECT_EQ(s.project().lastExport->solidCount, baseline.model->solidCount);
    EXPECT_EQ(finish(s, s.submitImport(path)).state, TaskState::Failed);
    EXPECT_TRUE(s.project().canUndo);
    import(s);
    EXPECT_FALSE(s.project().canUndo);
    EXPECT_FALSE(s.project().canRedo);
}

TEST_F(ProjectHistoryTest, NoOpSelectionQueryAndExportsPreserveRedoButEditsBranch)
{
    CommandService s;
    import(s);
    build(s);
    ok(s, s.submitDeleteModelParts({"web"}, s.project().revision));
    const auto reduced = s.project().model;
    run(s, "project.undo");
    run(s, "cloud.select_indices --indices '[0,1,2,3,4,5,6,7]'");
    run(s, "cloud.crop");
    run(s, "project.snapshot");
    ok(s, s.submitExportCloud(directory / "points.pcd", s.project().revision));
    ok(s, s.submitExport(directory / "still.step", s.project().revision));
    const auto revision = s.project().revision;
    EXPECT_EQ(finish(s, s.submitDeleteModelParts({"missing"}, revision)).state, TaskState::Failed);
    EXPECT_EQ(finish(s, s.submitProjectHistory(CloudHistoryAction::Undo, revision - 1)).errorCode, "stale_revision");
    EXPECT_TRUE(s.project().canRedo);
    run(s, "project.redo");
    EXPECT_EQ(s.project().model, reduced);
    run(s, "project.undo");
    ok(s, s.submitDeleteModelParts({"rib-1-right"}, s.project().revision));
    EXPECT_FALSE(s.project().canRedo);
    EXPECT_EQ(finish(s, s.submitLine("project.redo")).errorCode, "no_history");
}

TEST_F(ProjectHistoryTest, TenStepCapacitySharedArraysReferencePlaneAndResetBoundary)
{
    CommandService s;
    import(s);
    const auto cloud = s.project().cloud;
    for (int i = 0; i < 12; ++i)
        ok(s, s.submitReferencePlane(std::array<Vec3, 3>{{{0, 0, double(i)}, {10, 0, double(i)}, {0, 10, double(i)}}},
                                    s.project().revision));
    EXPECT_EQ(s.project().historySteps, 10);
    EXPECT_EQ(s.project().historyArrayBytes, cloud->points.capacity() * sizeof(PointSample));
    ok(s, s.submitSection({true, 7, true}, s.project().revision));
    for (int i = 0; i < 10; ++i) run(s, "project.undo");
    ASSERT_TRUE(s.project().referencePlane);
    EXPECT_DOUBLE_EQ(s.project().referencePlane->pointsMm[0].z, 1);
    EXPECT_EQ(s.project().cloud, cloud);
    EXPECT_TRUE(s.project().section.enabled);
    EXPECT_EQ(finish(s, s.submitLine("project.undo")).errorCode, "no_history");
    run(s, "project.reset");
    EXPECT_FALSE(s.project().referencePlane);
    EXPECT_FALSE(s.project().section.enabled);
    run(s, "project.undo");
    ASSERT_TRUE(s.project().referencePlane);
    EXPECT_DOUBLE_EQ(s.project().referencePlane->pointsMm[0].z, 1);
    EXPECT_FALSE(s.project().section.enabled); // 显示开关不随工程内容回滚。
}

TEST_F(ProjectHistoryTest, LateRecordingKeepsRedoBranchAndReplaysInNewService)
{
    const auto script = directory / "late.py";
    std::vector<std::string> ids;
    double volume{};
    {
        CommandService s;
        import(s);
        build(s);
        ok(s, s.submitDeleteModelParts({"web"}, s.project().revision));
        ok(s, s.submitDeleteModelParts({"rib-1-right"}, s.project().revision));
        run(s, "project.undo");
        ok(s, s.submitExport(directory / "old.step", s.project().revision));
        run(s, "project.snapshot");
        const auto before = s.project();
        const auto started = ok(s, s.submitStartRecording(script));
        EXPECT_NE(started.message.find("已补写 5 条"), std::string::npos);
        EXPECT_EQ(s.project().revision, before.revision);
        EXPECT_EQ(s.project().model, before.model);
        EXPECT_TRUE(read(script).ends_with("project.undo\n"));
        EXPECT_EQ(read(script).find("model.export_step"), std::string::npos);
        EXPECT_EQ(read(script).find("project.snapshot"), std::string::npos);
        run(s, "cloud.redo"); // 旧入口录成统一工程命令。
        EXPECT_TRUE(read(script).ends_with("project.redo\n"));
        EXPECT_EQ(s.recordingState().commandCount, 6);
        ids = s.project().model->partIds;
        volume = s.project().model->volumeMm3;
        ok(s, s.submitStopRecording());
    }
    CommandService replay;
    ok(replay, replay.submitScriptFile(script));
    ASSERT_TRUE(replay.project().model);
    EXPECT_EQ(replay.project().model->partIds, ids);
    EXPECT_DOUBLE_EQ(replay.project().model->volumeMm3, volume);
    run(replay, "project.undo");
    EXPECT_EQ(replay.project().model->solidCount, 6);
    run(replay, "project.redo");
    EXPECT_EQ(replay.project().model->partIds, ids);
}

TEST_F(ProjectHistoryTest, RecordingGapsIncludeEditsAndImportStartsANewPrefix)
{
    CommandService s;
    const auto first = directory / "first.py";
    const auto second = directory / "second.py";
    ok(s, s.submitStartRecording(first));
    import(s);
    run(s, "cloud.delete --indices '[0]'");
    ok(s, s.submitStopRecording());
    run(s, "project.undo");
    run(s, "project.redo");
    ok(s, s.submitStartRecording(second));
    EXPECT_EQ(s.recordingState().commandCount, 4);
    EXPECT_TRUE(read(second).ends_with("project.undo\nproject.redo\n"));
    import(s); // 正在录制的文件继续保留历史；下一份录制只补写最新导入。
    EXPECT_EQ(s.recordingState().commandCount, 5);
    ok(s, s.submitStopRecording());
    ok(s, s.submitStartRecording(directory / "third.py"));
    EXPECT_EQ(s.recordingState().commandCount, 1);
    EXPECT_EQ(read(directory / "third.py").find("project.undo"), std::string::npos);
    EXPECT_TRUE(read(first).ends_with("cloud.delete --indices [0]\n"));
}

TEST_F(ProjectHistoryTest, EmptyProjectParametricHistoryAndCancelledOperationAreAtomic)
{
    CommandService s;
    EXPECT_EQ(finish(s, s.submitLine("project.reset")).errorCode, "no_baseline");
    build(s);
    ASSERT_TRUE(s.project().model);
    EXPECT_FALSE(s.project().canReset);
    run(s, "project.undo");
    EXPECT_FALSE(s.project().model);
    run(s, "project.redo");
    EXPECT_TRUE(s.project().model);
    ProjectOperations operations;
    (void)operations.submitImport(input);
    const auto before = operations.project();
    std::stop_source stop;
    stop.request_stop();
    rm::CommandContext context;
    context.stop = stop.get_token();
    operations.setContext(context);
    EXPECT_THROW((void)operations.submitReconstruct(parameters, before.revision, BeamFrame{}), GeometryError);
    EXPECT_EQ(operations.project().revision, before.revision);
    EXPECT_FALSE(operations.project().canUndo);
}

TEST_F(ProjectHistoryTest, DeletingGeneratedModelStillAllowsResetOfCommittedParameters)
{
    CommandService s;
    import(s);
    build(s);
    ok(s, s.submitDeleteModelParts(s.project().model->partIds, s.project().revision));
    EXPECT_FALSE(s.project().model);
    EXPECT_TRUE(s.project().committedParameters);
    EXPECT_TRUE(s.project().canReset);
    run(s, "project.reset");
    EXPECT_FALSE(s.project().committedParameters);
    EXPECT_FALSE(s.project().canReset);
    run(s, "project.undo");
    ASSERT_TRUE(s.project().committedParameters);
    EXPECT_DOUBLE_EQ(s.project().committedParameters->webThickness, parameters.webThickness);
    EXPECT_FALSE(s.project().model);
}

TEST(ProjectReplayPolicy, EveryRegisteredCommandExplicitlyDeclaresBootstrapPolicy)
{
    rm::CommandManager manager;
    rmcmd::registerAllCommandModules(manager);
    for (const auto &[name, definition] : manager.definitions())
        EXPECT_NE(replayPolicy(canonicalHistoryCommand(name)), ReplayPolicy::Unspecified) << name;
}

TEST_F(ProjectHistoryTest, RecordingFailureRetainsCompletePrefixAndJournalRejectsIncompleteBootstrap)
{
    const auto path = directory / "limit.py";
    MacroRecorder recorder;
    ASSERT_TRUE(recorder.start(path, false).ok());
    recorder.recordLine("project.undo");
    const auto before = read(path);
    const std::string oversized(MacroRecorder::maxScriptBytes, 'x');
    recorder.recordLine(oversized);
    EXPECT_FALSE(recorder.recording());
    EXPECT_FALSE(recorder.state().error.empty());
    EXPECT_EQ(read(path), before);
    ReplayJournal journal;
    journal.append("cloud.import", "cloud.import a.pcd");
    journal.append("cloud.select_indices", oversized);
    EXPECT_THROW((void)journal.prefix(), std::runtime_error);
    journal.append("cloud.import", "cloud.import b.pcd");
    ASSERT_EQ(journal.prefix().size(), 1);
    journal.append("future.edit", "future.edit");
    EXPECT_THROW((void)journal.prefix(), std::runtime_error);
}
TEST_F(ProjectHistoryTest, ModelMoveRotateUndoAndRecordedReplayHaveIdenticalGeometry)
{
    const auto script = directory / "move-rotate.py";
    CommandService s;
    import(s); build(s);
    const auto original = s.project();
    ModelTransform move; move.offsetMm = {-100,12,6};
    ok(s,s.submitModelTransform({"web","rib-1-right"},move,s.project().revision));
    const auto moved = s.project();
    EXPECT_EQ(moved.model->partTransforms.size(),2);
    ok(s,s.submitStartRecording(script)); // 中途开始必须补写移动。
    run(s,"model.rotate --parts '[\"web\",\"rib-1-right\"]' --axis z --angle -60 --center '[0,0,0]'");
    const auto rotated = s.project();
    run(s,"project.undo");
    EXPECT_EQ(s.project().mesh,moved.mesh);
    run(s,"project.undo");
    EXPECT_EQ(s.project().mesh,original.mesh);
    run(s,"project.redo"); run(s,"project.redo");
    EXPECT_EQ(s.project().mesh,rotated.mesh);
    ok(s,s.submitStopRecording());
    EXPECT_NE(read(script).find("model.move --parts"),std::string::npos);
    EXPECT_NE(read(script).find("model.rotate --parts"),std::string::npos);
    CommandService replay;
    ok(replay,replay.submitScriptFile(script));
    const auto actual = replay.project();
    ASSERT_TRUE(actual.mesh);
    ASSERT_EQ(actual.mesh->vertices.size(),rotated.mesh->vertices.size());
    for(std::size_t i=0;i<actual.mesh->vertices.size();++i)
    {
        EXPECT_NEAR(actual.mesh->vertices[i].x,rotated.mesh->vertices[i].x,1e-8);
        EXPECT_NEAR(actual.mesh->vertices[i].y,rotated.mesh->vertices[i].y,1e-8);
        EXPECT_NEAR(actual.mesh->vertices[i].z,rotated.mesh->vertices[i].z,1e-8);
    }
}
TEST_F(ProjectHistoryTest, InvalidAndZeroModelTransformsPreserveRedoAndRecording)
{
    CommandService s;
    build(s);
    run(s,"model.move --parts '[\"web\"]' --offset '[1,2,3]'");
    run(s,"project.undo");
    const auto before = s.project();
    ok(s,s.submitStartRecording(directory/"noop.py"));
    const auto count=s.recordingState().commandCount;
    run(s,"model.move --parts '[\"web\"]' --offset '[0,0,0]'");
    for(const auto line : {
        "model.rotate --parts '[\"web\"]' --axis q --angle 10 --center '[0,0,0]'",
        "model.rotate --parts '[\"web\"]' --axis x --angle 181 --center '[0,0,0]'",
        "model.rotate --parts '[\"web\"]' --axis x --angle 90",
        "model.move --parts '[\"web\",\"missing\"]' --offset '[1,0,0]'",
        "model.move --parts '[\"web\"]' --offset '[1,0]'"})
        EXPECT_FALSE(finish(s,s.submitLine(line)).result.ok());
    EXPECT_EQ(s.project().revision,before.revision);
    EXPECT_EQ(s.project().mesh,before.mesh);
    EXPECT_TRUE(s.project().canRedo);
    EXPECT_EQ(s.recordingState().commandCount,count);
}

}
