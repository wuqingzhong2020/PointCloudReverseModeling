/*
 * 文件描述：任务输出协议单元测试，覆盖初始化、并发锁、失败/取消保护、真实 STEP 交付及服务关闭顺序。
 */
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#ifdef _WIN32
#include <Windows.h>
#endif
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "commands/CommandService.h"
#include "commands/TaskOutput.h"
#include "geometry/Geometry.h"

namespace
{
class TaskOutputTest : public testing::Test
{
protected:
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("reverse-task-output-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) /
        std::filesystem::path(u8"中文 任务");
    void SetUp() override
    {
        std::filesystem::create_directories(directory);
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory.parent_path(), ignored);
    }
    static void write(const std::filesystem::path &path, std::string_view value)
    {
        std::ofstream file(path, std::ios::binary);
        file << value;
        if (!file)
        {
            throw std::runtime_error("fixture write failed");
        }
    }
    nlohmann::json result() const
    {
        std::ifstream file(directory / "result.json", std::ios::binary);
        return nlohmann::json::parse(file);
    }
    static reverse::StepExportResult fakeStep(const std::filesystem::path &path)
    {
        write(path, "verified-step");
        reverse::StepExportResult result;
        result.fileBytes = std::filesystem::file_size(path);
        result.valid = true;
        result.solidCount = 1;
        return result;
    }
    static reverse::TaskSnapshot finish(reverse::CommandService &service, const std::string &id)
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
        throw std::runtime_error("Task timed out");
    }
    void build(reverse::CommandService &service)
    {
        const auto path = directory / "input.pcd";
        write(path, "VERSION .7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
                    "WIDTH 8\nHEIGHT 1\nPOINTS 8\nDATA ascii\n"
                    "0 -20 -30\n0 -20 30\n0 20 -30\n0 20 30\n100 -20 -30\n100 -20 30\n100 20 -30\n100 20 30\n");
        ASSERT_EQ(finish(service, service.submitStartupImport(path)).state, reverse::TaskState::Succeeded);
        EXPECT_FALSE(service.project().model);
        const reverse::BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
        ASSERT_EQ(finish(service, service.submitReconstruct(parameters, service.project().revision,
                                                           reverse::BeamFrame{})).state,
                  reverse::TaskState::Succeeded);
    }
};
#ifdef _WIN32
struct ReadLock
{
    HANDLE handle;
    explicit ReadLock(const std::filesystem::path &path)
        : handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr))
    {
        if (handle == INVALID_HANDLE_VALUE)
        {
            throw std::runtime_error("Cannot lock fixture");
        }
    }
    ~ReadLock()
    {
        CloseHandle(handle);
    }
};
#endif
}
TEST_F(TaskOutputTest, InitializesFalsePreservesOldModelAndRejectsConcurrentDirectory)
{
    write(directory / "reserveModel.step", "old-model");
    write(directory / "result.json", R"({"model_file":"reserveModel.step","export_success":true})");
    {
        reverse::TaskOutput output;
        output.initialize(directory);
        EXPECT_TRUE(output.initialized());
        EXPECT_EQ(result(), (nlohmann::json{{"model_file", "reserveModel.step"}, {"export_success", false}}));
        EXPECT_EQ(std::filesystem::file_size(directory / "reserveModel.step"), 9);
        reverse::TaskOutput other;
        EXPECT_THROW(other.initialize(directory / ".." / directory.filename()), reverse::GeometryError);
        EXPECT_FALSE(other.initialized());
        EXPECT_FALSE(result().at("export_success").get<bool>());
    }
    reverse::TaskOutput next;
    EXPECT_NO_THROW(next.initialize(directory));
}
TEST_F(TaskOutputTest, CreatesDirectoryAndRejectsFileOrUnwritableResult)
{
    reverse::TaskOutput output;
    const auto created = directory / "new" / "nested";
    output.initialize(created);
    EXPECT_TRUE(std::filesystem::is_regular_file(created / "result.json"));
    reverse::TaskOutput invalid;
    EXPECT_THROW(invalid.initialize(created / "result.json"), reverse::GeometryError);
    std::filesystem::create_directory(directory / "result.json");
    EXPECT_THROW(invalid.initialize(directory), reverse::GeometryError);
    EXPECT_FALSE(invalid.initialized());
}
TEST_F(TaskOutputTest, ConversionFailureEmptyFileAndCancellationKeepFalse)
{
    reverse::TaskOutput output;
    output.initialize(directory);
    write(directory / "reserveModel.step", "old-model");
    EXPECT_THROW((void)output.deliver([](const auto &) -> reverse::StepExportResult {
        throw std::runtime_error("conversion failed");
    }), std::runtime_error);
    EXPECT_THROW((void)output.deliver([](const auto &path) {
        write(path, "");
        return reverse::StepExportResult{};
    }), reverse::GeometryError);
    std::stop_source stop;
    EXPECT_THROW((void)output.deliver([&](const auto &path) {
        const auto result = fakeStep(path);
        stop.request_stop();
        return result;
    }, stop.get_token()), reverse::GeometryError);
    EXPECT_FALSE(result().at("export_success").get<bool>());
    EXPECT_EQ(std::filesystem::file_size(directory / "reserveModel.step"), 9);
}
#ifdef _WIN32
TEST_F(TaskOutputTest, LockedStepDoesNotReplaceOldFileAndCanRetry)
{
    reverse::TaskOutput output;
    output.initialize(directory);
    write(directory / "reserveModel.step", "old-model");
    {
        ReadLock lock(directory / "reserveModel.step");
        EXPECT_THROW((void)output.deliver(fakeStep), reverse::GeometryError);
        EXPECT_FALSE(result().at("export_success").get<bool>());
        EXPECT_EQ(std::filesystem::file_size(directory / "reserveModel.step"), 9);
    }
    EXPECT_NO_THROW((void)output.deliver(fakeStep));
    EXPECT_TRUE(result().at("export_success").get<bool>());
}
TEST_F(TaskOutputTest, LockedJsonKeepsFalseAfterStepCommitAndAllowsRetry)
{
    reverse::TaskOutput output;
    output.initialize(directory);
    {
        ReadLock lock(directory / "result.json");
        EXPECT_THROW((void)output.deliver(fakeStep), reverse::GeometryError);
        EXPECT_TRUE(output.initialized());
        EXPECT_FALSE(result().at("export_success").get<bool>());
        EXPECT_GT(std::filesystem::file_size(directory / "reserveModel.step"), 0);
    }
    EXPECT_NO_THROW((void)output.deliver(fakeStep));
    EXPECT_TRUE(result().at("export_success").get<bool>());
    EXPECT_EQ(result().size(), 2);
}
#endif
TEST_F(TaskOutputTest, ImportFailureAndOrdinaryExportNeverCommitSuccess)
{
    reverse::CommandService service;
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    ASSERT_TRUE(service.outputInitialized());
    EXPECT_EQ(finish(service, service.submitStartupImport(directory / "missing.pcd")).state, reverse::TaskState::Failed);
    EXPECT_EQ(finish(service, service.submitStartupImport(directory)).state, reverse::TaskState::Failed);
    write(directory / "bad.step", "bad");
    EXPECT_EQ(finish(service, service.submitStartupImport(directory / "bad.step")).errorCode, "unsupported_format");
    EXPECT_FALSE(result().at("export_success").get<bool>());
    build(service);
    ASSERT_EQ(finish(service, service.submitExport(directory / "manual.step", service.project().revision)).state,
              reverse::TaskState::Succeeded);
    EXPECT_FALSE(result().at("export_success").get<bool>());
    service.requestShutdown();
    EXPECT_FALSE(result().at("export_success").get<bool>());
}
TEST_F(TaskOutputTest, FullStepDeliveryClosesServiceOnlyAfterSuccessfulCommit)
{
    reverse::CommandService service;
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(finish(service, service.submitExportAndExit(0)).errorCode, "no_model");
    EXPECT_TRUE(service.isIdle());
    build(service);
    const auto before = service.project();
    EXPECT_EQ(finish(service, service.submitExportAndExit(0)).errorCode, "stale_revision");
    const auto task = finish(service, service.submitExportAndExit(before.revision));
    ASSERT_EQ(task.state, reverse::TaskState::Succeeded) << task.message;
    EXPECT_TRUE(result().at("export_success").get<bool>());
    const auto imported = reverse::loadCadModel(directory / "reserveModel.step");
    EXPECT_EQ(imported->summary().solidCount, before.model->solidCount);
    EXPECT_NEAR(imported->summary().volumeMm3, before.model->volumeMm3, 1e-4);
    EXPECT_THROW((void)service.submitLine("project.snapshot"), reverse::GeometryError);
    EXPECT_EQ(service.project().revision, before.revision);
}
TEST_F(TaskOutputTest, MissingOutputAndBusyWorkerCannotDeliver)
{
    reverse::CommandService service;
    build(service);
    EXPECT_FALSE(service.outputInitialized());
    EXPECT_EQ(finish(service, service.submitExportAndExit(service.project().revision)).errorCode,
              "output_not_initialized");
    EXPECT_FALSE(std::filesystem::exists(directory / "result.json"));
    const auto waiting = service.submitPython("input('hold')");
    EXPECT_THROW((void)service.submitExportAndExit(service.project().revision), reverse::GeometryError);
    (void)service.cancel(waiting);
    EXPECT_EQ(finish(service, waiting).state, reverse::TaskState::Cancelled);
}


