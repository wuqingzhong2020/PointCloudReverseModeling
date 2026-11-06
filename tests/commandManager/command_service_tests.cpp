/* 文件描述：固定线程 FIFO、Python 类型桥接、宏回放与取消的产品适配单元测试。 */
#include "commands/CommandBindings.h"
#include "commands/CommandService.h"
#include "commands/CommandFeedback.h"
#include "commands/CommandScript.h"
#include "geometry/Geometry.h"
#include "CommandModules.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <gtest/gtest.h>
#include <thread>
namespace
{
reverse::TaskSnapshot finished(reverse::CommandService &service, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto task = service.task(id);
        if (task.finished())
        {
            return task;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("Command test timed out");
}
std::vector<reverse::UiCommandEvent> completedUiCommands(reverse::CommandService &service)
{
    auto events = service.takeUiCommandEvents();
    std::erase_if(events, [](const auto &event) { return event.kind != reverse::UiCommandEventKind::Finished; });
    return events;
}
struct CloudFixture
{
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("rm-command-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path;
    CloudFixture()
    {
        std::filesystem::create_directory(directory);
        path = directory / std::filesystem::path(u8"中文 点云.pcd");
        std::ofstream file(path);
        file << "VERSION .7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 8\nHEIGHT 1\nPOINTS 8\nDATA "
                "ascii\n";
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
    }
    ~CloudFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
    std::string utf8() const
    {
        const auto value = path.u8string();
        return {reinterpret_cast<const char *>(value.data()), value.size()};
    }
};
reverse::TaskSnapshot waitingForInput(reverse::CommandService &service, const std::string &id,
                                      std::uint64_t afterRequest = 0)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline)
    {
        auto task = service.task(id);
        if (task.waitingForInput && task.inputRequest > afterRequest)
        {
            return task;
        }
        if (task.finished())
        {
            throw std::runtime_error("Expected input request: " + task.message);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("Input request timed out");
}
} // namespace
TEST(UiCommandEcho, QuotedPathsOptionAliasesAndNestedValuesRoundtrip)
{
    rm::CommandManager manager;
    rmcmd::registerAllCommandModules(manager);
    const auto &definition = manager.definitions().at("cloud.import");
    for (const auto &path : {std::string("D:\\目录 名\\O'Brien.pcd"), std::string("--help"),
                             std::string("D:\\trailing space\\"), std::string("a'\" b\\'\".pcd")})
    {
        const auto text = reverse::commandText(definition, {path}, {});
        const auto parsed = manager.validateLine(text.native);
        ASSERT_TRUE(parsed.command) << text.native << ": " << parsed.result.message;
        EXPECT_EQ(parsed.command->arguments.at("path").as<std::string>(), path);
    }
    const auto text = reverse::commandText(manager.definitions().at("model.reconstruct"), {},
                                           {{"end_tolerance", 12.125}, {"mirror", false}});
    EXPECT_NE(text.native.find("--end-tolerance 12.125"), std::string::npos);
    const auto parsed = manager.validateLine(text.native);
    ASSERT_TRUE(parsed.command) << parsed.result.message;
    EXPECT_DOUBLE_EQ(parsed.command->arguments.at("end_tolerance").as<double>(), 12.125);
    EXPECT_FALSE(parsed.command->arguments.at("mirror").as<bool>());
    EXPECT_NE(text.python.find("False"), std::string::npos);
}
TEST(UiCommandEcho, SelectionCropHistoryAndExportReplayInBothModes)
{
    CloudFixture file;
    std::vector<reverse::UiCommandEvent> events;
    std::vector<std::size_t> expectedCounts;
    std::vector<reverse::CloudSelection> expectedSelections;
    {
        reverse::CommandService service;
        const auto collect = [&](const std::string &id) {
            const auto task = finished(service, id);
            EXPECT_TRUE(task.result.ok()) << task.message;
            auto batch = completedUiCommands(service);
            if (batch.size() != 1)
            {
                throw std::runtime_error("Expected exactly one UI echo");
            }
            EXPECT_EQ(batch.front().taskId, id);
            EXPECT_TRUE(batch.front().succeeded);
            EXPECT_EQ(batch.front().message.find("revision"), std::string::npos);
            events.push_back(std::move(batch.front()));
            const auto project = service.project();
            expectedCounts.push_back(project.cloud->points.size());
            expectedSelections.push_back(project.selection ? *project.selection : reverse::CloudSelection{});
            EXPECT_TRUE(completedUiCommands(service).empty());
        };
        collect(service.submitImport(file.path));
        reverse::CloudSelectionRect rectangle;
        rectangle.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
        rectangle.maxX = 0;
        collect(
            service.submitCloudSelection(rectangle, reverse::CloudSelectionMode::Replace, service.project().revision));
        EXPECT_EQ(service.project().selection->size(), 4u);
        collect(service.submitCloudEdit(reverse::CloudEditMode::KeepSelection, service.project().revision));
        EXPECT_EQ(service.project().cloud->points.size(), 4u);
        collect(service.submitCloudHistory(reverse::CloudHistoryAction::Undo, service.project().revision));
        collect(service.submitCloudHistory(reverse::CloudHistoryAction::Redo, service.project().revision));
        collect(service.submitCloudHistory(reverse::CloudHistoryAction::Reset, service.project().revision));
        collect(service.submitCloudSelection(rectangle, reverse::CloudSelectionMode::Add, service.project().revision));
        collect(service.submitCloudEdit(reverse::CloudEditMode::DeleteSelection, service.project().revision));
        collect(service.submitCloudSelection({}, reverse::CloudSelectionMode::Clear, service.project().revision));
        collect(service.submitExportCloud(file.directory / "result.pcd", service.project().revision));
    }
    EXPECT_LT(events.at(1).commandLine.size(), 1000u);
    EXPECT_NE(events.at(1).commandLine.find("viewProjection"), std::string::npos);
    for (const bool python : {false, true})
    {
        // Each replay starts without the output generated by this isolated fixture.
        ASSERT_TRUE(std::filesystem::remove(file.directory / "result.pcd"));
        reverse::CommandService replay;
        for (std::size_t i = 0; i < events.size(); ++i)
        {
            const auto &event = events[i];
            const auto task =
                finished(replay, python ? replay.submitPython(event.pythonLine) : replay.submitLine(event.commandLine));
            ASSERT_TRUE(task.result.ok()) << i << ": " << task.message;
            ASSERT_TRUE(replay.project().cloud);
            EXPECT_EQ(replay.project().cloud->points.size(), expectedCounts[i]);
            const auto selection = replay.project().selection;
            EXPECT_EQ(selection ? *selection : reverse::CloudSelection{}, expectedSelections[i]);
        }
        EXPECT_TRUE(replay.takeUiCommandEvents().empty());
    }
}
TEST(UiCommandEcho, ParametricGeometryAndExportRetainActualParameters)
{
    CloudFixture file;
    std::vector<reverse::UiCommandEvent> events;
    std::size_t solids{};
    const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    reverse::BeamFrame frame;
    frame.originMm = {50, 0, 0};
    {
        reverse::CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
        const auto built = finished(service, service.submitReconstruct(parameters, service.project().revision, frame));
        ASSERT_TRUE(built.result.ok()) << built.message;
        solids = service.project().model->solidCount;
        const auto exported =
            finished(service, service.submitExport(file.directory / "model.step", service.project().revision));
        ASSERT_TRUE(exported.result.ok()) << exported.message;
        events = completedUiCommands(service);
    }
    ASSERT_EQ(events.size(), 3u);
    EXPECT_NE(events[1].commandLine.find("mirrorSymmetry"), std::string::npos);
    EXPECT_NE(events[1].commandLine.find("--frame"), std::string::npos);
    for (const bool python : {false, true})
    {
        ASSERT_TRUE(std::filesystem::remove(file.directory / "model.step"));
        reverse::CommandService replay;
        for (const auto &event : events)
        {
            const auto task =
                finished(replay, python ? replay.submitPython(event.pythonLine) : replay.submitLine(event.commandLine));
            ASSERT_TRUE(task.result.ok()) << task.message;
        }
        ASSERT_TRUE(replay.project().model);
        EXPECT_EQ(replay.project().model->solidCount, solids);
        EXPECT_TRUE(replay.project().model->parameters.mirrorSymmetry);
        EXPECT_EQ(replay.project().model->parameters.ribStations, parameters.ribStations);
        EXPECT_DOUBLE_EQ(replay.project().model->frame.originMm.x, 50);
    }
}
TEST(UiCommandEcho, FailedAndCancelledUiTasksAreReportedOnceWithoutConsoleDuplicates)
{
    reverse::CommandService service;
    const auto bad = service.submitRecognizedReconstruct(false, 0, -1);
    EXPECT_FALSE(finished(service, bad).result.ok());
    const auto errors = completedUiCommands(service);
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FALSE(errors.front().succeeded);
    EXPECT_NE(errors.front().message.find("end_tolerance"), std::string::npos);
    EXPECT_NE(errors.front().commandLine.find("--end-tolerance -1"), std::string::npos);

    const auto blocked = service.submitPython("input('waiting')");
    const auto waiting = waitingForInput(service, blocked);
    const auto ui = service.submitCloudHistory(reverse::CloudHistoryAction::Reset, 0);
    EXPECT_TRUE(completedUiCommands(service).empty());
    ASSERT_TRUE(service.cancel(ui));
    ASSERT_TRUE(service.provideInput(blocked, waiting.inputRequest, std::string("continue")));
    ASSERT_TRUE(finished(service, blocked).result.ok());
    EXPECT_EQ(finished(service, ui).state, reverse::TaskState::Cancelled);
    const auto cancelled = completedUiCommands(service);
    ASSERT_EQ(cancelled.size(), 1u);
    EXPECT_FALSE(cancelled.front().succeeded);
    EXPECT_EQ(cancelled.front().commandLine, "project.reset");
    (void)finished(service, service.submitLine("help"));
    (void)finished(service, service.submitCommand("project.snapshot"));
    EXPECT_TRUE(completedUiCommands(service).empty());
}
TEST(CloudEditCommands, UiCropAndDeleteReplayAloneWithNoOrDifferentLiveSelection)
{
    CloudFixture file;
    for (const auto mode : {reverse::CloudEditMode::KeepSelection, reverse::CloudEditMode::DeleteSelection})
    {
        reverse::UiCommandEvent edit;
        std::vector<std::size_t> expected;
        {
            reverse::CommandService service;
            ASSERT_TRUE(
                finished(service, service.submitImport(file.path)).result.ok());
            reverse::CloudSelectionRect rect;
            rect.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
            rect.maxX = 0;
            ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Replace,
                                                                       service.project().revision))
                            .result.ok());
            ASSERT_TRUE(finished(service, service.submitCloudEdit(mode, service.project().revision)).result.ok());
            edit = completedUiCommands(service).back();
            EXPECT_NE(edit.commandLine.find("--regions"), std::string::npos);
            EXPECT_NE(edit.commandLine.find("viewProjection"), std::string::npos);
            EXPECT_EQ(edit.commandLine.find("--indices"), std::string::npos);
            for (const auto &point : service.project().cloud->points)
                expected.push_back(point.sourceIndex);
        }
        for (const bool python : {false, true})
        {
            reverse::CommandService replay;
            ASSERT_TRUE(finished(replay, replay.submitCommand("cloud.import", {file.utf8()})).result.ok());
            if (python)
            {
                // Deliberately select the opposite/smaller set. The edit must ignore it.
                ASSERT_TRUE(
                    finished(replay, replay.submitCommand("cloud.select_indices", {}, {{"indices", rm::Array{7}}}))
                        .result.ok());
            }
            const auto task =
                finished(replay, python ? replay.submitPython(edit.pythonLine) : replay.submitLine(edit.commandLine));
            ASSERT_TRUE(task.result.ok()) << task.message;
            std::vector<std::size_t> actual;
            for (const auto &point : replay.project().cloud->points)
                actual.push_back(point.sourceIndex);
            EXPECT_EQ(actual, expected);
            EXPECT_EQ(actual.size(), 4u);
        }
    }
}
TEST(CloudEditCommands, EditingRectangleReplacesItsContributionForEverySelectionMode)
{
    using namespace reverse;
    CloudFixture file;
    for (const auto mode : {CloudSelectionMode::Replace, CloudSelectionMode::Add, CloudSelectionMode::Remove})
    {
        CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
        const rm::Array seed = mode == CloudSelectionMode::Remove ? rm::Array{0, 1, 2, 3, 4, 5, 6, 7} : rm::Array{0};
        ASSERT_TRUE(finished(service, service.submitCommand("cloud.select_indices", {}, {{"indices", seed}})).result.ok());
        CloudSelectionRect rect;
        rect.viewProjection = {.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1};
        rect.maxX = 0;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, mode, service.project().revision)).result.ok());
        rect.minX = 0;
        rect.maxX = 1;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, mode, service.project().revision, true)).result.ok());
        const CloudSelection expected = mode == CloudSelectionMode::Remove ? CloudSelection{0, 1, 2, 3}
            : mode == CloudSelectionMode::Add ? CloudSelection{0, 4, 5, 6, 7} : CloudSelection{4, 5, 6, 7};
        EXPECT_EQ(*service.project().selection, expected);
        // 将框缩到无点区域：恢复减选原本移除的点，撤回加选旧框贡献。
        rect.minX = .7;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, mode, service.project().revision, true)).result.ok());
        const CloudSelection emptyRegionExpected = mode == CloudSelectionMode::Remove ? CloudSelection{0, 1, 2, 3, 4, 5, 6, 7}
            : mode == CloudSelectionMode::Add ? CloudSelection{0} : CloudSelection{};
        EXPECT_EQ(*service.project().selection, emptyRegionExpected);
        ASSERT_EQ(service.project().selectionRecipe->regions.size(), 1u);
        EXPECT_DOUBLE_EQ(service.project().selectionRecipe->regions.back().rectangle.minX, .7);
    }
}

