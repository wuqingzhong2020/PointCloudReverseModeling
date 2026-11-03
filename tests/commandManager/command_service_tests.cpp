/* 文件描述：固定线程 FIFO、Python 类型桥接、宏回放与取消的产品适配单元测试。 */
#include "commands/CommandBindings.h"
#include "commands/CommandService.h"
#include "commands/CommandFeedback.h"
#include "commands/CommandScript.h"
#include "CommandModules.h"
#include <chrono>
#include <filesystem>
#include <fstream>
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
        const auto text = reverse::commandText(definition, {path}, {{"unit", "mm"}});
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
            auto batch = service.takeUiCommandEvents();
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
            EXPECT_TRUE(service.takeUiCommandEvents().empty());
        };
        collect(service.submitImport(file.path, reverse::LengthUnit::Millimeter));
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
        ASSERT_TRUE(finished(service, service.submitImport(file.path, reverse::LengthUnit::Millimeter)).result.ok());
        const auto built = finished(service, service.submitReconstruct(parameters, service.project().revision, frame));
        ASSERT_TRUE(built.result.ok()) << built.message;
        solids = service.project().model->solidCount;
        const auto exported =
            finished(service, service.submitExport(file.directory / "model.step", service.project().revision));
        ASSERT_TRUE(exported.result.ok()) << exported.message;
        events = service.takeUiCommandEvents();
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
    const auto errors = service.takeUiCommandEvents();
    ASSERT_EQ(errors.size(), 1u);
    EXPECT_FALSE(errors.front().succeeded);
    EXPECT_NE(errors.front().message.find("end_tolerance"), std::string::npos);
    EXPECT_NE(errors.front().commandLine.find("--end-tolerance -1"), std::string::npos);

    const auto blocked = service.submitPython("input('waiting')");
    const auto waiting = waitingForInput(service, blocked);
    const auto ui = service.submitCloudHistory(reverse::CloudHistoryAction::Reset, 0);
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
    ASSERT_TRUE(service.cancel(ui));
    ASSERT_TRUE(service.provideInput(blocked, waiting.inputRequest, std::string("continue")));
    ASSERT_TRUE(finished(service, blocked).result.ok());
    EXPECT_EQ(finished(service, ui).state, reverse::TaskState::Cancelled);
    const auto cancelled = service.takeUiCommandEvents();
    ASSERT_EQ(cancelled.size(), 1u);
    EXPECT_FALSE(cancelled.front().succeeded);
    EXPECT_EQ(cancelled.front().commandLine, "cloud.reset");
    (void)finished(service, service.submitLine("help"));
    (void)finished(service, service.submitCommand("project.snapshot"));
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
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
                finished(service, service.submitImport(file.path, reverse::LengthUnit::Millimeter)).result.ok());
            reverse::CloudSelectionRect rect;
            rect.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
            rect.maxX = 0;
            ASSERT_TRUE(finished(service, service.submitCloudSelection(rect, reverse::CloudSelectionMode::Replace,
                                                                       service.project().revision))
                            .result.ok());
            ASSERT_TRUE(finished(service, service.submitCloudEdit(mode, service.project().revision)).result.ok());
            edit = service.takeUiCommandEvents().back();
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
TEST(CloudEditCommands, MultipleCameraRegionsAndUndoKeepCompleteRecipe)
{
    CloudFixture file;
    reverse::UiCommandEvent crop, remove;
    std::vector<std::size_t> expectedCrop, expectedRemove;
    {
        reverse::CommandService service;
        ASSERT_TRUE(finished(service, service.submitImport(file.path, reverse::LengthUnit::Millimeter)).result.ok());
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
        crop = service.takeUiCommandEvents().back();
        for (const auto &point : service.project().cloud->points)
            expectedCrop.push_back(point.sourceIndex);
        ASSERT_TRUE(
            finished(service, service.submitCloudHistory(reverse::CloudHistoryAction::Undo, service.project().revision))
                .result.ok());
        ASSERT_TRUE(service.project().selectionRecipe);
        EXPECT_EQ(service.project().selectionRecipe->regions.size(), 3u);
        ASSERT_TRUE(finished(service, service.submitCloudEdit(reverse::CloudEditMode::DeleteSelection,
                                                              service.project().revision))
                        .result.ok());
        remove = service.takeUiCommandEvents().back();
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
        ASSERT_TRUE(finished(service, service.submitImport(file.path, reverse::LengthUnit::Millimeter)).result.ok());
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
        edit = service.takeUiCommandEvents().back();
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
        EXPECT_EQ(service.takeUiCommandEvents().back().commandLine, "cloud.crop --indices []");
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
    ASSERT_TRUE(finished(service, service.submitImport(file.path, reverse::LengthUnit::Millimeter)).result.ok());
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
    const auto import = service.submitImport(file.path, reverse::LengthUnit::Millimeter);
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
    EXPECT_EQ(service.recordingState().commandCount, 1);
    EXPECT_EQ(readScript(other), source);
    EXPECT_EQ(readScript(path), source);
    // Direct Python calls use the same recorder as commands from script files.
    ASSERT_TRUE(finished(service, service.submitPython("rm.execute('project.snapshot')")).result.ok());
    EXPECT_EQ(service.recordingState().commandCount, 2);
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
               << "\" --unit mm\ncloud.select_indices --indices '[0,1]'\n"
                  "cloud.crop\nmodel.reconstruct --mirror wrong\ncloud.reset\n";
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(output)).result.ok());
    const auto script = service.submitScriptFile(input);
    const auto after = service.submitLine("project.snapshot");
    const auto result = finished(service, script);
    EXPECT_EQ(result.errorCode, "invalid_value");
    EXPECT_TRUE(result.result.committed);
    EXPECT_NE(result.output.find("[脚本] rm> cloud.import "), std::string::npos);
    EXPECT_NE(result.output.find("已导入点云，共 8 点。"), std::string::npos);
    EXPECT_NE(result.output.find("[脚本] rm> cloud.select_indices --indices '[0,1]'"), std::string::npos);
    EXPECT_NE(result.output.find("[脚本] rm> cloud.crop"), std::string::npos);
    EXPECT_NE(result.output.find("[脚本] rm> model.reconstruct --mirror wrong"), std::string::npos);
    EXPECT_TRUE(result.output.ends_with("未完成：" + result.message + '\n'));
    EXPECT_TRUE(result.message.starts_with("第 5 行："));
    EXPECT_NE(result.message.find("--mirror"), std::string::npos);
    EXPECT_EQ(result.result.value.as<rm::Arguments>().at("scriptLine"), rm::Value(5U));
    EXPECT_EQ(result.result.value.as<rm::Arguments>().at("parameter"), rm::Value("--mirror"));
    EXPECT_EQ(result.output.find("[脚本] rm> cloud.reset"), std::string::npos);
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
    const auto first = result.output.find("[脚本] rm> project.snapshot\n");
    ASSERT_NE(first, std::string::npos);
    const auto second = result.output.find("[脚本] rm> project.snapshot\n", first + 1);
    ASSERT_NE(second, std::string::npos);
    EXPECT_EQ(result.output.find("[脚本] rm> project.snapshot\n", second + 1), std::string::npos);
    EXPECT_NE(result.output.find("[脚本] rm> help cloud.crop\n"), std::string::npos);
    EXPECT_NE(result.output.find("[脚本] rm> cloud.reset --help\n"), std::string::npos);
    EXPECT_NE(result.output.find("用法: cloud.crop"), std::string::npos);
    EXPECT_EQ(result.output.find("注释"), std::string::npos);
    EXPECT_EQ(result.output.find("版本:"), std::string::npos);
    EXPECT_TRUE(finished(service, service.submitLine("project.snapshot")).output.empty());
    EXPECT_EQ(finished(service, service.submitPythonLine("1 + 1")).output, "2\n");
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
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
        EXPECT_TRUE(output.ends_with("[脚本] rm> test.step\n"));
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
        EXPECT_EQ(output.find("[脚本] rm> help"), std::string::npos);
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
    EXPECT_EQ(finished(service, service.submitStartRecording(file.directory / "missing" / "script.py")).errorCode,
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
                  "cloud.import \"" << file.utf8() << "\" --unit mm\n"
                  "cloud.select_indices --indices '[0,1]'\ncloud.crop\n"
                  "print(values[1], literal.startswith('保留文字'))\n";
    }
    reverse::CommandService service;
    ASSERT_TRUE(finished(service, service.submitStartRecording(recording)).result.ok());
    const auto task = finished(service, service.submitScriptFile(path));
    ASSERT_TRUE(task.result.ok()) << task.message;
    EXPECT_TRUE(task.output.starts_with("开始\n"));
    EXPECT_TRUE(task.output.ends_with("2 True\n"));
    const auto first = task.output.find("[脚本] rm> project.snapshot\n");
    ASSERT_NE(first, std::string::npos);
    EXPECT_NE(task.output.find("[脚本] rm> project.snapshot\n", first + 1), std::string::npos);
    EXPECT_EQ(task.output.find("[脚本] rm> cloud.reset"), std::string::npos);
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
    EXPECT_TRUE(imported.output.empty());
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
    EXPECT_TRUE(native.output.empty());
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
    EXPECT_EQ(direct.output, "已导入点云，共 8 点。\n");
    EXPECT_EQ(finished(service, service.submitPythonLine("_['revision']")).output, "1\n");
    const auto assignment = finished(service, service.submitPythonLine("result = " + expression));
    ASSERT_TRUE(assignment.result.ok()) << assignment.message;
    EXPECT_TRUE(assignment.output.empty());
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