TEST_F(TaskOutputTest, RecordingOverwritesWithNoticeAndCapturesStartupAndFinalDelivery)
{
    reverse::CommandService service;
    const auto script = directory / "record.py";
    write(script, "old content must disappear");
    const auto started = finish(service, service.submitStartRecording(script, true));
    ASSERT_EQ(started.state, reverse::TaskState::Succeeded);
    EXPECT_NE(started.message.find("已覆盖"), std::string::npos);
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    build(service);
    const auto delivered = finish(service, service.submitExportAndExit(service.project().revision));
    ASSERT_EQ(delivered.state, reverse::TaskState::Succeeded) << delivered.message;
    EXPECT_TRUE(service.exitRequested());
    EXPECT_FALSE(service.outputInitialized());
    EXPECT_FALSE(service.recordingState().active);
    std::ifstream input(script, std::ios::binary);
    const std::string source{std::istreambuf_iterator<char>(input), {}};
    EXPECT_EQ(source.find("old content"), std::string::npos);
    const auto output = source.find("project.set_output_dir ");
    const auto import = source.find("cloud.import ");
    EXPECT_LT(output, import);
    EXPECT_LT(import, source.find("model.parametric "));
    EXPECT_TRUE(source.ends_with("project.export_and_exit\n"));
    EXPECT_EQ(service.recordingState().commandCount, 4);
    const auto events = service.takeUiCommandEvents();
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto &event) {
        return event.kind == reverse::UiCommandEventKind::Started && event.commandLine == "project.export_and_exit";
    }));
}