TEST(CloudEditCommands, EditedRectanglePreservesEarlierRegionsAndCropReplay)
{
    using namespace reverse;
    CloudFixture file;
    CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    CloudSelectionRect rect;
    rect.viewProjection = {.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1};
    rect.maxX = 0;
    ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Replace,
                                                               service.project().revision)).result.ok());
    rect.minX = 0;
    rect.maxX = 1;
    ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Add,
                                                               service.project().revision)).result.ok());
    rect.minY = 0;
    ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Add,
                                                               service.project().revision, true)).result.ok());
    EXPECT_EQ(*service.project().selection, (CloudSelection{0, 1, 2, 3, 6, 7}));
    ASSERT_EQ(service.project().selectionRecipe->regions.size(), 2u);
    EXPECT_EQ(service.project().selectionRecipe->regions.front().mode, CloudSelectionMode::Replace);
    const auto events = completedUiCommands(service);
    ASSERT_FALSE(events.empty());
    EXPECT_NE(events.back().commandLine.find("--edit-last true"), std::string::npos);
    CommandService replay;
    for (const auto &event : events)
    {
        const auto task = finished(replay, replay.submitLine(event.commandLine));
        ASSERT_TRUE(task.result.ok()) << task.message;
    }
    EXPECT_EQ(*replay.project().selection, *service.project().selection);
    ASSERT_TRUE(finished(service, service.submitCloudEdit(CloudEditMode::KeepSelection,
                                                         service.project().revision)).result.ok());
    const auto crop = completedUiCommands(service).back();
    ASSERT_TRUE(finished(replay, replay.submitImport(file.path)).result.ok());
    ASSERT_TRUE(finished(replay, replay.submitLine(crop.commandLine)).result.ok());
    ASSERT_EQ(replay.project().cloud->points.size(), 6u);
    for (std::size_t i = 0; i < 6; ++i)
        EXPECT_EQ(replay.project().cloud->points[i].sourceIndex, service.project().cloud->points[i].sourceIndex);
}

TEST(CloudEditCommands, InvalidRectangleEditsPreserveCommittedSelection)
{
    using namespace reverse;
    CloudFixture file;
    CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    CloudSelectionRect rect;
    rect.viewProjection = {.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1};
    const auto revision = service.project().revision;
    EXPECT_EQ(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Replace, revision, true)).errorCode,
              "invalid_selection");
    EXPECT_EQ(service.project().revision, revision);
    ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Replace, revision)).result.ok());
    const auto before = service.project();
    EXPECT_EQ(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Add, before.revision, true)).errorCode,
              "invalid_selection");
    EXPECT_EQ(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Replace, revision, true)).errorCode,
              "stale_revision");
    rect.maxX = rect.minX;
    EXPECT_EQ(finished(service, service.submitCloudSelection(rect, CloudSelectionMode::Replace, before.revision, true)).errorCode,
              "invalid_selection");
    EXPECT_EQ(service.project().revision, before.revision);
    EXPECT_EQ(service.project().selection, before.selection);
    EXPECT_EQ(service.project().selectionRecipe, before.selectionRecipe);
}

