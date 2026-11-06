/*
 * 文件描述：验证俯视线扫的自动容差、不同板高、短筋板、断带和精确圆弧轮廓。
 * 可选真实 PCD 回归由显式环境目录提供，输出识别摘要与 STEP 回读结果，不启动 GUI。
 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <random>

#include <gtest/gtest.h>

#include "commands/Json.h"
#include "geometry/Geometry.h"
#include "geometry/ScanProfile.h"

namespace
{
using namespace reverse;
CloudData scan(bool shortRib = false, bool gap = false, bool curved = false)
{
    CloudData cloud;
    std::mt19937 random(619);
    std::uniform_real_distribution<double> noise(-0.02, 0.02);
    const auto add = [&](double x, double y, double z)
    { cloud.points.push_back({{x + noise(random), y + noise(random), z + noise(random)}, 0, cloud.points.size()}); };
    for (int x = -500; x <= 500; x += 5)
    {
        for (int y = -100; y <= 100; y += 5) add(x, y, 0);
        for (int w = 0; w <= 10; ++w)
        {
            add(x, -100 + w * 0.8, 100);
            add(x, 88 + w * 1.2, 70); // 两侧翼板也允许不同高度。
        }
    }
    for (int y = -90; y <= (shortRib ? 15 : 86); y += 2)
    {
        if (gap && y > -20 && y < 20) continue;
        double height = 35; // 明显低于最高翼板，旧 65% 高度筛选会遗漏。
        if (curved)
        {
            height = 65;
            if (std::abs(y) < 24) height -= std::sqrt(24.0 * 24 - y * y);
        }
        for (int w = 0; w <= 10; ++w) add(-250 - 3 + w * 0.6, y, height);
    }
    return cloud;
}

TEST(OverheadScan, RecognizesLowRibsAndUnequalFlangeHeightsWithAutomaticTolerance)
{
    const auto r = recognizePlates(scan(), BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 4u);
    EXPECT_NEAR(r.plates[1].maximumMm.y, 70, 0.15);
    EXPECT_NEAR(r.plates[2].maximumMm.y, 100, 0.15);
    EXPECT_NEAR(r.plates[3].maximumMm.y, 35, 0.15);
    EXPECT_NEAR(*r.plates[3].thicknessMm, 6, 0.15);
    EXPECT_GT(r.surfaceNoiseMm, 0);
    EXPECT_LT(r.surfaceNoiseMm, 0.1);
    EXPECT_GT(r.scanSpacingMm, 0);
    EXPECT_GE(r.contourToleranceMm, 3 * r.surfaceNoiseMm);
    EXPECT_EQ(buildRecognizedPlates(r)->summary().solidCount, 4u);
}

TEST(OverheadScan, SeparatesSlowWebWarpFromScanNoise)
{
    auto cloud = scan();
    for (auto &p : cloud.points)
        if (p.position.z < 1) p.position.z += 6 * std::cos(p.position.x / 500 * std::numbers::pi);
    const auto r = recognizePlates(cloud, BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 4u);
    EXPECT_LT(r.surfaceNoiseMm, 0.15);
    EXPECT_LT(r.contourToleranceMm, 2);
    EXPECT_NEAR(r.plates.back().maximumMm.y, 35, 0.15);
}

TEST(OverheadScan, FindsFlangesDespiteWideLowerBackgroundAndPcaYaw)
{
    auto cloud = scan();
    for (int x = -500; x <= 500; x += 5)
        for (int y = -500; y <= 500; y += 5)
            if (std::abs(y) > 130) cloud.points.push_back({{double(x), double(y), -80}, 0, cloud.points.size()});
    // 背景不超过主体腹板的采样权重；避免将多个工件误称为同一板件。
    std::erase_if(cloud.points, [](const auto &p) { return p.position.z < -50 && p.sourceIndex % 5 != 0; });
    BeamEstimate initial;
    const double yaw = 0.015;
    initial.frame.xAxis = {std::cos(yaw), std::sin(yaw), 0};
    initial.frame.yAxis = {-std::sin(yaw), std::cos(yaw), 0};
    const auto r = recognizePlates(cloud, initial);
    ASSERT_EQ(r.plates.size(), 4u);
    EXPECT_LT(std::abs(r.frame.xAxis.y), 0.005);
    EXPECT_NEAR(r.plates.front().maximumMm.z - r.plates.front().minimumMm.z, 180, 1);
    EXPECT_NEAR(*r.plates[1].thicknessMm, 12, 0.3);
    EXPECT_NEAR(*r.plates[2].thicknessMm, 8, 0.3);
}

TEST(OverheadScan, DoesNotExtendShortRibAcrossUnobservedGap)
{
    const auto r = recognizePlates(scan(true), BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 4u);
    const auto &rib = r.plates.back();
    EXPECT_NEAR(rib.minimumMm.z, -14, 2);
    EXPECT_GT(rib.minimumMm.z - r.plates[0].minimumMm.z, 60);
    EXPECT_LT(rib.maximumMm.z - rib.minimumMm.z, 110);
    EXPECT_TRUE(std::any_of(r.warnings.begin(), r.warnings.end(), [](const auto &s) {return s.find("端部间隙") != std::string::npos;}));
}

TEST(OverheadScan, SplitsDisconnectedRibAtSameStation)
{
    const auto r = recognizePlates(scan(false, true), BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 5u);
    EXPECT_GT(r.plates[4].minimumMm.z - r.plates[3].maximumMm.z, 35);
    EXPECT_NEAR(r.plates[3].minimumMm.x, r.plates[4].minimumMm.x, 0.1);
    EXPECT_EQ(buildRecognizedPlates(r)->summary().solidCount, 5u);
}

TEST(OverheadScan, PreservesVisibleArcInModelMirrorAndStep)
{
    const auto r = recognizePlates(scan(false, false, true), BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 4u);
    const auto &rib = r.plates.back();
    ASSERT_FALSE(rib.outline.empty());
    EXPECT_TRUE(std::any_of(rib.outline.begin(), rib.outline.end(), [](const auto &s) { return s.arcMiddleMm.has_value(); }));
    const auto model = buildRecognizedPlates(r);
    EXPECT_EQ(model->summary().solidCount, 4u);
    auto rectangular = r;
    rectangular.plates.back().outline.clear();
    const auto box = buildRecognizedPlates(rectangular);
    EXPECT_LT(model->summary().volumeMm3, box->summary().volumeMm3 - 1000);
    const auto mirrored = buildRecognizedPlates(r, {true, 30});
    EXPECT_EQ(mirrored->summary().solidCount, 7u);
    EXPECT_FALSE(mirrored->summary().plates.back().outline.empty());
    const auto target = std::filesystem::temp_directory_path() /
        ("overhead-arc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    const auto exported = exportStep(*mirrored, target);
    EXPECT_TRUE(exported.valid);
    EXPECT_EQ(exported.solidCount, 7u);
    std::ifstream stepFile(target);
    const std::string stepText((std::istreambuf_iterator<char>(stepFile)), std::istreambuf_iterator<char>());
    EXPECT_NE(stepText.find("CIRCLE("), std::string::npos); // STEP 保留解析圆弧，不是密集折线。
    stepFile.close();
    std::filesystem::remove(target);
    EXPECT_THROW((void)evaluateDeviation(scan(false, false, true), *model), GeometryError);
}

TEST(OverheadScan, PreservesTwoVisibleUpperQuarterCircleNotchesAndLeavesBottomStraight)
{
    auto cloud = scan();
    for (auto &p : cloud.points)
    {
        if (std::abs(p.position.x + 250) > 4 || p.position.z < 1 || std::abs(p.position.y) > 91) continue;
        const double distance = std::min(p.position.y + 90, 86 - p.position.y);
        p.position.z = distance < 28 ? 65 - std::sqrt(std::max(0.0, 28*28-distance*distance)) : 65;
    }
    const auto r = recognizePlates(cloud, BeamEstimate{});
    ASSERT_EQ(r.plates.size(), 4u);
    const auto &rib = r.plates.back();
    ASSERT_GE(rib.outline.size(), 5u);
    EXPECT_GE(std::count_if(rib.outline.begin(), rib.outline.end(), [](const auto &s) { return s.arcMiddleMm.has_value(); }), 2);
    EXPECT_NEAR(rib.outline.back().firstMm.y, r.plates.front().maximumMm.y, 1e-6);
    EXPECT_NEAR(rib.outline.back().lastMm.y, r.plates.front().maximumMm.y, 1e-6);
    EXPECT_FALSE(rib.outline.back().arcMiddleMm);
    const auto withWebHoles = buildRecognizedPlates(r, {false, 0, true, 5});
    const auto withoutWebHoles = buildRecognizedPlates(r, {false, 0, false, 5});
    EXPECT_DOUBLE_EQ(withWebHoles->summary().volumeMm3, withoutWebHoles->summary().volumeMm3);
    auto rectangle = r;rectangle.plates.back().outline.clear();
    EXPECT_LT(withWebHoles->summary().volumeMm3, buildRecognizedPlates(rectangle)->summary().volumeMm3 - 2000);
}

TEST(ScanProfile, FitsQuarterAndHalfCircleWithoutReplacingThemWithChords)
{
    for (double sweep : {std::numbers::pi / 2, std::numbers::pi})
    {
        std::vector<Vec3> points;
        for (int i = 0; i <= 32; ++i)
        {
            const double a = sweep * i / 32;
            points.push_back({20 * std::cos(a), 20 * std::sin(a), 0});
        }
        const auto fitted = detail::fitScanProfile(points, 0.1);
        ASSERT_EQ(fitted.size(), 1u);
        ASSERT_TRUE(fitted.front().arcMiddleMm);
        EXPECT_NEAR(std::hypot(fitted.front().arcMiddleMm->x, fitted.front().arcMiddleMm->y), 20, 1e-9);
    }
    std::vector<Vec3> line;
    for (int i = 0; i <= 30; ++i) line.push_back({double(i), i % 2 ? 0.01 : -0.01, 0});
    const auto fitted = detail::fitScanProfile(line, 0.1);
    ASSERT_EQ(fitted.size(), 1u);
    EXPECT_FALSE(fitted.front().arcMiddleMm);
    std::stop_source cancelled; cancelled.request_stop();
    EXPECT_THROW((void)detail::fitScanProfile(line, 0.1, cancelled.get_token()), GeometryError);
}

TEST(OverheadScan, DISABLED_RealClouds)
{
    const auto input = std::getenv("RM_HBEAM_SAMPLE_DIR");
    const auto output = std::getenv("RM_HBEAM_REPORT_DIR");
    if (!input || !output) GTEST_SKIP() << "Set RM_HBEAM_SAMPLE_DIR and RM_HBEAM_REPORT_DIR for read-only sample verification.";
    std::filesystem::create_directories(output);
    nlohmann::json report = nlohmann::json::array();
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(input))
    {
        if (entry.path().extension() != ".pcd") continue;
        const auto start = std::chrono::steady_clock::now();
        const auto cloud = loadPointCloud(entry.path(), LengthUnit::Millimeter);
        const auto r = recognizePlates(*cloud, estimateBeam(*cloud));
        ASSERT_FALSE(r.plates.empty());
        const auto model = buildRecognizedPlates(r);
        if (entry.path().filename() == L"reverse_modeling_result-20260922-08-17-37.pcd" ||
            entry.path().filename() == L"reverse_modeling_result-20260922-08-17-37（最普通H型钢）.pcd")
            EXPECT_EQ(model->summary().solidCount, 7u);
        else if (entry.path().filename() == L"孔.pcd")
        {
            EXPECT_EQ(model->summary().solidCount, 5u);
            EXPECT_EQ(model->summary().plates.front().holes.size(), 24u);
        }
        ProjectSnapshot snapshot; snapshot.recognition = r;
        snapshot.model = std::shared_ptr<const ModelSummary>(model, &model->summary());
        auto item = toJson(snapshot);
        const auto name = entry.path().filename().u8string();
        item["file"] = std::string(reinterpret_cast<const char *>(name.data()), name.size());
        item["points"] = cloud->points.size();
        item["seconds"] = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        const auto step = std::filesystem::path(output) / ("scan-sample-" + std::to_string(++count) + ".step");
        ASSERT_FALSE(std::filesystem::exists(step)) << "Use a fresh report directory.";
        const auto exported = exportStep(*model, step);
        EXPECT_TRUE(exported.valid);
        EXPECT_EQ(exported.solidCount, model->summary().solidCount);
        item["stepValid"] = exported.valid;
        report.push_back(item);
    }
    EXPECT_GT(count, 0u);
    std::ofstream(std::filesystem::path(output) / "recognition.json") << report.dump(2);
}
}