TEST_F(TaskOutputTest, LateRecordingSeedsSuccessfulSessionWithoutReexecutingIt)
{
    reverse::CommandService service;
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(finish(service, service.submitStartupImport(directory / "missing.pcd")).state, reverse::TaskState::Failed);
    build(service);
    const auto revision = service.project().revision;
    const auto started = finish(service, service.submitStartRecording(directory / "late.py"));
    ASSERT_EQ(started.state, reverse::TaskState::Succeeded);
    EXPECT_NE(started.message.find("已补写 3 条前序命令"), std::string::npos);
    EXPECT_EQ(service.project().revision, revision);
    EXPECT_TRUE(service.project().model);
    EXPECT_EQ(service.recordingState().commandCount, 3);
    ASSERT_EQ(finish(service, service.submitStopRecording()).state, reverse::TaskState::Succeeded);
}

TEST_F(TaskOutputTest, OutputCommandHelpAndRepeatedInitializationHaveNoSideEffects)
{
    reverse::CommandService service;
    EXPECT_EQ(finish(service, service.submitLine("project.export_and_exit --help")).result.kind, rm::ResultKind::Help);
    EXPECT_EQ(finish(service, service.submitLine("project.set_output_dir --help")).result.kind, rm::ResultKind::Help);
    EXPECT_FALSE(service.outputInitialized());
    EXPECT_FALSE(service.exitRequested());
    EXPECT_FALSE(std::filesystem::exists(directory / "result.json"));
    EXPECT_EQ(finish(service, service.submitLine("project.set_output_dir")).state, reverse::TaskState::Failed);
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    write(directory / "result.json", "sentinel");
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory / ".")).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(std::filesystem::file_size(directory / "result.json"), 8);
    EXPECT_EQ(finish(service, service.submitInitializeOutput(directory / "other")).errorCode, "output_already_initialized");
}