TEST(CloudEditCommands, MultipleCameraRegionsRemainReplayableAfterUndoClearsSelection)
{
    CloudFixture file;
    reverse::UiCommandEvent crop, remove;
    std::vector<std::size_t> expectedCrop, expectedRemove;
    {
        reverse::CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
        reverse::CloudSelectionRect rect;
        // Imported points are local to the first sample: x=0/100, y=0/40, z=0/60.
        rect.viewProjection = {.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1};
        rect.maxX = 0;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Replace,
                                                                   service.project().revision))
                        .result.ok());
        rect.minX = 0;
        rect.maxX = 1;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Add,
                                                                   service.project().revision))
                        .result.ok());
        // Different camera: screen X now follows local Y, removing the upper half.
        rect.viewProjection = {0, .01, 0, -.2, .01, 0, 0, -.5, 0, 0, .01, -.3, 0, 0, 0, 1};
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Remove,
                                                                   service.project().revision))
                        .result.ok());
        ASSERT_EQ(service.project().selection->size(), 4u);
        ASSERT_TRUE(finished(service,
                             service.submitCloudEdit(reverse::CloudEditMode::KeepSelection, service.project().revision))
                        .result.ok());
        crop = completedUiCommands(service).back();
        for (const auto &point : service.project().cloud->points)
            expectedCrop.push_back(point.sourceIndex);
        ASSERT_TRUE(
            finished(service, service.submitCloudHistory(reverse::CloudHistoryAction::Undo, service.project().revision))
                .result.ok());
        EXPECT_FALSE(service.project().selectionRecipe);
        EXPECT_FALSE(service.project().selection);
        // 编辑命令已携带全部框选参数；撤销清空临时选区后仍可直接复用。
        auto removeLine = crop.commandLine;
        removeLine.replace(0, std::string("cloud.crop").size(), "cloud.delete");
        ASSERT_TRUE(finished(service, service.submitLine(removeLine)).result.ok());
        remove.commandLine = removeLine;
        for (const auto &point : service.project().cloud->points)
            expectedRemove.push_back(point.sourceIndex);
    }
    for (const auto &entry : {std::pair{crop, expectedCrop}, std::pair{remove, expectedRemove}})
    {
        reverse::CommandService replay;
        ASSERT_TRUE(finished(replay, replay.submitCommand("cloud.import", {file.utf8()})).result.ok());
        const auto task = finished(replay, replay.submitLine(entry.first.commandLine));
        ASSERT_TRUE(task.result.ok()) << task.message;
        std::vector<std::size_t> actual;
        for (const auto &point : replay.project().cloud->points)
            actual.push_back(point.sourceIndex);
        EXPECT_EQ(actual, entry.second);
    }
}
TEST(CloudEditCommands, IndexSeedPlusRectangleAndClearDoNotReuseOldRegions)
{
    CloudFixture file;
    reverse::UiCommandEvent edit;
    std::vector<std::size_t> expected;
    {
        reverse::CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
        ASSERT_TRUE(finished(service, service.submitCommand("cloud.select_indices", {}, {{"indices", rm::Array{0, 0}}}))
                        .result.ok());
        reverse::CloudSelectionRect rect;
        rect.viewProjection = {.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1};
        rect.minX = 0;
        ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Add,
                                                                   service.project().revision))
                        .result.ok());
        ASSERT_EQ(service.project().selection->size(), 5u);
        ASSERT_TRUE(finished(service,
                             service.submitCloudEdit(reverse::CloudEditMode::KeepSelection, service.project().revision))
                        .result.ok());
        edit = completedUiCommands(service).back();
        EXPECT_NE(edit.commandLine.find("--indices"), std::string::npos);
        EXPECT_NE(edit.commandLine.find("--regions"), std::string::npos);
        for (const auto &point : service.project().cloud->points)
            expected.push_back(point.sourceIndex);
        ASSERT_TRUE(
            finished(service, service.submitCloudHistory(reverse::CloudHistoryAction::Undo, service.project().revision))
                .result.ok());
        ASSERT_TRUE(finished(service, service.submitCloudSelection({}, reverse::CloudSelectionMode::Clear,
                                                                   service.project().revision))
                        .result.ok());
        const auto before = service.project();
        const auto empty =
            finished(service, service.submitCloudEdit(reverse::CloudEditMode::KeepSelection, before.revision));
        EXPECT_EQ(empty.errorCode, "empty_selection");
        EXPECT_EQ(service.project().revision, before.revision);
        EXPECT_EQ(completedUiCommands(service).back().commandLine, "cloud.crop --indices []");
    }
    reverse::CommandService replay;
    ASSERT_TRUE(finished(replay, replay.submitCommand("cloud.import", {file.utf8()})).result.ok());
    const auto task = finished(replay, replay.submitPython(edit.pythonLine));
    ASSERT_TRUE(task.result.ok()) << task.message;
    std::vector<std::size_t> actual;
    for (const auto &point : replay.project().cloud->points)
        actual.push_back(point.sourceIndex);
    EXPECT_EQ(actual, expected);
}
TEST(CloudEditCommands, InvalidExplicitSelectionDoesNotModifyLiveState)
{
    CloudFixture file;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitCommand("cloud.import", {file.utf8()})).result.ok());
    ASSERT_TRUE(
        finished(service, service.submitCommand("cloud.select_indices", {}, {{"indices", rm::Array{0}}})).result.ok());
    const auto before = service.project();
    rm::Arguments rectangle{{"viewProjection", rm::Array{.01, 0, 0, -.5, 0, .01, 0, -.2, 0, 0, .01, -.3, 0, 0, 0, 1}},
                            {"minX", -.1},
                            {"minY", -.1},
                            {"maxX", .1},
                            {"maxY", .1},
                            {"zeroToOneDepth", false}};
    const auto crop = [&](const rm::Arguments &r) {
        return finished(service, service.submitCommand(
                                     "cloud.crop", {},
                                     {{"regions", rm::Array{rm::Arguments{{"rectangle", r}, {"mode", "replace"}}}}}));
    };
    EXPECT_EQ(crop(rectangle).errorCode, "empty_selection");
    rectangle["maxX"] = -.2;
    const auto reversed = crop(rectangle);
    EXPECT_EQ(reversed.errorCode, "invalid_selection");
    EXPECT_NE(reversed.message.find("--regions[0].rectangle"), std::string::npos);
    rectangle["maxX"] = .1;
    rectangle["zeroToOneDepth"] = "false";
    const auto invalidType = crop(rectangle);
    EXPECT_EQ(invalidType.errorCode, "invalid_value");
    EXPECT_NE(invalidType.message.find("regions[0].rectangle.zeroToOneDepth"), std::string::npos);
    EXPECT_EQ(finished(service, service.submitLine("cloud.delete --indices '[999]' ")).errorCode, "invalid_index");
    const auto after = service.project();
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.cloud, before.cloud);
    EXPECT_EQ(after.selection, before.selection);
    EXPECT_EQ(after.selectionRecipe, before.selectionRecipe);
}
TEST(CommandQueue, SameThreadFifoAndRevisionCheckedAtExecution)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto first = service.submitCommand("cloud.import", {file.utf8()});
    const auto second = service.submitCommand("project.snapshot");
    const auto stale = service.submitCommand("cloud.select_indices", {}, {{"indices", rm::Array{}}}, 0);
    const auto result1 = finished(service, first);
    ASSERT_TRUE(result1.result.ok()) << result1.message;
    const auto result2 = finished(service, second);
    ASSERT_TRUE(result2.result.ok());
    EXPECT_EQ(reverse::commandJson(result2.result.value).at("revision"), 1);
    EXPECT_EQ(finished(service, stale).errorCode, "stale_revision");
    EXPECT_EQ(result1.workerThread, result2.workerThread);
    EXPECT_EQ(result2.workerThread, finished(service, service.submitLine("help")).workerThread);
}
TEST(PythonCommands, IsolatedRuntimeStrictTypesHelpAndWrongThread)
{
    reverse::CommandService service;
    const auto task = finished(service, service.submitPython(R"PY(
import rm, sys, threading, json, math, sqlite3, ssl
assert '' not in sys.path
assert sys.flags.isolated == 1
assert rm.help('model.reconstruct') == rm.execute_line('model.reconstruct --help')
assert rm.execute('project.snapshot')['revision'] == 0
try:
    rm.execute('model.reconstruct', mirror='false')
    raise AssertionError('string was accepted as bool')
except rm.CommandError as e:
    assert e.code == 'invalid_value'
    assert e.details['parameter'] == 'mirror'
errors = []
def other_thread():
    try: rm.execute('project.snapshot')
    except RuntimeError as e: errors.append(str(e))
t = threading.Thread(target=other_thread)
t.start()
t.join()
assert len(errors) == 1 and 'wrong_thread' in errors[0]
print('桥接成功')
)PY"));
    EXPECT_TRUE(task.result.ok()) << task.message;
    EXPECT_NE(task.output.find("桥接成功"), std::string::npos);
}
TEST(PythonCommands, ScriptIsOneQueueJobAndFailurePreservesEarlierCommit)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto code = "import rm\nrm.execute('cloud.import', " + rm::formatValue(file.utf8()) +
                      ")\n"
                      "assert rm.execute('project.snapshot')['revision'] == 1\nraise ValueError('expected stop')\n";
    const auto script = service.submitPython(code);
    const auto after = service.submitCommand("project.snapshot");
    const auto failure = finished(service, script);
    EXPECT_EQ(failure.errorCode, "python_error");
    EXPECT_TRUE(failure.result.committed);
    EXPECT_EQ(reverse::commandJson(finished(service, after).result.value).at("revision"), 1);
    EXPECT_TRUE(service.project().cloud);
}
TEST(PythonCommands, CancelRunningAndQueuedWorkThenContinue)
{
    reverse::CommandService service;
    const auto running = service.submitPython("import time\ntime.sleep(0.3)\nwhile True:\n    value = 1\n");
    const auto queued = service.submitCommand("project.snapshot");
    ASSERT_TRUE(service.cancel(queued));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (service.task(running).state == reverse::TaskState::Queued && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(service.cancel(running));
    EXPECT_EQ(finished(service, running).state, reverse::TaskState::Cancelled);
    EXPECT_EQ(finished(service, queued).state, reverse::TaskState::Cancelled);
    EXPECT_TRUE(finished(service, service.submitLine("help")).result.ok());
}
namespace
{
std::string readScript(const std::filesystem::path &path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace
TEST(CommandMacros, WritesEachCommandBeforeStopAndReplaysChinesePath)
{
    CloudFixture file;
    const auto path = file.directory / std::filesystem::path(u8"录制 脚本.py");
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(path)).result.ok());
    EXPECT_TRUE(service.recordingState().active);
    EXPECT_EQ(readScript(path).find("import rm"), std::string::npos);
    EXPECT_EQ(readScript(path).find("schema"), std::string::npos);
    ASSERT_TRUE(finished(service, service.submitCommand("cloud.import", {file.utf8()})).result.ok());
    // Read while still recording: a successful command is already present on disk.
    const auto imported = readScript(path);
    EXPECT_NE(imported.find("\ncloud.import "), std::string::npos);
    EXPECT_EQ(imported.find("rm.execute"), std::string::npos);
    EXPECT_EQ(imported.find("require_version"), std::string::npos);
    EXPECT_NE(imported.find("mm"), std::string::npos);
    EXPECT_EQ(service.recordingState().commandCount, 1);
    (void)finished(service, service.submitLine("help"));
    (void)finished(service, service.submitLine("cloud.import --help"));
    (void)finished(service, service.submitLine("model.reconstruct --mirror wrong"));
    EXPECT_EQ(readScript(path), imported);
    ASSERT_TRUE(finished(service, service.submitLine("project.snapshot")).result.ok());
    EXPECT_EQ(service.recordingState().commandCount, 2);
    EXPECT_NE(readScript(path).find("project.snapshot"), std::string::npos);
    const auto beforeStop = readScript(path);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    EXPECT_FALSE(service.recordingState().active);
    EXPECT_EQ(readScript(path), beforeStop);
    reverse::CommandService replay;
    const auto result = finished(replay, replay.submitScriptFile(path));
    ASSERT_TRUE(result.result.ok()) << result.message;
    ASSERT_TRUE(replay.project().cloud);
    EXPECT_EQ(replay.project().cloud->points.size(), 8);
    EXPECT_EQ(readScript(path), beforeStop);
}
TEST(CommandMacros, GeometrySelectionIsCompactAndCropReplaysWithoutViewport)
{
    CloudFixture file;
    const auto path = file.directory / "selection.py";
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(path)).result.ok());
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    reverse::CloudSelectionRect rectangle;
    rectangle.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
    rectangle.maxX = 0;
    ASSERT_TRUE(finished(service, service.submitCloudSelection(rectangle, reverse::CloudSelectionMode::Replace,
                                                               service.project().revision))
                    .result.ok());
    ASSERT_TRUE(
        finished(service, service.submitCloudEdit(reverse::CloudEditMode::KeepSelection, service.project().revision))
            .result.ok());
    const auto script = readScript(path);
    EXPECT_NE(script.find("viewProjection"), std::string::npos);
    EXPECT_NE(script.find("regions"), std::string::npos);
    EXPECT_EQ(script.find("cloud.select_indices"), std::string::npos);
    EXPECT_LT(script.size(), 4096);
    EXPECT_EQ(service.recordingState().commandCount, 3);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    reverse::CommandService replay;
    const auto result = finished(replay, replay.submitScriptFile(path));
    ASSERT_TRUE(result.result.ok()) << result.message;
    ASSERT_TRUE(replay.project().cloud);
    EXPECT_EQ(replay.project().cloud->points.size(), service.project().cloud->points.size());
    EXPECT_EQ(replay.project().cloud->points.size(), 4);
    for (std::size_t i = 0; i < replay.project().cloud->points.size(); ++i)
    {
        const auto &actual = replay.project().cloud->points[i];
        const auto &expected = service.project().cloud->points[i];
        EXPECT_EQ(actual.sourceIndex, expected.sourceIndex);
        EXPECT_DOUBLE_EQ(actual.position.x, expected.position.x);
        EXPECT_DOUBLE_EQ(actual.position.y, expected.position.y);
        EXPECT_DOUBLE_EQ(actual.position.z, expected.position.z);
    }
}
TEST(CommandMacros, ControlsFollowFifoAndReplayAppendsCommands)
{
    CloudFixture file;
    const auto path = file.directory / "fifo.py";
    reverse::CommandService service;
    const auto start = service.submitStartRecording(path);
    const auto import = service.submitImport(file.path);
    const auto stop = service.submitStopRecording();
    const auto after = service.submitLine("project.snapshot");
    ASSERT_TRUE(finished(service, after).result.ok());
    EXPECT_TRUE(finished(service, start).result.ok());
    EXPECT_TRUE(finished(service, import).result.ok());
    EXPECT_TRUE(finished(service, stop).result.ok());
    EXPECT_EQ(service.recordingState().commandCount, 1);
    EXPECT_EQ(readScript(path).find("project.snapshot"), std::string::npos);
    const auto other = file.directory / "second.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(other)).result.ok());
    const auto source = readScript(path);
    const auto replay = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(replay.result.ok()) << replay.message;
    EXPECT_TRUE(service.recordingState().active);
    EXPECT_EQ(service.recordingState().commandCount, 2); // 一条补写导入，加一条实际回放导入。
    EXPECT_TRUE(readScript(other).starts_with(source));
    EXPECT_EQ(readScript(path), source);
    // Direct Python calls use the same recorder as commands from script files.
    ASSERT_TRUE(finished(service, service.submitPython("rm.execute('project.snapshot')")).result.ok());
    EXPECT_EQ(service.recordingState().commandCount, 3);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}
TEST(CommandMacros, NativeScriptRecordsSuccessfulPrefixAndStopsAtInvalidParameter)
{
    CloudFixture file;
    const auto input = file.directory / "source.py";
    const auto output = file.directory / std::filesystem::path(u8"再次录制.py");
    {
        std::ofstream script(input, std::ios::binary);
        script << "# 与命令窗口相同\ncloud.import \"" << file.utf8()
               << "\"\ncloud.select_indices --indices '[0,1]'\n"
                  "cloud.crop\nmodel.reconstruct --mirror wrong\ncloud.reset\n";
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(output)).result.ok());
    const auto script = service.submitScriptFile(input);
    const auto after = service.submitLine("project.snapshot");
    const auto result = finished(service, script);
    EXPECT_EQ(result.errorCode, "invalid_value");
    EXPECT_TRUE(result.result.committed);
    EXPECT_NE(result.output.find("// cloud.import "), std::string::npos);
    EXPECT_NE(result.output.find("已导入点云，共 8 点。"), std::string::npos);
    EXPECT_NE(result.output.find("// cloud.select_indices --indices '[0,1]'"), std::string::npos);
    EXPECT_NE(result.output.find("// cloud.crop"), std::string::npos);
    EXPECT_NE(result.output.find("// model.reconstruct --mirror wrong"), std::string::npos);
    EXPECT_TRUE(result.output.ends_with("未完成：" + result.message + '\n'));
    EXPECT_TRUE(result.message.starts_with("第 5 行："));
    EXPECT_NE(result.message.find("--mirror"), std::string::npos);
    EXPECT_EQ(result.result.value.as<rm::Arguments>().at("scriptLine"), rm::Value(5U));
    EXPECT_EQ(result.result.value.as<rm::Arguments>().at("parameter"), rm::Value("--mirror"));
    EXPECT_EQ(result.output.find("// cloud.reset"), std::string::npos);
    EXPECT_EQ(result.output.find("canUndoCloud"), std::string::npos);
    EXPECT_TRUE(service.recordingState().active);
    ASSERT_TRUE(service.project().cloud);
    EXPECT_EQ(service.project().cloud->points.size(), 2);
    const auto subsequent = finished(service, after);
    EXPECT_EQ(subsequent.workerThread, result.workerThread);
    EXPECT_EQ(reverse::commandJson(subsequent.result.value).at("revision"), service.project().revision);
    EXPECT_EQ(service.recordingState().commandCount, 4); // Three edits, then the queued query.
    const auto recorded = readScript(output);
    EXPECT_LT(recorded.find("cloud.import"), recorded.find("cloud.select_indices"));
    EXPECT_LT(recorded.find("cloud.select_indices"), recorded.find("cloud.crop"));
    EXPECT_EQ(recorded.find("rm.execute"), std::string::npos);
    EXPECT_EQ(recorded.find("model.reconstruct"), std::string::npos);
    EXPECT_EQ(recorded.find("cloud.reset"), std::string::npos);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}
