/* 文件描述：用尺寸已知的 T 排点云验证直/斜板、异形槽口、背景过滤和命令重放。 */
#include <chrono>
#include <cmath>
#include <filesystem>
#include <numbers>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "commands/SectionPlane.h"
#include "geometry/ModelTransform.h"
#include "geometry/TBarRecognition.h"

namespace reverse
{
namespace
{
CloudData tbarCloud(double angle = 0, bool irregular = false, bool ground = true, bool shortWeb = false)
{
    CloudData cloud;
    cloud.originMm = {2300, -1400, 800};
    std::mt19937 rng(73);
    std::normal_distribution<double> noise(0, .08);
    const double a = angle * std::numbers::pi / 180, yaw = .3;
    auto add = [&](double x, double y, double z) {
        x += noise(rng);
        y += noise(rng);
        z += noise(rng);
        cloud.points.push_back({{std::cos(yaw) * x - std::sin(yaw) * y, std::sin(yaw) * x + std::cos(yaw) * y, z},
                                0,
                                cloud.points.size()});
    };
    for (int x = -400; x <= 400; x += 4)
    {
        for (int y = -90; y <= 90; y += 3)
            add(x, y, 20);
        for (int z = 0; z <= 20; z += 2)
        {
            add(x, -90, z);
            add(x, 90, z);
        }
        if (shortWeb && std::abs(x) > 350)
            continue;
        const double height = irregular ? (x > 28 && x < 64 ? 220 : 300) : 300;
        for (int h = 0; h <= height; h += 3)
            for (double n : {-6., 6.})
            {
                const double z = 20 + h * std::cos(a) - n * std::sin(a);
                if (z >= 20)
                    add(x, h * std::sin(a) + n * std::cos(a), z);
            }
        for (int n = -6; n <= 6; n += 2)
            add(x, height * std::sin(a) + n * std::cos(a), 20 + height * std::cos(a) - n * std::sin(a));
    }
    for (int x : {-400, 400})
        for (int y = -90; y <= 90; y += 3)
            for (int z = 0; z <= 20; z += 2)
                add(x, y, z);
    if (ground)
    {
        for (int x = -550; x <= 550; x += 8)
            for (int y = -350; y <= 350; y += 8)
                if (!(x > -400 && x < 400 && y > -90 && y < 90))
                    add(x, y, 0);
    }
    else
    {
        for (int x : {-260, 260})
            for (int y = -260; y <= 260; y += 4)
                for (int dx = -10; dx <= 10; dx += 4)
                    add(x + dx, y, -10);
    }
    return cloud;
}
PlateBuildOptions tbarOptions()
{
    PlateBuildOptions o;
    o.type = "tbar";
    return o;
}
PlateRecognition recognize(const CloudData &cloud, PlateBuildOptions o)
{
    return recognizeTBar(cloud, estimateBeam(cloud), o);
}
} // namespace
TEST(TBarRecognition, RegularGroundHasTwoMeasuredSolidsAndExcludesGround)
{
    auto cloud = tbarCloud();
    auto o = tbarOptions();
    const auto r = recognize(cloud, o);
    ASSERT_EQ(r.plates.size(), 2);
    ASSERT_TRUE(r.plates[0].thicknessMm);
    ASSERT_TRUE(r.plates[1].thicknessMm);
    EXPECT_NEAR(*r.plates[0].thicknessMm, 20, .6);
    EXPECT_NEAR(*r.plates[1].thicknessMm, 12, .15);
    auto model = buildRecognizedPlates(r, o);
    EXPECT_EQ(model->summary().solidCount, 2);
    EXPECT_NEAR(model->summary().volumeMm3, 5760000, 200000);
    EXPECT_EQ(associateModelCloud(*model, cloud)->summary().reconstructableParts.size(), 2);
}
TEST(TBarRecognition, InclinedWebRetainsTwentyDegreesAndMeetsBase)
{
    auto cloud = tbarCloud(20);
    auto o = tbarOptions();
    o.perpendicularHypothesis = false;
    const auto r = recognize(cloud, o);
    ASSERT_EQ(r.plates.size(), 2);
    const auto &base = r.plateFrames.at(0).second, &web = r.plateFrames.at(1).second;
    const double cosine = base.yAxis.x * web.yAxis.x + base.yAxis.y * web.yAxis.y + base.yAxis.z * web.yAxis.z;
    EXPECT_NEAR(std::acos(cosine) * 180 / std::numbers::pi, 20, 1.0);
    auto model = buildRecognizedPlates(r, o);
    EXPECT_EQ(model->summary().solidCount, 2);
    for (std::size_t i = 0; i < model->mesh().triangles.size(); ++i)
        if (model->mesh().trianglePartIndices[i] == 1)
            for (auto index : model->mesh().triangles[i])
                EXPECT_GE(inverseTransformPoint(base, model->mesh().vertices[index]).y, -1e-6);
}
TEST(TBarRecognition, IrregularNotchSurvivesAndRackIsExcluded)
{
    auto cloud = tbarCloud(0, true, false);
    auto o = tbarOptions();
    o.onGround = false;
    o.regularUpperSurface = false;
    o.projectionOntoBorder = true;
    const auto r = recognize(cloud, o);
    ASSERT_EQ(r.plates.size(), 2);
    EXPECT_GT(r.plates[1].outline.size(), 6);
    auto irregular = buildRecognizedPlates(r, o);
    EXPECT_EQ(irregular->summary().solidCount, 2);
    o.regularUpperSurface = true;
    auto regular = buildRecognizedPlates(recognize(cloud, o), o);
    EXPECT_GT(regular->summary().volumeMm3 - irregular->summary().volumeMm3, 20000);
}
TEST(TBarRecognition, BaseWidthLimitRejectsWideSupport)
{
    auto cloud = tbarCloud();
    auto o = tbarOptions();
    o.maximumBaseWidthMm = 120;
    EXPECT_THROW(recognize(cloud, o), GeometryError);
}
TEST(TBarRecognition, BorderProjectionChangesEndConnection)
{
    auto cloud = tbarCloud(0, false, true, true);
    auto o = tbarOptions();
    auto direct = buildRecognizedPlates(recognize(cloud, o), o);
    o.projectionOntoBorder = true;
    auto border = buildRecognizedPlates(recognize(cloud, o), o);
    EXPECT_GT(border->summary().volumeMm3, direct->summary().volumeMm3 + 100000);
}
TEST(TBarRecognition, RejectsInvalidOptionsAndHonorsCancellation)
{
    auto o = tbarOptions();
    o.maximumBaseWidthMm = 0;
    EXPECT_THROW(validateTBarOptions(o), GeometryError);
    o = tbarOptions();
    o.mirrorSymmetry = true;
    EXPECT_THROW(validateTBarOptions(o), GeometryError);
    o = tbarOptions();
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(recognizeTBar(tbarCloud(), {}, o, stop.get_token()), GeometryError);
}
TEST(TBarRecognition, SeveralSeparatedWorkpiecesKeepIndependentFrames)
{
    auto cloud = tbarCloud(0, false, false);
    const auto other = tbarCloud(0, false, false);
    for (auto point : other.points)
    {
        point.position.x -= std::sin(.3) * 700;
        point.position.y += std::cos(.3) * 700;
        point.sourceIndex = cloud.points.size();
        cloud.points.push_back(point);
    }
    auto o = tbarOptions();
    o.onGround = false;
    const auto r = recognize(cloud, o);
    ASSERT_EQ(r.plates.size(), 4);
    EXPECT_EQ(buildRecognizedPlates(r, o)->summary().solidCount, 4);
}
TEST(TBarRecognition, UnknownBaseThicknessRemainsSurface)
{
    auto cloud = tbarCloud();
    std::erase_if(cloud.points, [](const auto &p) { return p.position.z < 19.5; });
    auto o = tbarOptions();
    o.onGround = false;
    const auto r = recognize(cloud, o);
    ASSERT_EQ(r.plates.size(), 2);
    EXPECT_FALSE(r.plates[0].thicknessMm);
    const auto model = buildRecognizedPlates(r, o);
    EXPECT_EQ(model->summary().solidCount, 1);
    EXPECT_EQ(model->summary().surfaceCount, 1);
}
TEST(TBarRecognition, SlotLimitControlsUnsupportedSpan)
{
    auto cloud = tbarCloud();
    std::erase_if(cloud.points, [](const auto &p) {
        const double x = p.position.x * std::cos(.3) + p.position.y * std::sin(.3);
        return p.position.z > 25 && x > 0 && x < 35;
    });
    auto o = tbarOptions();
    o.regularUpperSurface = false;
    EXPECT_NO_THROW(recognize(cloud, o));
    o.maximumSlotWidthMm = 10;
    EXPECT_THROW(recognize(cloud, o), GeometryError);
}
TEST(TBarRecognition, CommandUndoReplayExportAndBottomView)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("rm-tbar-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } cleanup{dir};
    const auto input = dir / "inclined.pcd";
    auto cloud = tbarCloud(20);
    cloud.sourcePointCount = cloud.points.size();
    (void)exportPointCloud(cloud, input);
    CommandService service;
    auto done = [&](const std::string &id) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(40);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto task = service.task(id);
            if (task.finished())
            {
                if (!task.result.ok())
                    throw std::runtime_error(task.errorCode + ": " + task.message);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        (void)service.cancel(id);
        throw std::runtime_error("Tbar command timed out");
    };
    done(service.submitImport(input));
    auto o = tbarOptions();
    o.perpendicularHypothesis = false;
    done(service.submitRecognizedReconstruct(o, service.project().revision));
    const auto built = service.project();
    ASSERT_TRUE(built.model);
    EXPECT_EQ(built.model->solidCount, 3); // Two plates and the scanned ground envelope.
    ASSERT_EQ(built.model->obstacleParts.size(), 1);
    EXPECT_EQ(built.model->recognizedPartFrames.size(), 2);
    ASSERT_TRUE(bottomSectionPlane(built));
    const auto plane = *bottomSectionPlane(built);
    EXPECT_NEAR(std::abs(plane.normal.z), 1, .001);
    done(service.submitExport(dir / "inclined.step", built.revision));
    const auto imported = loadCadModel(dir / "inclined.step");
    EXPECT_EQ(imported->summary().solidCount, 3);
    EXPECT_NEAR(imported->summary().volumeMm3, built.model->volumeMm3, .01);
    done(service.submitLine("project.undo"));
    EXPECT_FALSE(service.project().model);
    done(service.submitLine("project.redo"));
    EXPECT_EQ(service.project().model, built.model);
    EXPECT_EQ(service.project().committedRecognitionOptions, o);
    done(service.submitLine(
        "model.reconstruct --type tbar --on-ground true --regular-upper-surface true --perpendicular false "
        "--projection-onto-border false --max-slot-width 50 --max-base-width 250"));
    EXPECT_EQ(service.project().committedRecognitionOptions, o);
    EXPECT_NEAR(service.project().model->volumeMm3, built.model->volumeMm3, .001);
}
} // namespace reverse
