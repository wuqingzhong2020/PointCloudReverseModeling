/*
 * 文件描述：
 * 用独立构造的顶边/底面采样验证逐板识别、底板最大值估算、证据不足、默认单侧及显式镜像。
 * 检查底板夹持、翼板底面对齐、有界端部收齐及显示、STEP 数据一致性。
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "commands/Json.h"
#include "geometry/Geometry.h"
#include "workbench/ModelDisplayData.h"

namespace
{
using namespace reverse;

CloudData observedTray(double firstThickness = 8, double secondThickness = 12)
{
    CloudData cloud;
    std::mt19937 generator(201);
    std::uniform_real_distribution<double> noise(-0.02, 0.02);
    const auto add = [&](double x, double y, double z)
    { cloud.points.push_back({{x + noise(generator), y + noise(generator), z + noise(generator)}, 0, 0}); };
    for (int x = -500; x <= 500; x += 5)
    {
        for (int y = -100; y <= 100; y += 5)
        {
            add(x, y, 0);
        }
        for (int across = 0; across <= 10; ++across)
        {
            add(x, -100 + across * firstThickness / 10, 100);
            add(x, 100 - secondThickness + across * secondThickness / 10, 100);
        }
    }
    for (int y = -90; y <= 88; y += 2)
    {
        for (int across = 0; across <= 10; ++across)
        {
            add(-250 - 3 + across * 0.6, y, 100); // 6 mm 肋板。
            add(180 - 5 + across, y, 100);        // 10 mm 肋板。
        }
    }
    return cloud;
}

TEST(PlateRecognition, MeasuresDifferentPlateWidthsAndActualStations)
{
    const auto cloud = observedTray();
    const auto recognition = recognizePlates(cloud, BeamEstimate{});
    ASSERT_EQ(recognition.plates.size(), 5u);
    const auto &bottom = recognition.plates.front();
    ASSERT_TRUE(bottom.thicknessMm);
    EXPECT_DOUBLE_EQ(*bottom.thicknessMm,
                     std::max(*recognition.plates[1].thicknessMm, *recognition.plates[2].thicknessMm));
    EXPECT_NEAR(*bottom.thicknessMm, 12, 0.15);
    EXPECT_EQ(bottom.method, "inferred_side_plate_max");
    EXPECT_NEAR(bottom.maximumMm.y, 0, 0.1);
    EXPECT_DOUBLE_EQ(bottom.minimumMm.y, bottom.maximumMm.y - *bottom.thicknessMm);
    EXPECT_DOUBLE_EQ(bottom.minimumMm.z, recognition.plates[1].maximumMm.z);
    EXPECT_DOUBLE_EQ(bottom.maximumMm.z, recognition.plates[2].minimumMm.z);
    EXPECT_NEAR(*recognition.plates[1].thicknessMm, 12, 0.15); // 框架 z 轴为项目 -Y。
    EXPECT_NEAR(*recognition.plates[2].thicknessMm, 8, 0.15);
    EXPECT_NEAR(*recognition.plates[3].thicknessMm, 6, 0.15);
    EXPECT_NEAR(*recognition.plates[4].thicknessMm, 10, 0.15);
    EXPECT_NEAR((recognition.plates[3].minimumMm.x + recognition.plates[3].maximumMm.x) / 2, -250, 0.1);
    EXPECT_NEAR((recognition.plates[4].minimumMm.x + recognition.plates[4].maximumMm.x) / 2, 180, 0.1);
    const auto model = buildRecognizedPlates(recognition);
    EXPECT_EQ(model->summary().solidCount, 5u);
    EXPECT_EQ(model->summary().surfaceCount, 0u);
    EXPECT_FALSE(model->summary().parameters.mirrorSymmetry);
    const auto display = workbench::makeModelDisplayParts(model->mesh(), model->summary(), {});
    ASSERT_EQ(display.size(), 5u);
    EXPECT_FALSE(display.front().thicknessUnresolved);
    EXPECT_TRUE(display.front().thicknessEstimated);
    EXPECT_EQ(display.front().thicknessMm, bottom.thicknessMm);
    for (std::size_t index = 1; index < display.size(); ++index)
    {
        EXPECT_EQ(display[index].thicknessMm, recognition.plates[index].thicknessMm);
        EXPECT_FALSE(display[index].thicknessEstimated);
        EXPECT_DOUBLE_EQ(recognition.plates[index].minimumMm.y,
                         index <= 2 ? bottom.minimumMm.y : bottom.maximumMm.y);
    }
}

TEST(PlateRecognition, TakesThickerSideRegardlessOfSideOrder)
{
    for (const bool swapSides : {false, true})
    {
        const auto cloud = observedTray(swapSides ? 16 : 10, swapSides ? 10 : 16);
        const auto recognition = recognizePlates(cloud, BeamEstimate{});
        ASSERT_GE(recognition.plates.size(), 3u);
        ASSERT_TRUE(recognition.plates.front().thicknessMm);
        ASSERT_TRUE(recognition.plates[1].thicknessMm);
        ASSERT_TRUE(recognition.plates[2].thicknessMm);
        EXPECT_NEAR(*recognition.plates[1].thicknessMm, swapSides ? 10 : 16, 0.15);
        EXPECT_NEAR(*recognition.plates[2].thicknessMm, swapSides ? 16 : 10, 0.15);
        EXPECT_NEAR(*recognition.plates.front().thicknessMm, 16, 0.15);
        EXPECT_DOUBLE_EQ(*recognition.plates.front().thicknessMm,
                         std::max(*recognition.plates[1].thicknessMm, *recognition.plates[2].thicknessMm));
    }
}

TEST(PlateRecognition, MirrorsAboutBottomMidplaneAndExportsEstimatedSolid)
{
    const auto recognition = recognizePlates(observedTray(), BeamEstimate{});
    const auto model = buildRecognizedPlates(recognition, PlateBuildOptions{true});
    EXPECT_EQ(model->summary().solidCount, 9u);
    EXPECT_EQ(model->summary().surfaceCount, 0u);
    EXPECT_EQ(model->summary().plates.size(), 9u);
    const auto &bottom = recognition.plates.front();
    const double plane = (bottom.minimumMm.y + bottom.maximumMm.y) / 2;
    for (std::size_t index = 1; index < recognition.plates.size(); ++index)
    {
        const auto &mirror = model->summary().plates[recognition.plates.size() + index - 1];
        EXPECT_DOUBLE_EQ(mirror.minimumMm.y, 2 * plane - recognition.plates[index].maximumMm.y);
        EXPECT_NEAR(mirror.maximumMm.y, index <= 2 ? plane : bottom.minimumMm.y, 1e-9);
        if (index <= 2)
        {
            EXPECT_DOUBLE_EQ(model->summary().plates[index].minimumMm.y, plane);
        }
        EXPECT_TRUE(mirror.method.starts_with("explicit_mirror_"));
    }
    const auto &plates = model->summary().plates;
    for (std::size_t i = 0; i < plates.size(); ++i)
    {
        for (std::size_t j = i + 1; j < plates.size(); ++j)
        {
            const auto &a = plates[i];
            const auto &b = plates[j];
            const double overlapX = std::min(a.maximumMm.x, b.maximumMm.x) - std::max(a.minimumMm.x, b.minimumMm.x);
            const double overlapY = std::min(a.maximumMm.y, b.maximumMm.y) - std::max(a.minimumMm.y, b.minimumMm.y);
            const double overlapZ = std::min(a.maximumMm.z, b.maximumMm.z) - std::max(a.minimumMm.z, b.minimumMm.z);
            EXPECT_TRUE(overlapX <= 1e-8 || overlapY <= 1e-8 || overlapZ <= 1e-8) << a.id << " / " << b.id;
        }
    }
    const auto file =
        std::filesystem::temp_directory_path() /
        ("recognized-plates-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    const auto result = exportStep(*model, file);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.solidCount, 9u);
    EXPECT_EQ(result.surfaceCount, 0u);
    std::filesystem::remove(file);
}

TEST(PlateRecognition, SnapsOnlyEndOverhangsWithinConfiguredTolerance)
{
    auto recognition = recognizePlates(observedTray(), BeamEstimate{});
    recognition.plates[0].minimumMm.x = -510;
    recognition.plates[0].maximumMm.x = 540;
    recognition.plates[1].minimumMm.x = -500;
    recognition.plates[1].maximumMm.x = 500;
    recognition.plates[2].minimumMm.x = -498;
    recognition.plates[2].maximumMm.x = 498;
    const auto aligned = buildRecognizedPlates(recognition, PlateBuildOptions{false, 12});
    EXPECT_DOUBLE_EQ(aligned->summary().plates[0].minimumMm.x, -498); // 等于容差，也应收齐。
    EXPECT_DOUBLE_EQ(aligned->summary().plates[0].maximumMm.x, 540);  // 42 mm 突出，保留。
    EXPECT_FALSE(aligned->summary().warnings.empty());
    EXPECT_DOUBLE_EQ(aligned->summary().recognitionOptions.endOverhangToleranceMm, 12);
    EXPECT_DOUBLE_EQ(recognition.plates[0].minimumMm.x, -510); // 原始识别结果不能被改写。
    for (const double tolerance : {0.0, 11.9})
    {
        const auto retained = buildRecognizedPlates(recognition, PlateBuildOptions{false, tolerance});
        EXPECT_DOUBLE_EQ(retained->summary().plates[0].minimumMm.x, -510);
        EXPECT_DOUBLE_EQ(retained->summary().plates[0].maximumMm.x, 540);
    }
    const auto bothAligned = buildRecognizedPlates(recognition, PlateBuildOptions{false, 42});
    EXPECT_DOUBLE_EQ(bothAligned->summary().plates[0].minimumMm.x, -498);
    EXPECT_DOUBLE_EQ(bothAligned->summary().plates[0].maximumMm.x, 498);
    for (const double tolerance : {-1.0, 1001.0, std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_THROW((void)buildRecognizedPlates(recognition, PlateBuildOptions{false, tolerance}), GeometryError);
    }
}

TEST(PlateRecognition, KeepsIndependentFlangeEndsWhenCloudBottomProtrudes)
{
    for (const int overhang : {20, 40})
    {
        auto cloud = observedTray();
        for (int x = 505; x <= 500 + overhang; x += 5)
        {
            for (int y = -100; y <= 100; y += 5)
            {
                cloud.points.push_back({{static_cast<double>(x), static_cast<double>(y), 0}, 0, 0});
                cloud.points.push_back({{-static_cast<double>(x), static_cast<double>(y), 0}, 0, 0});
            }
        }
        const auto recognition = recognizePlates(cloud, BeamEstimate{});
        ASSERT_GE(recognition.plates.size(), 3u);
        EXPECT_NEAR(recognition.plates[0].maximumMm.x, 500 + overhang, 0.1);
        EXPECT_NEAR(recognition.plates[1].maximumMm.x, 500, 0.1);
        EXPECT_NEAR(recognition.plates[2].maximumMm.x, 500, 0.1);
        const auto model = buildRecognizedPlates(recognition);
        EXPECT_NEAR(model->summary().plates[0].minimumMm.x, overhang <= 30 ? -500 : -540, 0.1);
        EXPECT_NEAR(model->summary().plates[0].maximumMm.x, overhang <= 30 ? 500 : 540, 0.1);
    }
}

TEST(PlateRecognition, DoesNotEstimateBottomWhenOneSideIsMissing)
{
    auto cloud = observedTray();
    std::erase_if(cloud.points,
                  [](const auto &point) { return point.position.z > 1 && point.position.y < -80; });
    const auto recognition = recognizePlates(cloud, BeamEstimate{});
    ASSERT_EQ(recognition.plates.size(), 1u);
    EXPECT_FALSE(recognition.plates.front().thicknessMm);
    EXPECT_EQ(recognition.plates.front().method, "observed_surface_thickness_unresolved");
    EXPECT_EQ(recognition.plates.front().minimumMm.y, recognition.plates.front().maximumMm.y);
}

TEST(PlateRecognition, DoesNotInventRibsForFlatCloudAndHonorsCancellation)
{
    auto cloud = observedTray();
    std::erase_if(cloud.points, [](const auto &point) { return point.position.z > 1; });
    const auto recognition = recognizePlates(cloud, BeamEstimate{});
    ASSERT_EQ(recognition.plates.size(), 1u);
    EXPECT_FALSE(recognition.plates.front().thicknessMm);
    const auto model = buildRecognizedPlates(recognition);
    EXPECT_EQ(model->summary().solidCount, 0u);
    EXPECT_EQ(model->summary().surfaceCount, 1u);
    const auto display = workbench::makeModelDisplayParts(model->mesh(), model->summary(), {});
    ASSERT_EQ(display.size(), 1u);
    EXPECT_TRUE(display.front().thicknessUnresolved);
    EXPECT_FALSE(display.front().thicknessEstimated);
    const auto file =
        std::filesystem::temp_directory_path() /
        ("unresolved-bottom-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    const auto exported = exportStep(*model, file);
    EXPECT_TRUE(exported.valid);
    EXPECT_EQ(exported.solidCount, 0u);
    EXPECT_EQ(exported.surfaceCount, 1u);
    std::filesystem::remove(file);
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_THROW((void)recognizePlates(cloud, BeamEstimate{}, cancelled.get_token()), GeometryError);
}

TEST(PlateRecognition, RejectsInconsistentThicknessBeforeBuilding)
{
    auto recognition = recognizePlates(observedTray(), BeamEstimate{});
    recognition.plates[1].thicknessMm = 99;
    EXPECT_THROW((void)buildRecognizedPlates(recognition), GeometryError);
    recognition.plates[1].thicknessMm = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)buildRecognizedPlates(recognition), GeometryError);
}

TEST(CommandService, ImportsRecognitionAndBuildsWithoutPresetParameters)
{
    const auto file =
        std::filesystem::temp_directory_path() /
        ("recognized-cloud-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".pcd");
    const auto cloud = observedTray();
    {
        std::ofstream output(file);
        output << "VERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH " << cloud.points.size()
               << "\nHEIGHT 1\nPOINTS " << cloud.points.size() << "\nDATA ascii\n";
        for (const auto &point : cloud.points)
        {
            output << point.position.x << ' ' << point.position.y << ' ' << point.position.z << '\n';
        }
    }
    CommandService service;
    const auto wait = [&](const std::string &task)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!service.task(task).finished() && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        EXPECT_EQ(service.task(task).state, TaskState::Succeeded) << service.task(task).message;
    };
    wait(service.submitImport(file, LengthUnit::Millimeter));
    std::filesystem::remove(file);
    const auto imported = service.project();
    ASSERT_TRUE(imported.recognition);
    EXPECT_FALSE(toJson(imported).contains("suggestedParameters"));
    wait(service.submitRecognizedReconstruct(false, imported.revision, 12.5));
    const auto built = service.project();
    ASSERT_TRUE(built.model);
    EXPECT_TRUE(built.model->fromPointCloud);
    EXPECT_EQ(built.model->solidCount, 5u);
    EXPECT_EQ(built.model->surfaceCount, 0u);
    const auto bottom = toJson(built)["model"]["plates"][0];
    EXPECT_NEAR(bottom["thicknessMm"].get<double>(), 12, 0.15);
    EXPECT_EQ(bottom["method"], "inferred_side_plate_max");
    EXPECT_EQ(bottom["thicknessMm"], toJson(imported)["recognition"]["plates"][0]["thicknessMm"]);
    EXPECT_DOUBLE_EQ(toJson(built)["model"]["recognitionOptions"]["endOverhangToleranceMm"].get<double>(), 12.5);
    EXPECT_THROW((void)service.submitRecognizedReconstruct(true, imported.revision), GeometryError);
    wait(service.submitRecognizedReconstruct(true, built.revision));
    EXPECT_EQ(service.project().model->solidCount, 9u);
    EXPECT_EQ(service.project().model->surfaceCount, 0u);
}
} // namespace