TEST(ScriptEcho, NativeLinesCommentsHelpAndQueriesWithoutRecording)
{
    CloudFixture file;
    const auto path = file.directory / "echo.py";
    {
        std::ofstream script(path, std::ios::binary);
        script << "\xEF\xBB\xBF"
                  "  # 注释不执行\r\n\r\n  project.snapshot  \r\n"
                  "help cloud.crop\ncloud.reset --help\nproject.snapshot";
    }
    reverse::CommandService service;
    const auto result = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(result.result.ok()) << result.message;
    EXPECT_FALSE(service.recordingState().active);
    EXPECT_EQ(result.message, "脚本执行完成，共 4 条命令。");
    const auto first = result.output.find("// project.snapshot\n");
    ASSERT_NE(first, std::string::npos);
    const auto second = result.output.find("// project.snapshot\n", first + 1);
    ASSERT_NE(second, std::string::npos);
    EXPECT_EQ(result.output.find("// project.snapshot\n", second + 1), std::string::npos);
    EXPECT_NE(result.output.find("// help cloud.crop\n"), std::string::npos);
    EXPECT_NE(result.output.find("// cloud.reset --help\n"), std::string::npos);
    EXPECT_NE(result.output.find("用法: cloud.crop"), std::string::npos);
    EXPECT_EQ(result.output.find("注释"), std::string::npos);
    EXPECT_EQ(result.output.find("版本:"), std::string::npos);
    EXPECT_EQ(finished(service, service.submitLine("project.snapshot")).output, "// project.snapshot\n");
    EXPECT_EQ(finished(service, service.submitPythonLine("1 + 1")).output, "2\n");
    EXPECT_TRUE(completedUiCommands(service).empty());
}
TEST(CommandScripts, StreamsBeforeHandlerAndCancelsBetweenCommandsOnSameThread)
{
    rm::CommandManager manager;
    rm::CommandDefinition command;
    command.name = "test.step";
    command.module = "test";
    command.summary = "测试提交";
    command.usage = "test.step";
    manager.registerDefinition(command);
    std::stop_source cancellation;
    std::string output;
    int calls = 0;
    const auto thread = std::this_thread::get_id();
    manager.bindHandler("test.step", [&](const auto &, const auto &) {
        EXPECT_EQ(std::this_thread::get_id(), thread);
        EXPECT_TRUE(output.ends_with("// test.step\n"));
        ++calls;
        return rm::CommandResult{rm::ResultKind::Success, {}, "done", {}, true};
    });
    manager.freeze();
    rm::CommandContext context;
    context.stop = cancellation.get_token();
    context.output = [&](std::string_view message) {
        output += message;
        if (message == "done\n")
        {
            cancellation.request_stop();
        }
    };
    const auto result = reverse::executeCommandScript(manager, "test.step\ntest.step\n", context);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(result.code, "cancelled");
    EXPECT_TRUE(result.committed);
    EXPECT_TRUE(result.message.starts_with("第 2 行："));
    EXPECT_TRUE(output.ends_with("未完成：" + result.message + '\n'));
}
TEST(CommandScripts, ReportsSyntaxErrorsWithPhysicalLineNumbersWithoutExecutingLaterLines)
{
    rm::CommandManager manager;
    rmcmd::registerAllCommandModules(manager);
    manager.freeze();
    const std::pair<std::string, std::string> cases[]{
        {"project.snapshot --typo=123", "unknown_option"},
        {"cloud.import \"missing quote", "unclosed_quote"},
        {"import rm", "unknown_command"},
        {"rm.execute('project.snapshot')", "unknown_command"},
        {"cloud.select_indices --indices '[a]'", "invalid_json_value"}};
    for (const auto &[line, code] : cases)
    {
        SCOPED_TRACE(line);
        std::string output;
        rm::CommandContext context;
        context.output = [&](std::string_view message) { output += message; };
        const auto result = reverse::executeCommandScript(manager, "# header\n\n" + line + "\nhelp", context);
        EXPECT_EQ(result.code, code) << result.message;
        EXPECT_TRUE(result.message.starts_with("第 3 行："));
        EXPECT_FALSE(result.committed);
        EXPECT_EQ(output.find("// help"), std::string::npos);
    }
    EXPECT_EQ(reverse::executeCommandScript(manager, "# 空脚本\r\n\n", {}).message, "脚本执行完成，共 0 条命令。");
}
TEST(CommandMacros, ExecutingCurrentRecordingFileUsesOnlyItsExistingCommands)
{
    CloudFixture file;
    const auto path = file.directory / "self.py";
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(path)).result.ok());
    ASSERT_TRUE(finished(service, service.submitLine("project.snapshot")).result.ok());
    EXPECT_EQ(service.recordingState().commandCount, 1);
    const auto before = readScript(path);
    const auto result = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(result.result.ok()) << result.message;
    EXPECT_EQ(service.recordingState().commandCount, 2);
    EXPECT_TRUE(service.recordingState().active);
    const auto after = readScript(path);
    EXPECT_TRUE(after.starts_with(before));
    EXPECT_EQ(after.substr(before.size()), "project.snapshot\n");
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}
TEST(CommandMacros, InvalidPathsAndRepeatedStartDoNotEraseRecording)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto existing = file.directory / "existing.py";
    {
        std::ofstream output(existing);
        output << "# preserve me\n";
    }
    const auto original = readScript(existing);
    EXPECT_EQ(finished(service, service.submitStartRecording(existing)).errorCode, "output_exists");
    EXPECT_EQ(readScript(existing), original);
    EXPECT_EQ(finished(service, service.submitStartRecording(file.path)).errorCode, "invalid_script_path");
    EXPECT_EQ(finished(service, service.submitStartRecording(existing / "script.py")).errorCode,
              "recording_open_failed");
    EXPECT_FALSE(service.recordingState().active);
    EXPECT_EQ(finished(service, service.submitStopRecording()).errorCode, "not_recording");
    ASSERT_TRUE(finished(service, service.submitStartRecording(existing, true)).result.ok());
    ASSERT_TRUE(finished(service, service.submitLine("project.snapshot")).result.ok());
    const auto recorded = readScript(existing);
    EXPECT_EQ(finished(service, service.submitStartRecording(existing, true)).errorCode, "already_recording");
    EXPECT_EQ(readScript(existing), recorded);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    EXPECT_EQ(readScript(existing), recorded);
    const auto nested = file.directory / "missing" / "script.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(nested)).result.ok());
    EXPECT_TRUE(std::filesystem::is_regular_file(nested));
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}
TEST(CommandMacros, ScriptFailureStopsLaterCommandsAndRetainsEarlierCommit)
{
    CloudFixture file;
    const auto path = file.directory / "failure.py";
    {
        std::ofstream script(path, std::ios::binary);
        script << "\xEF\xBB\xBF"
                  "cloud.import \"" << file.utf8()
               << "\"\r\ncloud.unknown\r\ncloud.select_indices --indices [0]\r\n";
    }
    reverse::CommandService service;
    const auto task = finished(service, service.submitScriptFile(path));
    EXPECT_EQ(task.errorCode, "unknown_command");
    EXPECT_TRUE(task.result.committed);
    EXPECT_TRUE(task.message.starts_with("第 2 行："));
    ASSERT_TRUE(service.project().cloud);
    EXPECT_EQ(service.project().cloud->points.size(), 8);
    EXPECT_TRUE(!service.project().selection || service.project().selection->empty());
    EXPECT_EQ(finished(service, service.submitScriptFile(file.directory / "not-found.py")).errorCode,
              "script_open_failed");
    EXPECT_TRUE(finished(service, service.submitLine("help")).result.ok());
}
TEST(UnifiedConsole, PythonBuiltinsExpressionsAndImportsShareNativeCommandInput)
{
    reverse::CommandService service;
    const auto run = [&](const std::string &line) { return finished(service, service.submitLine(line)); };
    const auto printed = run("print('aaaa')");
    ASSERT_TRUE(printed.result.ok()) << printed.message;
    EXPECT_EQ(printed.output, "aaaa\n");
    EXPECT_FALSE(reverse::commandJson(printed.result.value).at("more"));
    EXPECT_TRUE(run("amount = 21").result.ok());
    EXPECT_EQ(run("amount * 2").output, "42\n");
    EXPECT_TRUE(run("import math").result.ok());
    EXPECT_TRUE(run("math.pi").output.starts_with("3.14159"));
    EXPECT_EQ(run("len([1, 2, 3])").output, "3\n");
    EXPECT_EQ(run("help cloud.import").result.kind, rm::ResultKind::Help);
    EXPECT_EQ(run("model.reconstruct --mirror wrong").errorCode, "invalid_value");
    EXPECT_EQ(run("cloud.missing").errorCode, "unknown_command");
    EXPECT_EQ(run("1 / 0").errorCode, "python_error");
    EXPECT_EQ(run("print('恢复')").output, "恢复\n");
    EXPECT_EQ(run("project.snapshot").workerThread, printed.workerThread);
}
TEST(UnifiedConsole, MultilinePythonMayContainNativeCommandsAndCanBeReset)
{
    reverse::CommandService service;
    const auto run = [&](const std::string &line) { return finished(service, service.submitLine(line)); };
    EXPECT_TRUE(reverse::commandJson(run("for i in range(2):").result.value).at("more"));
    EXPECT_TRUE(reverse::commandJson(run("    print(i)").result.value).at("more"));
    const auto body = run("    project.snapshot");
    ASSERT_TRUE(body.result.ok()) << body.message;
    EXPECT_TRUE(reverse::commandJson(body.result.value).at("more"));
    const auto end = run("");
    ASSERT_TRUE(end.result.ok()) << end.message;
    EXPECT_FALSE(reverse::commandJson(end.result.value).at("more"));
    EXPECT_TRUE(end.output.starts_with("0\n"));
    EXPECT_NE(end.output.find("\n1\n"), std::string::npos);
    EXPECT_NE(end.output.find("schemaVersion"), std::string::npos);
    EXPECT_TRUE(reverse::commandJson(run("if True:").result.value).at("more"));
    EXPECT_TRUE(finished(service, service.resetPythonConsole()).result.ok());
    EXPECT_EQ(run("print('reset')").output, "reset\n");
}
TEST(MixedScripts, PythonBlocksStringsAndNativeCommandsRecordOnlyExecutedBusinessCalls)
{
    CloudFixture file;
    const auto path = file.directory / "mixed.py";
    const auto recording = file.directory / "recorded.py";
    {
        std::ofstream script(path, std::ios::binary);
        script << "\xEF\xBB\xBF"
                  "print('开始')\n"
                  "literal = '''保留文字\ncloud.reset\n'''\n"
                  "values = (\n    'cloud.reset',\n    2\n)\n"
                  "def inspect_twice():\n    for i in range(2):\n        project.snapshot\n        print(i)\n"
                  "inspect_twice()\n"
                  "if False:\n    cloud.reset\n"
                  "cloud.import \"" << file.utf8() << "\"\n"
                  "cloud.select_indices --indices '[0,1]'\ncloud.crop\n"
                  "print(values[1], literal.startswith('保留文字'))\n";
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(recording)).result.ok());
    const auto task = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(task.result.ok()) << task.message;
    EXPECT_TRUE(task.output.starts_with("开始\n"));
    EXPECT_TRUE(task.output.ends_with("2 True\n"));
    const auto first = task.output.find("// project.snapshot\n");
    ASSERT_NE(first, std::string::npos);
    EXPECT_NE(task.output.find("// project.snapshot\n", first + 1), std::string::npos);
    EXPECT_EQ(task.output.find("// cloud.reset"), std::string::npos);
    ASSERT_TRUE(service.project().cloud);
    EXPECT_EQ(service.project().cloud->points.size(), 2);
    EXPECT_EQ(service.recordingState().commandCount, 5);
    EXPECT_EQ(readScript(recording).find("print("), std::string::npos);
    EXPECT_EQ(readScript(recording).find("cloud.reset"), std::string::npos);
    EXPECT_EQ(readScript(recording).find("rm.execute"), std::string::npos);
    EXPECT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}
