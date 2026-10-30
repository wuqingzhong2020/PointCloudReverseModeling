/*
 * 文件描述：
 * 验证点云单位与属性、有限板件几何、STEP 回读，以及异步命令失败时的版本和结果保护。
 */
#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "commands/Json.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, double tolerance, const char* message) {
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, message);
}
template<class F> void expectError(F action, const std::string& code) {
    try { action(); }
    catch (const reverse::GeometryError& error) {
        if (error.code() != code) throw std::runtime_error("Unexpected error code: " + error.code() + " (expected " + code + ")");
        return;
    }
    throw std::runtime_error("Expected failure: " + code);
}
reverse::TaskSnapshot wait(reverse::CommandService& service, const std::string& id) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    for (;;) {
        const auto task = service.task(id);
        if (task.finished()) return task;
        if (std::chrono::steady_clock::now() > deadline) {
            (void)service.cancel(id);
            throw std::runtime_error("Task timed out");
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void success(reverse::CommandService& service, const std::string& id) {
    const auto task = wait(service, id);
    if (task.state != reverse::TaskState::Succeeded) throw std::runtime_error(task.errorCode + ": " + task.message);
}

struct TemporaryDirectory {
    std::filesystem::path path{std::filesystem::temp_directory_path() /
        ("reverse-modeling-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))};
    TemporaryDirectory() { require(std::filesystem::create_directory(path), "Could not create isolated test directory"); }
    ~TemporaryDirectory() { std::error_code error; std::filesystem::remove_all(path, error); }
};

void writeCloud(const std::filesystem::path& file) {
    std::ofstream out(file);
    constexpr int count = 11 * 5 * 2 + 1;
    out << "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z intensity\nSIZE 4 4 4 4\nTYPE F F F F\nCOUNT 1 1 1 1\nWIDTH "
        << count << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << count << "\nDATA ascii\n";
    for (int x = -50; x <= 50; x += 10)
        for (int y = -20; y <= 20; y += 10)
            for (int z : {-30, 30}) out << x << ' ' << y << ' ' << z << " 42\n";
    out << "nan 0 0 7\n";
    require(out.good(), "Could not write fixture");
}
} // namespace

TEST(PointCloud, LoadsUnitsAttributesAndEstimates) {
    using namespace reverse;
    TemporaryDirectory temp;
    const auto input = temp.path / std::filesystem::path(u8"中文点云.pcd");
    writeCloud(input);
    auto cloud = loadPointCloud(input, LengthUnit::Millimeter);
    ASSERT_TRUE(cloud->sourcePointCount == 111 && cloud->invalidPointCount == 1 && cloud->points.size() == 110) << "Invalid point filtering/counts are wrong";
    ASSERT_TRUE(cloud->hasIntensity && cloud->points.front().intensity == 42 && cloud->points.back().sourceIndex == 109) << "Intensity or original source index lost";
    auto meterCloud = loadPointCloud(input, LengthUnit::Meter);
    close(meterCloud->boundsMaxMm.x - meterCloud->boundsMinMm.x, 100000, 1e-5, "Unit conversion failed");
    const auto estimate = estimateBeam(*cloud);
    close(estimate.length, 100, 1e-6, "PCA length initial guess failed");
}

TEST(Geometry, BuildsFinitePlatesAndEvaluatesDeviation) {
    using namespace reverse;
    BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    const BeamFrame frame;
    auto model = buildBeam(frame, parameters);
    ASSERT_TRUE(model->summary().solidCount == 7) << "Expected three main plates plus four ribs";
    close(model->summary().volumeMm3, 70800, 1e-6, "Plate volume is wrong");
    CloudData query;
    query.points = {{{0, 0, 30}, 0, 0}, {{0, 0, 0}, 0, 1}, {{60, 0, 0}, 0, 2}};
    const auto deviation = evaluateDeviation(query, *model);
    close(deviation.distancesMm.at(0), 0, 1e-10, "Surface point distance is wrong");
    close(deviation.distancesMm.at(1), 2, 1e-10, "Interior point must have distance to surface, not zero");
    close(deviation.distancesMm.at(2), 10, 1e-10, "Finite end face distance is wrong");
}

TEST(PointCloud, RejectsInvalidInputAndCancellation) {
    using namespace reverse;
    TemporaryDirectory temp;
    const auto input = temp.path / std::filesystem::path(u8"中文点云.pcd");
    writeCloud(input);
    std::stop_source cancelled;
    cancelled.request_stop();
    expectError([&] { (void)loadPointCloud(input, LengthUnit::Millimeter, cancelled.get_token()); }, "cancelled");
    expectError([&] { (void)parseLengthUnit("unknown"); }, "invalid_unit");
    expectError([&] { (void)parseBeamParameters(nlohmann::json{{"length", 100}}); }, "invalid_parameters");
    const auto malformed = temp.path / "malformed.pcd";
    {
        std::ofstream out(malformed);
        out << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 4294967295 1 1\n"
               "WIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA binary\n";
    }
    expectError([&] { (void)loadPointCloud(malformed, LengthUnit::Millimeter); }, "invalid_pcd");
}

TEST(CommandService, PreservesStateAndRoundTripsStepAndJson) {
    using namespace reverse;
    TemporaryDirectory temp;
    const auto input = temp.path / std::filesystem::path(u8"中文点云.pcd");
    writeCloud(input);
    BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    const BeamFrame frame;
    CommandService service;
    success(service, service.submitImport(input, LengthUnit::Millimeter));
    const auto imported = service.project();
    ASSERT_TRUE(imported.revision == 1 && imported.cloud && imported.estimate) << "Import did not commit snapshot";
    const auto importJson = toJson(imported);
    ASSERT_FALSE(importJson.contains("suggestedParameters")) << "No preset thickness should be serialized after import";
    expectError([&] { (void)service.submitReconstruct(parameters, 0); }, "stale_revision");
    success(service, service.submitReconstruct(parameters, imported.revision, frame));
    const auto built = service.project();
    ASSERT_TRUE(built.revision == 2 && built.model && built.mesh && built.model->solidCount == 7) << "Model commit failed";
    close(built.model->parameters.webThickness, 4, 1e-8, "Building must preserve edited parameters instead of resetting defaults");
    auto invalid = parameters;
    invalid.webThickness = 0;
    const auto failed = wait(service, service.submitReconstruct(invalid, built.revision));
    ASSERT_TRUE(failed.state == TaskState::Failed) << "Invalid dimensions should fail";
    ASSERT_TRUE(service.project().revision == built.revision && service.project().modelId == built.modelId) << "Failed reconstruction changed prior model";
    success(service, service.submitDeviation(built.revision));
    ASSERT_TRUE(service.project().deviation->pointCount == 110) << "Deviation discarded valid points";
    const auto output = temp.path / std::filesystem::path(u8"中文模型.step");
    success(service, service.submitExport(output, built.revision));
    const auto exported = service.project();
    ASSERT_TRUE(exported.lastExport && exported.lastExport->valid && exported.lastExport->solidCount == 7) << "STEP independent readback did not validate solids";
    close(exported.lastExport->volumeMm3, 70800, 1e-4, "STEP changed volume");
    const auto beforeBytes = std::filesystem::file_size(output);
    ASSERT_TRUE(wait(service, service.submitExport(output, built.revision)).state == TaskState::Failed) << "Export unexpectedly overwrote existing STEP";
    ASSERT_TRUE(std::filesystem::file_size(output) == beforeBytes) << "Failed export changed file";
    ASSERT_TRUE(wait(service, service.submitImport(temp.path / "missing.pcd", LengthUnit::Millimeter)).state == TaskState::Failed) << "Missing cloud must fail";
    ASSERT_TRUE(service.project().modelId == built.modelId) << "Failed import discarded model";
    const auto json = toJson(service.project());
    ASSERT_TRUE(json.at("schemaVersion") == 1 && json.at("model").at("solidCount") == 7 && !json.at("cloud").contains("points")) << "Serializable summary contract failed";
    ASSERT_TRUE(nlohmann::json::parse(json.dump()) == json) << "JSON roundtrip failed";
    const auto planar = temp.path / "planar.pcd";
    {
        std::ofstream out(planar);
        out << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\n"
               "WIDTH 3\nHEIGHT 1\nPOINTS 3\nDATA ascii\n0 0 0\n10 0 0\n0 10 0\n";
    }
    success(service, service.submitImport(planar, LengthUnit::Millimeter));
    const auto planarProject = service.project();
    ASSERT_TRUE(planarProject.cloud && !planarProject.estimate && !planarProject.recognition && !planarProject.model && !planarProject.warnings.empty()) << "Valid planar cloud must import even without a beam frame";
    expectError([&] { (void)service.submitReconstruct(parameters, planarProject.revision); }, "no_frame");
}

namespace
{
reverse::CloudSelectionRect cloudRectangle(bool half = false)
{
    reverse::CloudSelectionRect rectangle;
    // 导入以首个有效点 (-50,-20,-30) 为原点，视图使用局部坐标。
    rectangle.viewProjection = {0.02, 0, 0, -1, 0, 0.05, 0, -1, 0, 0, 0.025, -0.75, 0, 0, 0, 1};
    if (half)
    {
        rectangle.maxX = 0;
    }
    return rectangle;
}
} // namespace

TEST(CloudCommands, CropInvalidatesDerivedResultsAndUndoRestoresExportableModel)
{
    using namespace reverse;
    TemporaryDirectory temp;
    const auto input = temp.path / "cloud.pcd";
    writeCloud(input);
    const auto fileSize = std::filesystem::file_size(input);
    CommandService service;
    success(service, service.submitImport(input, LengthUnit::Millimeter));
    const auto original = service.project();
    const BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, false, true};
    success(service, service.submitReconstruct(parameters, original.revision));
    success(service, service.submitDeviation(service.project().revision));
    const auto built = service.project();
    success(service, service.submitCloudSelection(cloudRectangle(true), CloudSelectionMode::Replace, built.revision));
    const auto selected = service.project();
    ASSERT_TRUE(selected.selection);
    EXPECT_EQ(selected.selection->size(), 60u);
    EXPECT_EQ(selected.model, built.model);
    EXPECT_FALSE(selected.canUndoCloud);
    expectError([&] { (void)service.submitCloudEdit(CloudEditMode::KeepSelection, built.revision); }, "stale_revision");
    success(service, service.submitCloudEdit(CloudEditMode::KeepSelection, selected.revision));
    const auto cropped = service.project();
    ASSERT_TRUE(cropped.cloud);
    EXPECT_EQ(cropped.cloud->points.size(), 60u);
    EXPECT_EQ(cropped.cloud->sourcePointCount, 111u);
    EXPECT_NE(cropped.cloudId, original.cloudId);
    EXPECT_TRUE(cropped.canUndoCloud);
    EXPECT_TRUE(cropped.canResetCloud);
    EXPECT_FALSE(cropped.canRedoCloud);
    EXPECT_FALSE(cropped.model);
    EXPECT_FALSE(cropped.mesh);
    EXPECT_FALSE(cropped.deviation);
    EXPECT_FALSE(cropped.selection);
    ASSERT_TRUE(cropped.estimate);
    EXPECT_NEAR(cropped.estimate->length, 60, 1e-6); // 剩余最长尺寸为 Z=60，不再沿用原梁长 100。
    expectError([&] { (void)service.submitExport(temp.path / "stale.step", cropped.revision); }, "no_model");
    success(service, service.submitCloudHistory(CloudHistoryAction::Undo, cropped.revision));
    const auto undone = service.project();
    EXPECT_EQ(undone.cloud, built.cloud);
    EXPECT_EQ(undone.model, built.model);
    EXPECT_EQ(undone.mesh, built.mesh);
    EXPECT_EQ(undone.deviation, built.deviation);
    EXPECT_GT(undone.revision, cropped.revision);
    EXPECT_TRUE(undone.canRedoCloud);
    EXPECT_FALSE(undone.canResetCloud);
    // 证明恢复了几何内核对象，不只是恢复界面摘要。
    success(service, service.submitExport(temp.path / "restored.step", undone.revision));
    EXPECT_EQ(service.project().lastExport->solidCount, built.model->solidCount);
    success(service, service.submitCloudHistory(CloudHistoryAction::Redo, service.project().revision));
    EXPECT_EQ(service.project().cloud, cropped.cloud);
    EXPECT_FALSE(service.project().model);
    EXPECT_EQ(std::filesystem::file_size(input), fileSize);
    EXPECT_EQ(loadPointCloud(input, LengthUnit::Millimeter)->points.size(), 110u);
}

TEST(CloudCommands, DeleteAllResetAndNoOpPreserveHistoryCorrectly)
{
    using namespace reverse;
    TemporaryDirectory temp;
    const auto input = temp.path / "cloud.pcd";
    writeCloud(input);
    CommandService service;
    success(service, service.submitImport(input, LengthUnit::Millimeter));
    const auto original = service.project();
    expectError([&] { (void)service.submitCloudEdit(CloudEditMode::DeleteSelection, original.revision); },
                "empty_selection");
    success(service, service.submitCloudSelection(cloudRectangle(), CloudSelectionMode::Replace, original.revision));
    const auto selected = service.project();
    ASSERT_EQ(selected.selection->size(), 110u);
    success(service, service.submitCloudEdit(CloudEditMode::KeepSelection, selected.revision));
    EXPECT_EQ(service.project().revision, selected.revision);
    EXPECT_FALSE(service.project().canUndoCloud);
    success(service, service.submitCloudEdit(CloudEditMode::DeleteSelection, selected.revision));
    const auto empty = service.project();
    EXPECT_TRUE(empty.cloud->points.empty());
    EXPECT_FALSE(empty.estimate);
    EXPECT_FALSE(empty.recognition);
    EXPECT_FALSE(empty.model);
    success(service, service.submitCloudHistory(CloudHistoryAction::Undo, empty.revision));
    success(service, service.submitCloudEdit(CloudEditMode::KeepSelection, service.project().revision));
    EXPECT_TRUE(service.project().canRedoCloud); // 无变化的截取不清空重做。
    success(service, service.submitCloudHistory(CloudHistoryAction::Redo, service.project().revision));
    success(service, service.submitCloudHistory(CloudHistoryAction::Reset, service.project().revision));
    EXPECT_EQ(service.project().cloud, original.cloud);
    success(service, service.submitCloudHistory(CloudHistoryAction::Undo, service.project().revision));
    EXPECT_TRUE(service.project().cloud->points.empty()); // 重置本身可撤销。
    success(service, service.submitCloudHistory(CloudHistoryAction::Undo, service.project().revision));
    success(service, service.submitCloudSelection(cloudRectangle(true), CloudSelectionMode::Replace,
                                                  service.project().revision));
    success(service, service.submitCloudEdit(CloudEditMode::DeleteSelection, service.project().revision));
    EXPECT_EQ(service.project().cloud->points.size(), 50u);
    EXPECT_FALSE(service.project().canRedoCloud); // 新分支丢弃旧重做。
    const auto edited = service.project();
    EXPECT_EQ(wait(service, service.submitImport(temp.path / "missing.pcd", LengthUnit::Millimeter)).state,
              TaskState::Failed);
    EXPECT_EQ(service.project().cloud, edited.cloud);
    EXPECT_TRUE(service.project().canUndoCloud);
    success(service, service.submitImport(input, LengthUnit::Millimeter));
    EXPECT_FALSE(service.project().canUndoCloud);
    EXPECT_FALSE(service.project().canRedoCloud);
    EXPECT_FALSE(service.project().canResetCloud);
    EXPECT_FALSE(service.project().selection);
}

TEST(PointCloud, ImportsBinaryFromChineseDirectoryAndFullWidthFilename)
{
    using namespace reverse;
    TemporaryDirectory temp;
    const auto directory = temp.path / std::filesystem::path(u8"中文目录（点云兼容）") /
                           std::filesystem::path(u8"普通 H 型钢");
    ASSERT_TRUE(std::filesystem::create_directories(directory));
    const auto unicodePath = directory / std::filesystem::path(u8"reverse_modeling_result（最普通H型钢）.pcd");
    const auto asciiPath = temp.path / "same-cloud.pcd";
    constexpr std::size_t pointCount = 11 * 5 * 2;
    {
        std::ofstream output(unicodePath, std::ios::binary);
        ASSERT_TRUE(output);
        output << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH "
               << pointCount << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << pointCount << "\nDATA binary\n";
        for (int x = -50; x <= 50; x += 10)
        {
            for (int y = -20; y <= 20; y += 10)
            {
                for (int z : {-30, 30})
                {
                    const std::array<float, 3> point{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
                    output.write(reinterpret_cast<const char *>(point.data()), sizeof(point));
                }
            }
        }
        ASSERT_TRUE(output.good());
    }
    ASSERT_TRUE(std::filesystem::copy_file(unicodePath, asciiPath));
    const auto unicodeCloud = loadPointCloud(unicodePath, LengthUnit::Millimeter);
    const auto asciiCloud = loadPointCloud(asciiPath, LengthUnit::Millimeter);
    ASSERT_EQ(unicodeCloud->points.size(), pointCount);
    ASSERT_EQ(asciiCloud->points.size(), pointCount);
    EXPECT_FALSE(unicodeCloud->hasIntensity);
    EXPECT_EQ(unicodeCloud->fields, (std::vector<std::string>{"x", "y", "z"}));
    EXPECT_EQ(unicodeCloud->originMm.x, asciiCloud->originMm.x);
    EXPECT_EQ(unicodeCloud->originMm.y, asciiCloud->originMm.y);
    EXPECT_EQ(unicodeCloud->originMm.z, asciiCloud->originMm.z);
    for (std::size_t i = 0; i < pointCount; ++i)
    {
        const auto &a = unicodeCloud->points[i];
        const auto &b = asciiCloud->points[i];
        EXPECT_EQ(a.position.x, b.position.x);
        EXPECT_EQ(a.position.y, b.position.y);
        EXPECT_EQ(a.position.z, b.position.z);
        EXPECT_EQ(a.sourceIndex, i);
    }
    CommandService service;
    success(service, service.submitImport(unicodePath, LengthUnit::Millimeter));
    const auto project = service.project();
    ASSERT_TRUE(project.cloud);
    EXPECT_EQ(project.cloud->points.size(), pointCount);
    const auto utf8Path = unicodePath.u8string();
    EXPECT_EQ(project.sourcePathUtf8, std::string(utf8Path.begin(), utf8Path.end()));
    const auto missing = directory / std::filesystem::path(u8"不存在（点云）.pcd");
    expectError([&] { (void)loadPointCloud(missing, LengthUnit::Millimeter); }, "input_not_readable");
}
