/* 文件描述：仅用俯视可见底板和顶边验证两类焊件、直线圆弧、连接假设、障碍物及命令历史。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <random>
#include <thread>

#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "commands/Json.h"
#include "commands/SectionPlane.h"
#include "geometry/ModelTransform.h"
#include "geometry/ModelExchange.h"
#include "geometry/OtherModelRecognition.h"
#include "geometry/TBarRecognition.h"

namespace reverse
{
namespace
{
double roundedDistance(double x, double z, double halfX, double halfZ, double radius)
{
    const double qx = std::abs(x) - (halfX - radius), qz = std::abs(z) - (halfZ - radius);
    return std::hypot(std::max(qx, 0.0), std::max(qz, 0.0)) + std::min(std::max(qx, qz), 0.0) - radius;
}
CloudData weldment(bool ring, bool obstacles = false, bool gap = false)
{
    CloudData cloud;
    cloud.originMm = {1200000, -2300000, 1400};
    std::mt19937 random(79);
    std::normal_distribution<double> noise(0, 0.025);
    const double yaw = 0.27, tilt = 0.04;
    const auto add = [&](double x, double z, double height) {
        x += noise(random);
        z += noise(random);
        height += noise(random);
        const double a = std::cos(yaw) * x - std::sin(yaw) * z;
        const double b = std::sin(yaw) * x + std::cos(yaw) * z;
        cloud.points.push_back({{a, std::cos(tilt) * b - std::sin(tilt) * height,
            std::sin(tilt) * b + std::cos(tilt) * height}, 0, cloud.points.size()});
    };
    for (int x = -350; x <= 350; x += 2)
    {
        for (int z = -260; z <= 260; z += 2)
        {
            double height = 0;
            if (ring)
            {
                if (roundedDistance(x, z, 210, 170, 40) <= 0 &&
                    roundedDistance(x, z, 198, 158, 28) >= 0)
                {
                    height = 100;
                }
                if (std::abs(x) >= 210 && std::abs(x) <= 310 && std::abs(z) <= 6)
                {
                    height = 65;
                }
            }
            else
            {
                if (std::abs(x) <= 270 && std::abs(std::abs(z) - 180) <= 6)
                {
                    height = 100 - std::max(0, std::abs(x) - 220) * 0.5;
                }
                if (std::abs(std::abs(x) - 176) <= 6 && std::abs(z) <= 174)
                {
                    height = 60 - std::max(0, std::abs(z) - 125) * 0.4;
                }
                // 可见长圆孔；孔内没有底板点，也不生成背面或侧面。
                if (std::hypot(std::max(std::abs(x) - 30, 0), z) < 60)
                {
                    continue;
                }
            }
            if (gap && std::abs(x) < 16 && z > 160 && height > 0)
            {
                continue;
            }
            if (obstacles && std::abs(x) <= 30 && std::abs(z) <= 25)
            {
                height = 25;
            }
            add(x, z, height);
        }
    }
    if (obstacles)
    {
        for (int x = 420; x <= 480; x += 2)
        {
            for (int z = -30; z <= 30; z += 2)
            {
                add(x, z, 45);
            }
        }
    }
    cloud.sourcePointCount = cloud.points.size();
    return cloud;
}
PlateBuildOptions otherOptions()
{
    PlateBuildOptions options;
    options.type = "other";
    return options;
}
struct Scratch
{
    std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("rm-other-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Scratch()
    {
        std::filesystem::create_directory(path);
    }
    ~Scratch()
    {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};
void done(CommandService &service, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto task = service.task(id);
        if (task.finished())
        {
            if (!task.result.ok())
            {
                throw std::runtime_error(task.errorCode + ": " + task.message);
            }
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    (void)service.cancel(id);
    throw std::runtime_error("other model command timed out");
}
}

TEST(OtherModelRecognition, StraightRibsKeepFullBaseVisibleSlotAndIndependentHeight)
{
    const auto cloud = weldment(false);
    const auto recognition = recognizeOtherModel(cloud);
    ASSERT_EQ(recognition.plates.size(), 5);
    EXPECT_EQ(recognition.type, "other");
    const auto &base = recognition.plates.front();
    ASSERT_TRUE(base.thicknessMm);
    EXPECT_DOUBLE_EQ(*base.thicknessMm, 1);
    EXPECT_DOUBLE_EQ(base.maximumMm.y, 0);
    EXPECT_DOUBLE_EQ(base.minimumMm.y, -1);
    EXPECT_NEAR((base.maximumMm.x - base.minimumMm.x) * (base.maximumMm.z - base.minimumMm.z), 700 * 520, 1000);
    ASSERT_EQ(base.holes.size(), 1);
    EXPECT_NEAR(base.holes.front().radiusMm, 60, 6);
    EXPECT_NEAR(base.holes.front().straightLengthMm, 60, 8);
    for (std::size_t i = 1; i < recognition.plates.size(); ++i)
    {
        const auto &plate = recognition.plates[i];
        ASSERT_TRUE(plate.thicknessMm);
        EXPECT_NEAR(*plate.thicknessMm, 12, 0.5);
        EXPECT_EQ(plate.minimumMm.y, 0);
        EXPECT_FALSE(plate.outline.empty());
    }
    const auto model = buildRecognizedPlates(recognition, otherOptions());
    EXPECT_EQ(model->summary().solidCount, 5);
    EXPECT_EQ(model->summary().surfaceCount, 0);
    EXPECT_EQ(associateModelCloud(*model, cloud)->summary().reconstructableParts.size(), 5);
}

TEST(OtherModelRecognition, RoundedEnclosureKeepsInnerVoidAndAnalyticArcs)
{
    const auto recognition = recognizeOtherModel(weldment(true));
    const auto wall = std::find_if(recognition.plates.begin(), recognition.plates.end(), [](const auto &plate) {
        return !plate.uprightFootprint.empty();
    });
    ASSERT_NE(wall, recognition.plates.end());
    ASSERT_EQ(wall->uprightFootprint.size(), 2);
    ASSERT_TRUE(wall->thicknessMm);
    EXPECT_NEAR(*wall->thicknessMm, 12, 0.5); // 测四边宽度，不把约 100 mm 的高度当板厚。
    std::size_t arcs = 0;
    for (const auto &loop : wall->uprightFootprint)
    {
        for (const auto &edge : loop)
        {
            arcs += edge.arcMiddleMm.has_value();
        }
    }
    EXPECT_GE(arcs, 4);
    const auto model = buildRecognizedPlates(recognition, otherOptions());
    EXPECT_EQ(model->summary().solidCount, 4);
    EXPECT_EQ(model->summary().surfaceCount, 0);
    // 中空围板应约 1.6e6 mm³；若错误封顶/填心则超过 1.4e7 mm³。
    EXPECT_GT(model->summary().volumeMm3, 1300000);
    EXPECT_LT(model->summary().volumeMm3, 2400000);
    Scratch directory;
    const auto exported = exportStep(*model, directory.path / "round-weldment.step");
    EXPECT_TRUE(exported.valid);
    EXPECT_EQ(exported.solidCount, 4);
    EXPECT_EQ(exported.surfaceCount, 0);
    const auto restored = loadCadModel(directory.path / "round-weldment.step");
    EXPECT_NEAR(restored->summary().volumeMm3, model->summary().volumeMm3, 0.1);
    (void)exportCwrm(*model, directory.path / "round-weldment.cwrm");
    const auto cwrm = loadCadModel(directory.path / "round-weldment.cwrm");
    EXPECT_EQ(cwrm->summary().solidCount, 4);
    EXPECT_EQ(cwrm->summary().surfaceCount, 0);
    EXPECT_NEAR(cwrm->summary().volumeMm3, model->summary().volumeMm3, 0.1);
    const auto associated = associateModelCloud(*model, weldment(true));
    EXPECT_FALSE((std::find(associated->summary().reconstructableParts.begin(), associated->summary().reconstructableParts.end(), wall->id) != associated->summary().reconstructableParts.end()));
    LocalReconstruction request;
    request.partIds = {wall->id};
    EXPECT_THROW((void)reconstructLocalParts(*associated, weldment(true), request), GeometryError);
    request.type = "box";
    const auto box = reconstructLocalParts(*associated, weldment(true), request);
    ASSERT_TRUE(box->summary().obstacleParts.contains(wall->id));
    EXPECT_TRUE(box->summary().obstacleParts.at(wall->id).boundingBox);
}

TEST(OtherModelRecognition, RingThicknessEditPreservesHeightMiddleAndTransformedPosition)
{
    auto recognition = recognizeOtherModel(weldment(true));
    auto wall = std::find_if(recognition.plates.begin(), recognition.plates.end(),
        [](const auto &p) { return !p.uprightFootprint.empty(); });
    ASSERT_NE(wall, recognition.plates.end());
    const auto id = wall->id;
    wall->thicknessMm.reset(); // 兼容之前仅有连接组轮廓、尚无厚度标签的模型。
    const auto source = buildRecognizedPlates(recognition, otherOptions());
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::X;
    rotation.angleDegrees = 23;
    rotation.centerMm = recognition.frame.originMm;
    const auto moved = transformModelParts(*source, {id}, rotation);
    const auto oldInfo = inspectModelThickness(*moved, {id}).at(id);
    const auto edited = changeModelThickness(*moved, {{id}, 18});
    const auto newInfo = inspectModelThickness(*edited, {id}).at(id);
    EXPECT_NEAR(newInfo.thicknessMm, 18, 1e-6);
    EXPECT_NEAR(std::hypot(newInfo.centerMm.x - oldInfo.centerMm.x, newInfo.centerMm.y - oldInfo.centerMm.y,
        newInfo.centerMm.z - oldInfo.centerMm.z), 0, 1e-6);
    const auto &updated = *std::find_if(edited->summary().plates.begin(), edited->summary().plates.end(),
        [&](const auto &p) { return p.id == id; });
    EXPECT_DOUBLE_EQ(updated.minimumMm.y, wall->minimumMm.y);
    EXPECT_DOUBLE_EQ(updated.maximumMm.y, wall->maximumMm.y);
    EXPECT_EQ(updated.uprightFootprint.size(), 2);
    EXPECT_EQ(updated.uprightFootprint[0].size(), 8);
    EXPECT_EQ(updated.uprightFootprint[1].size(), 8);
    const auto restored = changeModelThickness(*edited, {{id}, oldInfo.thicknessMm});
    EXPECT_NEAR(restored->summary().volumeMm3, moved->summary().volumeMm3, 0.01);
    EXPECT_THROW((void)changeModelThickness(*edited, {{id}, 100}), GeometryError);
    EXPECT_FALSE(wall->thicknessMm);
    EXPECT_TRUE(moved->summary().thicknessOverrides.empty());
}

TEST(OtherModelRecognition, UnknownBaseCanBecomeSolidAndKeepsObservedContactPlaneAndSlot)
{
    auto recognition = recognizeOtherModel(weldment(false));
    auto &base = recognition.plates.front();
    base.minimumMm.y = 0;
    base.thicknessMm.reset();
    const auto source = buildRecognizedPlates(recognition, otherOptions());
    EXPECT_EQ(source->summary().surfaceCount, 1);
    EXPECT_DOUBLE_EQ(inspectModelThickness(*source, {base.id}).at(base.id).thicknessMm, 1);
    const auto defaulted = changeModelThickness(*source, {{base.id}, 1});
    EXPECT_EQ(defaulted->summary().surfaceCount, 0);
    EXPECT_EQ(defaulted->summary().solidCount, 5);
    const auto thickened = changeModelThickness(*defaulted, {{base.id}, 8});
    EXPECT_NEAR(inspectModelThickness(*thickened, {base.id}).at(base.id).thicknessMm, 8, 1e-6);
    const auto &updated = thickened->summary().plates.front();
    EXPECT_DOUBLE_EQ(updated.minimumMm.y, -8);
    EXPECT_DOUBLE_EQ(updated.maximumMm.y, 0);
    EXPECT_EQ(updated.holes.size(), base.holes.size());
    for (const auto &point : thickened->mesh().keyPoints)
    {
        if (point.partIndex != 0) continue;
        const auto local = inverseTransformPoint(recognition.frame, point.positionMm);
        EXPECT_TRUE(std::abs(local.y) < 1e-6 || std::abs(local.y + 8) < 1e-6);
    }
    const auto back = changeModelThickness(*thickened, {{base.id}, 1});
    EXPECT_NEAR(back->summary().volumeMm3, defaulted->summary().volumeMm3, 0.01);
    EXPECT_EQ(source->summary().surfaceCount, 1);
    EXPECT_TRUE(source->summary().thicknessOverrides.empty());
}

TEST(OtherModelRecognition, VisibleGapIsNotBridgedAndObstaclesRemainSeparate)
{
    const auto gapRecognition = recognizeOtherModel(weldment(false, false, true));
    EXPECT_EQ(gapRecognition.plates.size(), 6);
    const auto cloud = weldment(true, true);
    const auto recognition = recognizeOtherModel(cloud);
    auto model = buildRecognizedPlates(recognition, otherOptions());
    model = reconstructModelObstacles(*model, cloud, recognition);
    EXPECT_GE(model->summary().obstacleParts.size(), 2);
    EXPECT_EQ(model->summary().plates.size(), 4);
}

TEST(OtherModelRecognition, CommandsHistoryOptionsAndBottomPlane)
{
    Scratch directory;
    (void)exportPointCloud(weldment(false), directory.path / "weldment.pcd");
    CommandService service;
    done(service, service.submitImport(directory.path / "weldment.pcd"));
    done(service, service.submitLine("model.reconstruct --type other"));
    const auto built = service.project();
    ASSERT_TRUE(built.model);
    ASSERT_TRUE(built.recognition);
    EXPECT_EQ(built.recognition->type, "other");
    ASSERT_TRUE(built.committedRecognitionOptions);
    EXPECT_EQ(built.committedRecognitionOptions->type, "other");
    ASSERT_FALSE(built.warnings.empty());
    EXPECT_TRUE(built.warnings.front().starts_with("其他模型："));
    EXPECT_TRUE(bottomSectionPlane(built));
    done(service, service.submitLine("project.undo"));
    EXPECT_FALSE(service.project().model);
    done(service, service.submitLine("project.redo"));
    EXPECT_EQ(service.project().model, built.model);
    auto options = otherOptions();
    options.mirrorSymmetry = true;
    EXPECT_THROW(validateTBarOptions(options), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)recognizeOtherModel(weldment(false), stop.get_token()), GeometryError);
    EXPECT_THROW((void)recognizeOtherModel(CloudData{}), GeometryError);
}
}