TEST(MixedScripts, NativeParameterErrorKeepsLineAndCommittedPrefixWithoutTraceback)
{
    CloudFixture file;
    const auto path = file.directory / "mixed-error.py";
    {
        std::ofstream script(path);
        script << "print('before')\ncloud.import \"" << file.utf8()
               << "\"\nmodel.reconstruct --mirror wrong\nprint('after')\n";
    }
    reverse::CommandService service;
    const auto task = finished(service, service.submitScriptFile(path));
    EXPECT_EQ(task.errorCode, "invalid_value");
    EXPECT_TRUE(task.result.committed);
    EXPECT_TRUE(task.message.starts_with("第 3 行："));
    EXPECT_NE(task.message.find("--mirror"), std::string::npos);
    EXPECT_TRUE(task.output.ends_with("未完成：" + task.message + '\n'));
    EXPECT_EQ(task.output.find("after"), std::string::npos);
    EXPECT_EQ(task.output.find("Traceback"), std::string::npos);
    ASSERT_TRUE(service.project().cloud);
    EXPECT_EQ(service.project().cloud->points.size(), 8);
}
TEST(MixedScripts, PythonErrorsKeepLineAndInputCanBeAnsweredWithoutBlockingQueueMutex)
{
    CloudFixture file;
    const auto path = file.directory / "input.py";
    {
        std::ofstream script(path);
        script << "answer = input('名称: ')\nprint('收到', answer)\nproject.snapshot\n1 / 0\nprint('after')\n";
    }
    reverse::CommandService service;
    const auto id = service.submitScriptFile(path);
    const auto waiting = waitingForInput(service, id);
    EXPECT_NE(waiting.output.find("名称: "), std::string::npos);
    const auto queued = service.submitLine("print('next')");
    EXPECT_EQ(service.task(queued).state, reverse::TaskState::Queued);
    ASSERT_TRUE(service.provideInput(id, waiting.inputRequest, "中文"));
    const auto task = finished(service, id);
    EXPECT_EQ(task.errorCode, "python_error");
    EXPECT_TRUE(task.message.starts_with("第 4 行：ZeroDivisionError:"));
    EXPECT_NE(task.output.find("收到 中文\n"), std::string::npos);
    EXPECT_EQ(task.output.find("after"), std::string::npos);
    EXPECT_EQ(finished(service, queued).output, "next\n");
}
TEST(PythonConsole, ExpressionsPersistentNamespaceAndMultiline)
{
    reverse::CommandService service;
    const auto line = [&service](const std::string &source) {
        return finished(service, service.submitPythonLine(source));
    };
    const auto expression = line("6 * 7");
    ASSERT_TRUE(expression.result.ok()) << expression.message;
    EXPECT_EQ(expression.output, "42\n");
    EXPECT_FALSE(reverse::commandJson(expression.result.value).at("more"));
    EXPECT_EQ(line("_ + 1").output, "43\n");
    ASSERT_TRUE(line("total = 0").result.ok());
    const auto header = line("for i in range(3):");
    ASSERT_TRUE(header.result.ok()) << header.message;
    EXPECT_TRUE(reverse::commandJson(header.result.value).at("more"));
    const auto body = line("    total += i");
    ASSERT_TRUE(body.result.ok()) << body.message;
    EXPECT_TRUE(reverse::commandJson(body.result.value).at("more"));
    const auto end = line("");
    ASSERT_TRUE(end.result.ok()) << end.message;
    EXPECT_FALSE(reverse::commandJson(end.result.value).at("more"));
    EXPECT_EQ(line("total").output, "3\n");
    EXPECT_EQ(line("print('hello', end=''); print('世界')").output, "hello世界\n");
    EXPECT_EQ(expression.workerThread, end.workerThread);
}
TEST(PythonConsole, FailedOrAbortedBlocksDoNotPoisonNextCommand)
{
    reverse::CommandService service;
    const auto line = [&service](const std::string &source) {
        return finished(service, service.submitPythonLine(source));
    };
    EXPECT_TRUE(line("if True:").result.ok());
    EXPECT_EQ(line("!invalid").errorCode, "python_error");
    EXPECT_EQ(line("1 + 1").output, "2\n");
    EXPECT_EQ(line("1 / 0").errorCode, "python_error");
    EXPECT_TRUE(line("if True:").result.ok());
    EXPECT_TRUE(finished(service, service.resetPythonConsole()).result.ok());
    EXPECT_EQ(line("3 + 4").output, "7\n");
    EXPECT_TRUE(finished(service, service.submitPython("from_script = 9")).result.ok());
    EXPECT_EQ(line("from_script").output, "9\n");
}
TEST(PythonConsole, InputReleasesQueueMutexAndUsesVersionedResponses)
{
    reverse::CommandService service;
    const auto id = service.submitPythonLine("first = input('名字: '); second = input('次数: ')");
    const auto first = waitingForInput(service, id);
    EXPECT_NE(first.output.find("名字: "), std::string::npos);
    // These are the same non-waiting calls used by GUI polling and submission.
    const auto queued = service.submitLine("project.snapshot");
    EXPECT_EQ(service.task(queued).state, reverse::TaskState::Queued);
    EXPECT_FALSE(service.provideInput(id, first.inputRequest + 1, "stale"));
    EXPECT_FALSE(service.provideInput(queued, first.inputRequest, "wrong task"));
    EXPECT_FALSE(service.provideInput(id, first.inputRequest, std::string(1024 * 1024 + 1, 'x')));
    EXPECT_TRUE(service.provideInput(id, first.inputRequest, "中文回答"));
    const auto second = waitingForInput(service, id, first.inputRequest);
    EXPECT_GT(second.inputRequest, first.inputRequest);
    EXPECT_FALSE(service.provideInput(id, first.inputRequest, "duplicate"));
    EXPECT_TRUE(service.provideInput(id, second.inputRequest, "2"));
    const auto result = finished(service, id);
    ASSERT_TRUE(result.result.ok()) << result.message;
    EXPECT_FALSE(result.waitingForInput);
    EXPECT_EQ(result.workerThread, finished(service, queued).workerThread);
    const auto value = finished(service, service.submitPythonLine("print(first, int(second))"));
    EXPECT_EQ(value.output, "中文回答 2\n");
}
TEST(PythonConsole, InputSupportsEofCancellationAndSubsequentCommands)
{
    reverse::CommandService service;
    const auto eofId = service.submitPythonLine("input('EOF: ')");
    const auto eof = waitingForInput(service, eofId);
    ASSERT_TRUE(service.provideInput(eofId, eof.inputRequest, std::nullopt));
    const auto failure = finished(service, eofId);
    EXPECT_EQ(failure.errorCode, "python_error");
    EXPECT_NE(failure.message.find("EOFError"), std::string::npos);
    const auto cancelId = service.submitPython("answer = input('等待: ')");
    const auto cancel = waitingForInput(service, cancelId);
    EXPECT_TRUE(service.cancel(cancelId));
    EXPECT_FALSE(service.provideInput(cancelId, cancel.inputRequest, "late"));
    EXPECT_EQ(finished(service, cancelId).state, reverse::TaskState::Cancelled);
    EXPECT_EQ(finished(service, service.submitPythonLine("2 + 3")).output, "5\n");
}
TEST(PythonConsole, ShutdownWakesPendingInput)
{
    const auto start = std::chrono::steady_clock::now();
    {
        reverse::CommandService service;
        const auto id = service.submitPythonLine("input('waiting')");
        (void)waitingForInput(service, id);
    }
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(15));
}
TEST(PythonConsole, ShutdownRequestDoesNotWaitForRunningScript)
{
    reverse::CommandService service;
    const auto id = service.submitPython("import time\nprint('started')\ntime.sleep(0.7)\n");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (service.task(id).output.empty() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    ASSERT_FALSE(service.task(id).output.empty());
    const auto start = std::chrono::steady_clock::now();
    service.requestShutdown();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::milliseconds(200));
    EXPECT_THROW((void)service.submitLine("help"), std::exception);
    while (!service.shutdownComplete() && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    EXPECT_TRUE(service.shutdownComplete());
}
TEST(CommandFeedback, NativeMutationIsBriefQueryAndHelpRemainExplicit)
{
    CloudFixture file;
    reverse::CommandService service({{}, file.directory / "log"});
    const auto blank = finished(service, service.submitLine(" \t "));
    EXPECT_TRUE(reverse::commandFeedback(blank.result).empty());
    EXPECT_TRUE(blank.output.empty());
    const auto imported = finished(service, service.submitLine("cloud.import " + rm::formatValue(file.utf8())));
    ASSERT_TRUE(imported.result.ok()) << imported.message;
    EXPECT_EQ(reverse::commandFeedback(imported.result), "已导入点云，共 8 点。");
    EXPECT_NE(imported.output.find("原始点 8；无效点 0；当前点 8"), std::string::npos);
    EXPECT_EQ(imported.output.find("canUndoCloud"), std::string::npos);
    EXPECT_EQ(reverse::commandJson(imported.result.value).at("revision"), 1);
    const auto query = finished(service, service.submitLine("project.snapshot"));
    EXPECT_NE(reverse::commandFeedback(query.result).find("\"cloud\""), std::string::npos);
    const auto help = finished(service, service.submitLine("help cloud.import"));
    EXPECT_EQ(help.result.kind, rm::ResultKind::Help);
    EXPECT_NE(reverse::commandFeedback(help.result).find("用法"), std::string::npos);
    std::ifstream log(file.directory / "log/commands.log");
    const std::string contents((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("cloud.import [ok]"), std::string::npos);
    EXPECT_NE(contents.find(" ms"), std::string::npos);
}
TEST(CommandFeedback, NativeAndPythonParameterFailuresPreserveDetailsWithoutTraceback)
{
    reverse::CommandService service;
    const auto native = finished(service, service.submitLine("model.reconstruct --mirror wrong"));
    EXPECT_EQ(native.errorCode, "invalid_value");
    EXPECT_NE(native.message.find("--mirror"), std::string::npos);
    EXPECT_NE(native.message.find("true|false"), std::string::npos);
    EXPECT_EQ(native.message.find("用法"), std::string::npos);
    EXPECT_EQ(native.output, "// model.reconstruct --mirror wrong\n");
    const auto python = finished(service, service.submitPythonLine("rm.execute('model.reconstruct', mirror='wrong')"));
    EXPECT_EQ(python.errorCode, "invalid_value");
    EXPECT_NE(python.message.find("mirror"), std::string::npos);
    EXPECT_NE(python.message.find("wrong"), std::string::npos);
    EXPECT_EQ(python.message.find("\n"), std::string::npos);
    EXPECT_EQ(reverse::commandJson(python.result.value).at("parameter"), "mirror");
    EXPECT_TRUE(python.output.empty());
    EXPECT_EQ(service.project().revision, 0);
}
TEST(CommandFeedback, PythonReplSummarizesDirectCallsWithoutChangingScriptValues)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto expression = "rm.execute('cloud.import', " + rm::formatValue(file.utf8()) + ")";
    const auto direct = finished(service, service.submitPythonLine(expression));
    ASSERT_TRUE(direct.result.ok()) << direct.message;
    EXPECT_TRUE(direct.output.ends_with("已导入点云，共 8 点。\n"));
    EXPECT_NE(direct.output.find("源文件："), std::string::npos);
    EXPECT_EQ(finished(service, service.submitPythonLine("_['revision']")).output, "1\n");
    const auto assignment = finished(service, service.submitPythonLine("result = " + expression));
    ASSERT_TRUE(assignment.result.ok()) << assignment.message;
    EXPECT_NE(assignment.output.find("源文件："), std::string::npos);
    EXPECT_EQ(assignment.output.find("canUndoCloud"), std::string::npos);
    EXPECT_EQ(finished(service, service.submitPythonLine("type(result) is dict")).output, "True\n");
    const auto explicitValue = finished(service, service.submitPythonLine("result"));
    EXPECT_NE(explicitValue.output.find("'cloud':"), std::string::npos);
    const auto help = finished(service, service.submitPythonLine("rm.help('cloud.import')"));
    EXPECT_TRUE(help.output.starts_with("cloud.import"));
    EXPECT_NE(help.output.find("\n用法:"), std::string::npos);
    EXPECT_EQ(finished(service, service.submitPythonLine("2 + 3")).output, "5\n");
}
TEST(CommandFeedback, PythonConversionErrorsNameNestedOrPositionalParameters)
{
    reverse::CommandService service;
    const auto nested =
        finished(service, service.submitPythonLine("rm.execute('cloud.select_indices', indices=[1, object()])"));
    EXPECT_EQ(nested.errorCode, "invalid_value");
    EXPECT_EQ(reverse::commandJson(nested.result.value).at("parameter"), "indices[1]");
    const auto path = finished(service, service.submitPythonLine("rm.execute('cloud.import', object())"));
    EXPECT_EQ(path.errorCode, "invalid_value");
    EXPECT_EQ(reverse::commandJson(path.result.value).at("parameter"), "path");
    const auto overflow =
        finished(service, service.submitPythonLine("rm.execute('model.reconstruct', end_tolerance=2**100)"));
    EXPECT_EQ(overflow.errorCode, "invalid_value");
    EXPECT_EQ(reverse::commandJson(overflow.result.value).at("parameter"), "end_tolerance");
    EXPECT_NE(overflow.message.find("64"), std::string::npos);
    const auto custom = finished(service, service.submitPythonLine("raise rm.CommandError('custom')"));
    EXPECT_EQ(custom.errorCode, "python_error");
    EXPECT_EQ(custom.message, "CommandError: custom");
}
TEST(CommandFeedback, PythonErrorsKeepInternalFramesOnlyInDiagnosticLog)
{
    CloudFixture file;
    reverse::CommandService service({{}, file.directory / "log"});
    const auto syntax = finished(service, service.submitPythonLine("rm.help cloud.reset"));
    EXPECT_EQ(syntax.errorCode, "python_error");
    EXPECT_TRUE(syntax.message.starts_with("SyntaxError:"));
    EXPECT_EQ(syntax.message.find("codeop.py"), std::string::npos);
    const auto runtime = finished(service, service.submitPythonLine("1 / 0"));
    EXPECT_EQ(runtime.message, "ZeroDivisionError: division by zero");
    std::ifstream log(file.directory / "log/commands.log");
    const std::string contents((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    EXPECT_NE(contents.find("codeop.py"), std::string::npos);
    EXPECT_NE(contents.find("ZeroDivisionError"), std::string::npos);
}

TEST(CloudImportCommands, UiAndStartupImportsRecordOnlyPathAndReplayInMillimeters)
{
    CloudFixture file;
    for (const bool startup : {false, true})
    {
        reverse::CommandService service;
        const auto recording = file.directory / (startup ? "startup-mm.py" : "ui-mm.py");
        ASSERT_TRUE(finished(service, service.submitStartRecording(recording)).result.ok());
        const auto task = finished(service, startup ? service.submitStartupImport(file.path)
                                                   : service.submitImport(file.path));
        ASSERT_TRUE(task.result.ok()) << task.message;
        const auto cloud = service.project().cloud;
        ASSERT_TRUE(cloud);
        EXPECT_EQ(cloud->sourceUnit, reverse::LengthUnit::Millimeter);
        EXPECT_DOUBLE_EQ(cloud->sourceScaleToMm, 1);
        EXPECT_DOUBLE_EQ(cloud->points.back().position.x + cloud->originMm.x, 100);
        bool sawImport = false;
        for (const auto &event : completedUiCommands(service))
        {
            if (!event.commandLine.starts_with("cloud.import ")) continue;
            sawImport = true;
            EXPECT_EQ(event.commandLine.find("--unit"), std::string::npos);
        }
        EXPECT_TRUE(sawImport);
        ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
        std::ifstream input(recording);
        const std::string contents((std::istreambuf_iterator<char>(input)), {});
        EXPECT_NE(contents.find("cloud.import "), std::string::npos);
        EXPECT_EQ(contents.find("--unit"), std::string::npos);
        reverse::CommandService replay;
        const auto result = finished(replay, replay.submitScriptFile(recording));
        ASSERT_TRUE(result.result.ok()) << result.message;
        ASSERT_TRUE(replay.project().cloud);
        EXPECT_EQ(replay.project().cloud->sourceUnit, reverse::LengthUnit::Millimeter);
        EXPECT_DOUBLE_EQ(replay.project().cloud->sourceScaleToMm, 1);
        EXPECT_DOUBLE_EQ(replay.project().cloud->points.back().position.x + replay.project().cloud->originMm.x, 100);
    }
}

TEST(StartupExecution, NativeArgvKeepsChinesePathAndCommandOptions)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto imported = finished(service, service.submitCommandArguments({"cloud.import", file.utf8()}));
    ASSERT_TRUE(imported.result.ok()) << imported.message;
    ASSERT_NE(service.project().cloud, nullptr);
    EXPECT_EQ(service.project().cloud->points.size(), 8);
    EXPECT_TRUE(imported.output.starts_with(reverse::commandExecutionEcho(
        reverse::commandArgumentsText({"cloud.import", file.utf8()}))));
    EXPECT_EQ(finished(service, service.submitCommandArguments({"cloud.import", "--help"})).result.kind, rm::ResultKind::Help);
    const auto invalid = finished(service, service.submitCommandArguments({"cloud.import", file.utf8(), "--unit", "wrong"}));
    EXPECT_EQ(invalid.errorCode, "unknown_option");
    EXPECT_NE(invalid.message.find("unit"), std::string::npos);
    const auto python = finished(service, service.submitCommandArguments({"print('startup python')"}));
    ASSERT_TRUE(python.result.ok()) << python.message;
    EXPECT_EQ(python.output, "startup python\n");
    const auto line = finished(service, service.submitCommandArguments({"help cloud.import"}));
    EXPECT_EQ(line.result.kind, rm::ResultKind::Help);
    EXPECT_EQ(imported.workerThread, python.workerThread);
}
TEST(ScriptArguments, ArgvAndFileAreIsolatedPerRunAndOnFailure)
{
    CloudFixture file;
    const auto path = file.directory / std::filesystem::path(u8"参数 脚本.py");
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitPython(
        "import sys\noriginal_argv = sys.argv\n__file__ = 'console-value'\n")).result.ok());
    {
        std::ofstream script(path, std::ios::binary);
        script << "import sys, json\n"
                  "assert __file__ == sys.argv[0]\n"
                  "print(json.dumps(sys.argv, ensure_ascii=False))\n"
                  "sys.argv = ['changed']\n__file__ = 'changed'\n";
    }
    const std::vector<std::string> arguments{"aa", "中文 空格", "", "--help", "-f", "-platform", "offscreen", "-2", "a'\"b", "D:\\末尾\\"};
    const auto run = finished(service, service.submitScriptFile(path, arguments));
    ASSERT_TRUE(run.result.ok()) << run.message;
    const auto parsed = nlohmann::json::parse(run.output);
    const auto utf8 = std::filesystem::absolute(path).u8string();
    const std::string expectedPath(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    EXPECT_EQ(parsed.at(0), expectedPath);
    for (std::size_t i = 0; i < arguments.size(); ++i) EXPECT_EQ(parsed.at(i + 1), arguments[i]);
    EXPECT_TRUE(finished(service, service.submitPython(
        "assert sys.argv is original_argv\nassert __file__ == 'console-value'\n")).result.ok());
    const auto noArgs = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(noArgs.result.ok()) << noArgs.message;
    EXPECT_EQ(nlohmann::json::parse(noArgs.output).size(), 1);
    {
        std::ofstream script(path, std::ios::binary);
        script << "import sys\nsys.argv = []\n__file__ = 'changed'\n1 / 0\n";
    }
    const auto failure = finished(service, service.submitScriptFile(path, {"x"}));
    EXPECT_EQ(failure.errorCode, "python_error");
    EXPECT_TRUE(failure.message.starts_with("第 4 行：ZeroDivisionError:")) << failure.message;
    EXPECT_TRUE(finished(service, service.submitPython(
        "assert sys.argv is original_argv\nassert __file__ == 'console-value'\n")).result.ok());
    EXPECT_EQ(finished(service, service.submitScriptFile(file.directory / "missing.py", {"x"})).errorCode, "script_open_failed");
}
TEST(ScriptArguments, MixedCommandsInputAndCancellationKeepArgumentScope)
{
    CloudFixture file;
    const auto path = file.directory / "interactive.py";
    {
        std::ofstream script(path);
        script << "import sys\nprint(sys.argv[1])\nproject.snapshot\ninput('continue: ')\n";
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitPython("import sys\noriginal_argv = sys.argv\n")).result.ok());
    const auto id = service.submitScriptFile(path, {"argument-value"});
    const auto waiting = waitingForInput(service, id);
    EXPECT_TRUE(waiting.output.starts_with("argument-value\n"));
    EXPECT_NE(waiting.output.find("// project.snapshot"), std::string::npos);
    ASSERT_TRUE(service.cancel(id));
    EXPECT_EQ(finished(service, id).state, reverse::TaskState::Cancelled);
    EXPECT_TRUE(finished(service, service.submitPython(
        "assert sys.argv is original_argv\nassert '__file__' not in globals()\n")).result.ok());
}