TEST_F(TaskOutputTest, NativeAndMixedReplayRecordExitAndSkipFollowingLines)
{
    for (bool mixed : {false, true})
    {
        reverse::CommandService service;
        build(service);
        const auto outputDir = directory / (mixed ? "mixed" : "native");
        const auto utf8 = outputDir.generic_u8string();
        const std::string output(reinterpret_cast<const char *>(utf8.data()), utf8.size());
        const auto script = directory / "replay.py";
        write(script, (mixed ? "print('before')\n" : "") +
            std::string("project.set_output_dir \"") + output + "\"\nproject.export_and_exit\n" +
            (mixed ? "open(__file__ + '.unexpected', 'w').write('bad')\n" : "cloud.import missing.pcd\n"));
        ASSERT_EQ(finish(service, service.submitStartRecording(directory / "re-record.py", true)).state,
                  reverse::TaskState::Succeeded);
        const auto replayed = finish(service, service.submitScriptFile(script));
        ASSERT_EQ(replayed.state, reverse::TaskState::Succeeded) << replayed.message;
        EXPECT_TRUE(service.exitRequested());
        EXPECT_FALSE(service.recordingState().active);
        EXPECT_FALSE(std::filesystem::exists(directory / "replay.py.unexpected"));
        EXPECT_EQ(replayed.output.find("cloud.import missing"), std::string::npos);
        EXPECT_TRUE(std::filesystem::exists(outputDir / "reserveModel.step"));
        std::ifstream input(directory / "re-record.py", std::ios::binary);
        const std::string source{std::istreambuf_iterator<char>(input), {}};
        EXPECT_TRUE(source.ends_with("project.export_and_exit\n"));
    }
}

TEST_F(TaskOutputTest, FailedExitIsNotRecordedAndDoesNotClose)
{
    reverse::CommandService service;
    ASSERT_EQ(finish(service, service.submitStartRecording(directory / "failed.py")).state, reverse::TaskState::Succeeded);
    const auto failed = finish(service, service.submitLine("project.export_and_exit"));
    EXPECT_EQ(failed.errorCode, "no_model");
    EXPECT_EQ(service.recordingState().commandCount, 0);
    EXPECT_FALSE(service.exitRequested());
    EXPECT_TRUE(service.isIdle());
}

TEST_F(TaskOutputTest, CommandArgumentsExitDiscardsQueuedWork)
{
    reverse::CommandService service;
    ASSERT_EQ(finish(service, service.submitInitializeOutput(directory)).state, reverse::TaskState::Succeeded);
    build(service);
    const auto revision = service.project().revision;
    const auto hold = service.submitPython("input('hold')");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!service.task(hold).waitingForInput && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    ASSERT_TRUE(service.task(hold).waitingForInput);
    const auto exit = service.submitCommandArguments({"project.export_and_exit"});
    const auto following = service.submitLine("cloud.reset");
    ASSERT_TRUE(service.provideInput(hold, service.task(hold).inputRequest, std::string{}));
    ASSERT_EQ(finish(service, exit).state, reverse::TaskState::Succeeded);
    EXPECT_EQ(finish(service, following).errorCode, "shutting_down");
    EXPECT_EQ(service.project().revision, revision);
    EXPECT_TRUE(result().at("export_success").get<bool>());
}