TEST(CommandTranscript, UiStartDetailsAndResultFollowWorkerOrder)
{
    CloudFixture file;
    reverse::CommandService service;
    const auto imported = service.submitImport(file.path);
    ASSERT_TRUE(finished(service, imported).result.ok());
    const auto exported = service.submitExportCloud(file.directory / "copy.pcd", service.project().revision);
    ASSERT_TRUE(finished(service, exported).result.ok());
    const auto events = service.takeUiCommandEvents();
    ASSERT_EQ(events.size(), 6u);
    for (std::size_t offset : {0u, 3u})
    {
        EXPECT_EQ(events[offset].kind, reverse::UiCommandEventKind::Started);
        EXPECT_EQ(events[offset + 1].kind, reverse::UiCommandEventKind::Output);
        EXPECT_EQ(events[offset + 2].kind, reverse::UiCommandEventKind::Finished);
        EXPECT_EQ(events[offset].taskId, events[offset + 1].taskId);
        EXPECT_EQ(events[offset].taskId, events[offset + 2].taskId);
        EXPECT_TRUE(events[offset + 2].succeeded);
    }
    EXPECT_EQ(events.front().taskId, imported);
    EXPECT_EQ(events.back().taskId, exported);
    EXPECT_NE(events[1].message.find(file.utf8()), std::string::npos);
    EXPECT_NE(events[1].message.find("原始点 8"), std::string::npos);
    EXPECT_NE(events[4].message.find("PCD 导出：8 点"), std::string::npos);
    EXPECT_NE(events[4].message.find("copy.pcd"), std::string::npos);
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
}

TEST(CommandTranscript, NativeAndMixedScriptsKeepEachCommandsDetailsTogether)
{
    CloudFixture file;
    for (const bool python : {false, true})
    {
        const auto script = file.directory / (python ? "mixed.py" : "native.py");
        std::ofstream source(script, std::ios::binary);
        if (python) source << "print('before')\n";
        source << "cloud.import " << rm::formatValue(file.utf8()) << "\n"
               << "cloud.export " << rm::formatValue((file.directory / (python ? "mixed.pcd" : "native.pcd")).string())
               << "\nmodel.reconstruct --mirror wrong\ncloud.reset\n";
        source.close();
        reverse::CommandService service;
        const auto result = finished(service, service.submitScriptFile(script));
        ASSERT_FALSE(result.result.ok());
        const auto &output = result.output;
        const auto importCommand = output.find("// cloud.import");
        const auto importLog = output.find("原始点 8");
        const auto importDone = output.find("已导入点云，共 8 点。");
        const auto exportCommand = output.find("// cloud.export");
        const auto exportLog = output.find("PCD 导出：8 点");
        const auto exportDone = output.find("点云已导出。");
        const auto failed = output.find("// model.reconstruct");
        ASSERT_NE(failed, std::string::npos);
        EXPECT_LT(importCommand, importLog);
        EXPECT_LT(importLog, importDone);
        EXPECT_LT(importDone, exportCommand);
        EXPECT_LT(exportCommand, exportLog);
        EXPECT_LT(exportLog, exportDone);
        EXPECT_LT(exportDone, failed);
        EXPECT_EQ(output.find("// cloud.reset"), std::string::npos);
        EXPECT_EQ(output.find("canUndoCloud"), std::string::npos);
        EXPECT_TRUE(output.ends_with("未完成：" + result.message + '\n'));
    }
}

TEST(CommandTranscript, ReconstructionWarningsDeviationAndStepReadbackAreIncluded)
{
    CloudFixture file;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    const auto model = finished(service, service.submitReconstruct(parameters, service.project().revision));
    ASSERT_TRUE(model.result.ok()) << model.message;
    EXPECT_NE(model.output.find("手动参数模型"), std::string::npos);
    for (const auto &warning : service.project().model->warnings)
        EXPECT_NE(model.output.find(warning), std::string::npos);
    const auto deviation = finished(service, service.submitDeviation(service.project().revision));
    ASSERT_TRUE(deviation.result.ok()) << deviation.message;
    EXPECT_NE(deviation.output.find("偏差（mm）：均值"), std::string::npos);
    EXPECT_NE(deviation.output.find("RMS"), std::string::npos);
    EXPECT_NE(deviation.output.find("P95"), std::string::npos);
    const auto exported = finished(service, service.submitExport(file.directory / "model.step", service.project().revision));
    ASSERT_TRUE(exported.result.ok()) << exported.message;
    EXPECT_NE(exported.output.find("STEP 回读："), std::string::npos);
    EXPECT_NE(exported.output.find("model.step"), std::string::npos);
}

TEST(CommandTranscript, UiAndTypedReconstructionHaveIdenticalInputExecutionAndResult)
{
    CloudFixture file;
    std::string command;
    std::string uiTranscript;
    {
        reverse::CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
        (void)service.takeUiCommandEvents();
        const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
        const auto built = finished(service, service.submitReconstruct(parameters, service.project().revision));
        ASSERT_TRUE(built.result.ok()) << built.message;
        const auto events = service.takeUiCommandEvents();
        ASSERT_EQ(events.size(), 3u);
        ASSERT_EQ(events.front().kind, reverse::UiCommandEventKind::Started);
        command = events.front().commandLine;
        uiTranscript = reverse::commandExecutionEcho(command, true);
        for (const auto &event : events)
        {
            if (event.kind == reverse::UiCommandEventKind::Output) uiTranscript += event.message;
            if (event.kind == reverse::UiCommandEventKind::Finished) uiTranscript += event.message + '\n';
        }
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    const auto typed = finished(service, service.submitLine(command));
    ASSERT_TRUE(typed.result.ok()) << typed.message;
    const auto typedTranscript = "rm> " + command + '\n' + typed.output + reverse::commandFeedback(typed.result) + '\n';
    EXPECT_EQ(uiTranscript, typedTranscript);
    EXPECT_TRUE(typedTranscript.starts_with("rm> " + command + "\n// " + command + '\n'));
    EXPECT_EQ(typedTranscript.find("// " + command, typedTranscript.find("// " + command) + 1), std::string::npos);
}

TEST(ReferencePlaneCommands, ValidateAndCommitWithoutChangingGeometry)
{
    CloudFixture cloud;
    reverse::CommandService service;
    ASSERT_EQ(finished(service, service.submitImport(cloud.path)).state,
              reverse::TaskState::Succeeded);
    const auto before = service.project();
    auto result = finished(service, service.submitLine("project.set_reference_plane --points '[[0,0,0],[10,0,0],[0,10,0]]'"));
    ASSERT_EQ(result.state, reverse::TaskState::Succeeded) << result.message;
    const auto plane = service.project().referencePlane;
    ASSERT_TRUE(plane);
    EXPECT_EQ(before.cloud, service.project().cloud);
    EXPECT_EQ(before.model, service.project().model);
    EXPECT_EQ(before.cloudId, service.project().cloudId);
    EXPECT_EQ(service.project().revision, before.revision + 1);
    result = finished(service, service.submitLine("project.set_reference_plane --points '[[0,0,0],[10,0,0],[20,0,0]]'"));
    EXPECT_EQ(result.state, reverse::TaskState::Failed);
    EXPECT_EQ(result.errorCode, "invalid_plane");
    EXPECT_EQ(service.project().referencePlane, plane);
    result = finished(service, service.submitLine("project.set_reference_plane --points '[[0,0],[1,0,0],[0,1,0]]'"));
    EXPECT_EQ(result.state, reverse::TaskState::Failed);
    EXPECT_EQ(service.project().referencePlane, plane);
    result = finished(service, service.submitLine("project.section --enabled true --offset 12.5 --reverse true"));
    ASSERT_EQ(result.state, reverse::TaskState::Succeeded) << result.message;
    EXPECT_TRUE(service.project().section.enabled);
    EXPECT_DOUBLE_EQ(service.project().section.offsetMm, 12.5);
    EXPECT_TRUE(service.project().section.reversed);
    EXPECT_EQ(before.cloud, service.project().cloud);
    ASSERT_EQ(finished(service, service.submitLine("project.clear_reference_plane")).state, reverse::TaskState::Succeeded);
    EXPECT_FALSE(service.project().referencePlane);
    EXPECT_FALSE(service.project().section.enabled);
    EXPECT_EQ(finished(service, service.submitLine("project.section --enabled true")).errorCode, "no_reference_plane");
}

TEST(ReferencePlaneCommands, RecordingReplayAndImportReset)
{
    CloudFixture cloud;
    reverse::CommandService service;
    ASSERT_EQ(finished(service, service.submitImport(cloud.path)).state,
              reverse::TaskState::Succeeded);
    const auto recording = cloud.directory / "plane.py";
    ASSERT_EQ(finished(service, service.submitStartRecording(recording)).state, reverse::TaskState::Succeeded);
    const std::array<reverse::Vec3, 3> points{reverse::Vec3{1000, 20, 30}, {1100, 20, 30}, {1000, 120, 30}};
    ASSERT_EQ(finished(service, service.submitReferencePlane(points, service.project().revision)).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitSection({true, -3, true}, service.project().revision)).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitStopRecording()).state, reverse::TaskState::Succeeded);
    std::ifstream input(recording);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(contents.find("project.set_reference_plane --points"), std::string::npos);
    EXPECT_NE(contents.find("project.section --enabled true"), std::string::npos);
    ASSERT_EQ(finished(service, service.submitImport(cloud.path)).state,
              reverse::TaskState::Succeeded);
    EXPECT_FALSE(service.project().referencePlane);
    EXPECT_FALSE(service.project().section.enabled);
    const auto replay = finished(service, service.submitScriptFile(recording));
    ASSERT_EQ(replay.state, reverse::TaskState::Succeeded) << replay.message;
    ASSERT_TRUE(service.project().referencePlane);
    EXPECT_DOUBLE_EQ(service.project().referencePlane->pointsMm[0].x, 1000);
    EXPECT_DOUBLE_EQ(service.project().section.offsetMm, -3);
    EXPECT_TRUE(service.project().section.reversed);
}

TEST(ReferencePlaneCommands, BottomSectionRecordsReplaysAndDisablesWhenBottomIsDeleted)
{
    CloudFixture cloud;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(cloud.path)).result.ok());
    const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    ASSERT_TRUE(finished(service, service.submitReconstruct(parameters, service.project().revision)).result.ok());
    const auto before = service.project();
    const auto recording = cloud.directory / "bottom-section.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(recording)).result.ok());
    const reverse::PlaneSection section{true, 2.5, true, reverse::SectionPlaneSource::Bottom};
    const auto enabled = finished(service, service.submitSection(section, service.project().revision));
    ASSERT_TRUE(enabled.result.ok()) << enabled.message;
    EXPECT_EQ(service.project().section, section);
    EXPECT_EQ(service.project().cloud, before.cloud);
    EXPECT_EQ(service.project().model, before.model);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    std::ifstream input(recording);
    const std::string contents((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(contents.find("--plane bottom"), std::string::npos);
    ASSERT_TRUE(finished(service, service.submitLine("project.section --enabled false --plane bottom")).result.ok());
    ASSERT_TRUE(finished(service, service.submitScriptFile(recording)).result.ok());
    EXPECT_EQ(service.project().section, section);
    ASSERT_TRUE(finished(service, service.submitDeleteModelParts({"web"}, service.project().revision)).result.ok());
    EXPECT_FALSE(service.project().section.enabled);
    const auto invalid = finished(service, service.submitLine("project.section --enabled true --plane bottom"));
    EXPECT_EQ(invalid.errorCode, "no_bottom_plane");
    EXPECT_FALSE(service.project().section.enabled);
    EXPECT_TRUE(finished(service, service.submitLine("project.section --enabled false --plane bottom")).result.ok());
}

TEST(ReferencePlaneCommands, LegacyUndoUsesUnifiedPlaneAndGeometryHistory)
{
    CloudFixture cloud;
    reverse::CommandService service;
    ASSERT_EQ(finished(service, service.submitImport(cloud.path)).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitLine("cloud.crop --indices '[0,1,2,3]' ")).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitLine("project.set_reference_plane --points '[[0,0,0],[10,0,0],[0,10,0]]' ")).state,
              reverse::TaskState::Succeeded);
    const auto plane = service.project().referencePlane;
    ASSERT_EQ(finished(service, service.submitLine("cloud.undo")).state, reverse::TaskState::Succeeded);
    EXPECT_FALSE(service.project().referencePlane);
    EXPECT_EQ(service.project().cloud->points.size(), 4);
    ASSERT_EQ(finished(service, service.submitLine("project.undo")).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(service.project().cloud->points.size(), 8);
    ASSERT_EQ(finished(service, service.submitLine("project.redo")).state, reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitLine("project.redo")).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(service.project().referencePlane, plane);
}

TEST(ModelDeleteCommands, RecordsExplicitIdsReplaysAndExportsOnlyRemainingGeometry)
{
    CloudFixture file;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    ASSERT_TRUE(finished(service, service.submitReconstruct(parameters, service.project().revision)).result.ok());
    ASSERT_TRUE(finished(service, service.submitLine(
        "project.set_reference_plane --points '[[0,0,0],[10,0,0],[0,10,0]]'")).result.ok());
    ASSERT_TRUE(finished(service, service.submitDeviation(service.project().revision)).result.ok());
    ASSERT_TRUE(finished(service, service.submitExport(file.directory / "before.step", service.project().revision)).result.ok());
    const auto before = service.project();
    ASSERT_TRUE(before.deviation);
    ASSERT_TRUE(before.lastExport);
    const auto recording = file.directory / "delete.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(recording)).result.ok());
    const auto result = finished(service, service.submitDeleteModelParts({"web", "rib-1-right"}, before.revision));
    ASSERT_TRUE(result.result.ok()) << result.message;
    const auto after = service.project();
    ASSERT_TRUE(after.model);
    EXPECT_EQ(after.model->solidCount, 5u);
    EXPECT_EQ(after.cloud, before.cloud);
    EXPECT_EQ(after.recognition.has_value(), before.recognition.has_value());
    EXPECT_EQ(after.referencePlane, before.referencePlane);
    EXPECT_FALSE(after.deviation);
    EXPECT_FALSE(after.lastExport);
    EXPECT_TRUE(after.exportedPathUtf8.empty());
    EXPECT_NE(after.modelId, before.modelId);
    EXPECT_EQ(before.model->solidCount, 7u);
    EXPECT_NE(readScript(recording).find("model.delete --parts"), std::string::npos);
    EXPECT_NE(readScript(recording).find("rib-1-right"), std::string::npos);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    const auto path = file.directory / std::filesystem::path(u8"删除后 模型.step");
    ASSERT_TRUE(finished(service, service.submitExport(path, after.revision)).result.ok());
    const auto imported = reverse::loadCadModel(path);
    EXPECT_EQ(imported->summary().solidCount, 5u);
    EXPECT_NEAR(imported->summary().volumeMm3, after.model->volumeMm3, 1e-4);
    // 重建后回放，不需要当前选择、视图或模型版本；部件 ID 由原建模过程稳定生成。
    ASSERT_TRUE(finished(service, service.submitReconstruct(parameters, service.project().revision)).result.ok());
    ASSERT_TRUE(finished(service, service.submitScriptFile(recording)).result.ok());
    EXPECT_EQ(service.project().model->partIds, after.model->partIds);
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, after.model->volumeMm3);
    // STEP 导入模型使用同一删除路径，全部删除后保留源文件但不可导出空模型。
    ASSERT_TRUE(finished(service, service.submitImport(path)).result.ok());
    const auto ids = service.project().model->partIds;
    ASSERT_TRUE(finished(service, service.submitDeleteModelParts({ids.front()}, service.project().revision)).result.ok());
    EXPECT_EQ(service.project().model->solidCount, 4u);
    ASSERT_TRUE(finished(service, service.submitDeleteModelParts(service.project().model->partIds,
        service.project().revision)).result.ok());
    EXPECT_FALSE(service.project().model);
    EXPECT_FALSE(service.project().mesh);
    EXPECT_TRUE(service.project().modelId.empty());
    EXPECT_TRUE(std::filesystem::exists(path));
    EXPECT_EQ(finished(service, service.submitExport(file.directory / "empty.step",
        service.project().revision)).errorCode, "no_model");
}

TEST(ModelDeleteCommands, ValidatesArgumentsRevisionAndMissingPartsAtomically)
{
    CloudFixture file;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(file.path)).result.ok());
    ASSERT_TRUE(finished(service, service.submitReconstruct({100, 40, 60, 4, 5, 3, {}, false, false},
        service.project().revision)).result.ok());
    const auto before = service.project();
    for (const auto *line : {"model.delete", "model.delete --parts '[]'", "model.delete --parts '[12]'",
                            "model.delete --parts '[\"web\",\"missing\"]'"})
    {
        const auto failed = finished(service, service.submitLine(line));
        EXPECT_FALSE(failed.result.ok()) << line;
        EXPECT_EQ(service.project().revision, before.revision);
        EXPECT_EQ(service.project().model, before.model);
    }
    EXPECT_EQ(finished(service, service.submitDeleteModelParts({"web"}, before.revision - 1)).errorCode,
              "stale_revision");
    EXPECT_EQ(service.project().model, before.model);
    const auto help = finished(service, service.submitLine("model.delete --help"));
    EXPECT_EQ(help.result.kind, rm::ResultKind::Help);
    EXPECT_NE(help.result.message.find("--parts"), std::string::npos);
    ASSERT_TRUE(finished(service, service.submitDeleteModelParts(before.model->partIds, before.revision)).result.ok());
    EXPECT_EQ(service.project().cloud, before.cloud);
    EXPECT_FALSE(service.project().model);
}

TEST(EdgeTreatmentCommands, PreviewIsTransientAndApplicationsRecordReplayUndoRedo)
{
    CloudFixture files;
    const auto script = files.directory / "edges.py";
    reverse::CommandService service;
    ASSERT_EQ(finished(service, service.submitImport(files.path)).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitReconstruct({100, 40, 60, 4, 5, 3, {25}, false, false},
                                                          service.project().revision))
                  .state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finished(service, service.submitStartRecording(script)).state, reverse::TaskState::Succeeded);
    const auto original = service.project();
    reverse::EdgeTreatment request{{{"web", {1}}}, reverse::EdgeTreatmentKind::Fillet, 1};
    const auto previewId = service.submitEdgePreview(request, original.revision);
    const auto previewTask = finished(service, previewId);
    ASSERT_EQ(previewTask.state, reverse::TaskState::Succeeded) << previewTask.message;
    ASSERT_TRUE(service.takeEdgePreview(previewId));
    EXPECT_FALSE(service.takeEdgePreview(previewId));
    EXPECT_EQ(service.project().revision, original.revision);
    EXPECT_EQ(service.project().historySteps, original.historySteps);
    EXPECT_EQ(service.project().model, original.model);
    const auto applied = finished(service, service.submitEdgeTreatment(request, original.revision));
    ASSERT_TRUE(applied.result.ok()) << applied.message;
    const auto first = service.project();
    EXPECT_LT(first.model->volumeMm3, original.model->volumeMm3);
    ASSERT_TRUE(finished(service, service.submitLine("project.undo")).result.ok());
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, original.model->volumeMm3);
    auto edited = finished(service, service.submitLine("model.fillet --part web --edges '[1]' --size 0.5"));
    ASSERT_TRUE(edited.result.ok()) << edited.message;
    const auto second = service.project();
    EXPECT_GT(second.model->volumeMm3, first.model->volumeMm3);
    ASSERT_TRUE(finished(service, service.submitLine("project.undo")).result.ok());
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, original.model->volumeMm3);
    const auto failed = finished(service, service.submitLine("model.fillet --part web --edges '[1]' --size 100000"));
    EXPECT_FALSE(failed.result.ok());
    EXPECT_TRUE(service.project().canRedo);
    ASSERT_TRUE(finished(service, service.submitLine("project.redo")).result.ok());
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, second.model->volumeMm3);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    std::ifstream file(script);
    const std::string content((std::istreambuf_iterator<char>(file)), {});
    EXPECT_NE(content.find("model.fillet"), std::string::npos);
    EXPECT_EQ(content.find("--feature"), std::string::npos);
    EXPECT_EQ(content.find("EdgePreview"), std::string::npos);
    EXPECT_EQ(content.find("100000"), std::string::npos);
    reverse::CommandService replay;
    const auto result = finished(replay, replay.submitScriptFile(script));
    ASSERT_TRUE(result.result.ok()) << result.message;
    ASSERT_TRUE(replay.project().model);
    EXPECT_NEAR(replay.project().model->volumeMm3, second.model->volumeMm3, 1e-6);
    EXPECT_EQ(replay.project().model->edgeTreatedParts, second.model->edgeTreatedParts);
}

TEST(EdgeTreatmentCommands, CrossPartPreviewApplyFailureUndoAndReplayAreAtomic)
{
    CloudFixture files;
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitImport(files.path)).result.ok());
    ASSERT_TRUE(finished(service, service.submitReconstruct({1000, 186.494, 627.8323, 6, 12, 8, {}, false, true},
        service.project().revision)).result.ok());
    const auto script = files.directory / "batch-edges.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(script)).result.ok());
    const auto original = service.project();
    const auto initialCount = service.recordingState().commandCount;
    reverse::EdgeTreatment request{{{"flange-bottom", {1}}, {"flange-top", {1}}}, reverse::EdgeTreatmentKind::Fillet, 20};
    const auto previewId = service.submitEdgePreview(request, original.revision);
    ASSERT_TRUE(finished(service, previewId).result.ok());
    const auto preview = service.takeEdgePreview(previewId);
    ASSERT_TRUE(preview);
    EXPECT_EQ(service.project().model, original.model);
    EXPECT_EQ(service.recordingState().commandCount, initialCount);
    const auto applied = finished(service, service.submitEdgeTreatment(request, original.revision));
    ASSERT_TRUE(applied.result.ok()) << applied.message;
    const auto edited = service.project();
    EXPECT_EQ(edited.historySteps, original.historySteps + 1);
    EXPECT_EQ(service.recordingState().commandCount, initialCount + 1);
    EXPECT_DOUBLE_EQ(edited.model->volumeMm3, preview->summary().volumeMm3);
    EXPECT_EQ(edited.model->edgeTreatedParts, (std::vector<std::string>{"flange-bottom", "flange-top"}));
    ASSERT_TRUE(finished(service, service.submitLine("project.undo")).result.ok());
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, original.model->volumeMm3);
    EXPECT_TRUE(service.project().model->edgeTreatedParts.empty());
    const auto beforeFailure = service.project();
    const auto countBeforeFailure = service.recordingState().commandCount;
    request.targets[1].edges = {99999};
    const auto failed = finished(service, service.submitEdgeTreatment(request, beforeFailure.revision));
    EXPECT_FALSE(failed.result.ok());
    EXPECT_NE(failed.message.find("flange-top"), std::string::npos);
    EXPECT_EQ(service.project().model, beforeFailure.model);
    EXPECT_EQ(service.project().revision, beforeFailure.revision);
    EXPECT_EQ(service.recordingState().commandCount, countBeforeFailure);
    EXPECT_TRUE(service.project().canRedo);
    ASSERT_TRUE(finished(service, service.submitLine("project.redo")).result.ok());
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
    std::ifstream file(script);
    const std::string content((std::istreambuf_iterator<char>(file)), {});
    EXPECT_NE(content.find("--targets"), std::string::npos);
    EXPECT_EQ(content.find("EdgePreview"), std::string::npos);
    EXPECT_EQ(content.find("99999"), std::string::npos);
    reverse::CommandService replay;
    const auto result = finished(replay, replay.submitScriptFile(script));
    ASSERT_TRUE(result.result.ok()) << result.message;
    ASSERT_TRUE(replay.project().model);
    EXPECT_NEAR(replay.project().model->volumeMm3, edited.model->volumeMm3, 1e-6);
    EXPECT_EQ(replay.project().model->edgeTreatedParts, edited.model->edgeTreatedParts);
}

TEST(EdgeTreatmentCommands, RejectsIncompleteConflictingAndMalformedTargets)
{
    reverse::CommandService service;
    for (const auto *command : {
        "model.fillet --size 1",
        "model.fillet --part web --size 1",
        "model.fillet --edges '[1]' --size 1",
        "model.fillet --targets '[]' --size 1",
        "model.fillet --targets '[{\"part\":\"web\"}]' --size 1",
        "model.fillet --targets '[{\"part\":\"web\",\"edges\":[0]}]' --size 1",
        "model.fillet --targets '[{\"part\":\"web\",\"edges\":[1]}]' --part web --size 1"})
    {
        const auto result = finished(service, service.submitLine(command));
        EXPECT_FALSE(result.result.ok()) << command;
        EXPECT_NE(result.result.code, "no_model") << result.message;
        EXPECT_EQ(service.project().revision, 0);
    }
}


TEST(UiOperationFeedback, InternalFailuresIncludeOperationAndReasonOnceWithoutRecording)
{
    using namespace reverse;
    CloudFixture files;
    CommandService service;
    const auto script = files.directory / "failure.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(script)).result.ok());
    const auto before = readScript(script);
    const std::vector<std::pair<std::string, std::function<std::string()>>> previews{
        {"圆角预览", [&] { return service.submitEdgePreview({}, 0); }},
        {"倒角预览", [&] { EdgeTreatment r; r.kind = EdgeTreatmentKind::Chamfer; return service.submitEdgePreview(r, 0); }},
        {"修改板厚预览", [&] { return service.submitThicknessPreview({}, 0); }},
        {"外扩板件轮廓预览", [&] { return service.submitContourExpansionPreview({}, 0); }},
        {"生成外扩贴板预览", [&] { return service.submitPlateGenerationPreview({}, 0); }},
        {"生成下底板预览", [&] { PlateGeneration r; r.kind = PlateGenerationKind::Base; return service.submitPlateGenerationPreview(r, 0); }},
        {"板厚识别", [&] { return service.submitThicknessInspection({"web"}, 0); }},
        {"过焊孔预览", [&] { return service.submitHolePreview({}, 0); }},
        {"底边孔预览", [&] { PlateHole r; r.kind = PlateHoleKind::Bottom; return service.submitHolePreview(r, 0); }},
        {"开孔底边识别", [&] { return service.submitHoleInspection("web", 0); }}
    };
    for (const auto &[label, submit] : previews)
    {
        SCOPED_TRACE(label);
        const auto task = finished(service, submit());
        ASSERT_EQ(task.state, TaskState::Failed);
        ASSERT_FALSE(task.message.empty());
        const auto events = service.takeUiCommandEvents();
        ASSERT_EQ(events.size(), 1u);
        EXPECT_EQ(events.front().taskId, task.id);
        EXPECT_EQ(events.front().kind, UiCommandEventKind::Output);
        EXPECT_TRUE(events.front().commandLine.empty());
        EXPECT_NE(events.front().message.find(label), std::string::npos);
        EXPECT_NE(events.front().message.find(task.message), std::string::npos);
        EXPECT_TRUE(service.takeUiCommandEvents().empty());
    }
    EXPECT_EQ(service.project().revision, 0);
    EXPECT_EQ(service.recordingState().commandCount, 0);
    EXPECT_EQ(readScript(script), before);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}

TEST(UiOperationFeedback, LocalFailureIsOutputOnlyAndDoesNotChangeRecordingOrHistory)
{
    using namespace reverse;
    CloudFixture files;
    CommandService service;
    const auto script = files.directory / "local-failure.py";
    ASSERT_TRUE(finished(service, service.submitStartRecording(script)).result.ok());
    const auto before = readScript(script);
    service.reportUiFailure("设置基准面", "三点共线，请重新选择不共线的点。");
    const auto events = service.takeUiCommandEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().kind, UiCommandEventKind::Output);
    EXPECT_EQ(events.front().message, "设置基准面失败：三点共线，请重新选择不共线的点。\n");
    EXPECT_TRUE(events.front().commandLine.empty());
    EXPECT_EQ(service.project().revision, 0);
    EXPECT_FALSE(service.project().canUndo);
    EXPECT_EQ(service.recordingState().commandCount, 0);
    EXPECT_EQ(readScript(script), before);
    ASSERT_TRUE(finished(service, service.submitStopRecording()).result.ok());
}

TEST(UiOperationFeedback, CancellingInternalPreviewDoesNotReportGenerationFailure)
{
    using namespace reverse;
    CommandService service;
    const auto blocked = service.submitPython("input('waiting')");
    const auto waiting = waitingForInput(service, blocked);
    const auto preview = service.submitPlateGenerationPreview({}, 0);
    ASSERT_TRUE(service.cancel(preview));
    ASSERT_TRUE(service.provideInput(blocked, waiting.inputRequest, std::string("continue")));
    ASSERT_TRUE(finished(service, blocked).result.ok());
    EXPECT_EQ(finished(service, preview).state, TaskState::Cancelled);
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
}
