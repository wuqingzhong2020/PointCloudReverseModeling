/* 文件描述：局部重建只替换选中部件，验证裁剪后关联、规则拟合、原子失败、历史及录制回放。 */
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "geometry/CloudEditing.h"
#include "geometry/Geometry.h"

namespace
{
using namespace reverse;
struct Fixture
{
    CloudData cloud;
    std::shared_ptr<const Model> model;
    Fixture()
    {
        PlateRecognition recognition;
        recognition.plates = {{"first", {0, 0, 0}, {100, 80, 10}, 2, 10},
                              {"other", {0, 150, 0}, {100, 230, 12}, 2, 12}};
        for (int y = 0; y <= 80; y += 2)
            for (int x = 0; x <= 100; x += 2)
                for (int z : {0, 10})
                    cloud.points.push_back({{double(x), double(y), double(z)}, 0, cloud.points.size()});
        for (int y = 150; y <= 230; y += 2)
            for (int x = 0; x <= 100; x += 2)
                for (int z : {0, 12})
                    cloud.points.push_back({{double(x), double(y), double(z)}, 0, cloud.points.size()});
        model = associateModelCloud(*buildRecognizedPlates(recognition), cloud);
    }
};
std::vector<Vec3> vertices(const MeshData &mesh, std::string id)
{
    const auto it = std::find(mesh.partIds.begin(), mesh.partIds.end(), id);
    const auto index = it - mesh.partIds.begin();
    std::vector<Vec3> result;
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
        if (mesh.trianglePartIndices[i] == index)
            for (auto v : mesh.triangles[i])
                result.push_back(mesh.vertices[v]);
    std::sort(result.begin(), result.end(),
              [](Vec3 a, Vec3 b) { return std::tie(a.x, a.y, a.z) < std::tie(b.x, b.y, b.z); });
    return result;
}
void sameVertices(const MeshData &a, const MeshData &b, const std::string &id)
{
    const auto first = vertices(a, id), second = vertices(b, id);
    ASSERT_EQ(first.size(), second.size());
    for (std::size_t i = 0; i < first.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(first[i].x, second[i].x);
        EXPECT_DOUBLE_EQ(first[i].y, second[i].y);
        EXPECT_DOUBLE_EQ(first[i].z, second[i].z);
    }
}
TEST(LocalReconstruction, CropUsesRemainingSourcePointsAndPreservesOtherPart)
{
    Fixture f;
    CloudSelection kept;
    for (std::size_t i = 0; i < f.cloud.points.size(); ++i)
        if (f.cloud.points[i].position.x <= 60)
            kept.push_back(i);
    const auto cropped = editCloudPoints(f.cloud, kept, CloudEditMode::KeepSelection);
    const auto after = reconstructLocalParts(*f.model, *cropped, {{"first"}});
    EXPECT_EQ(after->summary().partIds, f.model->summary().partIds);
    sameVertices(after->mesh(), f.model->mesh(), "other");
    EXPECT_NEAR(after->summary().volumeMm3, 60 * 80 * 10 + 100 * 80 * 12, 1e-5);
    EXPECT_NEAR(inspectModelThickness(*after, {"first"}).at("first").thicknessMm, 10, 1e-6);
    EXPECT_TRUE(f.model->summary().locallyReconstructedParts.empty());
    // 重复局部重建仍使用原始关联，不随重新分析得到的 PCA 框架漂移。
    auto again = reconstructLocalParts(*after, *cropped, {{"first"}});
    EXPECT_NEAR(again->summary().volumeMm3, after->summary().volumeMm3, 1e-5);
}
TEST(LocalReconstruction, AllContourRulesProduceValidModelsAndPreserveUnselected)
{
    Fixture f;
    for (const auto &rule : {"rectangle", "circle", "chamfer_rectangle", "convex", "polygon", "smooth"})
    {
        SCOPED_TRACE(rule);
        LocalReconstruction request{{"first"}};
        request.contour = rule;
        request.toleranceMm = 2;
        const auto after = reconstructLocalParts(*f.model, f.cloud, request);
        EXPECT_EQ(after->summary().solidCount, 2u);
        EXPECT_GT(after->summary().volumeMm3, 96000);
        sameVertices(after->mesh(), f.model->mesh(), "other");
    }
    LocalReconstruction box{{"first"}};
    box.type = "box";
    const auto after = reconstructLocalParts(*f.model, f.cloud, box);
    EXPECT_NEAR(after->summary().volumeMm3, f.model->summary().volumeMm3, 1e-5);
}
TEST(LocalReconstruction, PreservesUnselectedManualThicknessAndPosition)
{
    Fixture f;
    auto edited = changeModelThickness(*f.model, {{"other"}, 18.5});
    ModelTransform move;
    move.offsetMm = {12, -3, 27};
    edited = transformModelParts(*edited, {"other"}, move);
    auto cloud = f.cloud;
    std::erase_if(cloud.points, [](const auto &p) { return p.position.x > 60; });
    const auto after = reconstructLocalParts(*edited, cloud, {{"first"}});
    EXPECT_EQ(after->summary().partIds, edited->summary().partIds);
    sameVertices(after->mesh(), edited->mesh(), "other");
    EXPECT_DOUBLE_EQ(after->summary().thicknessOverrides.at("other").thicknessMm, 18.5);
    const auto &transform = after->summary().partTransforms.at("other");
    EXPECT_DOUBLE_EQ(transform.originMm.x, 12);
    EXPECT_DOUBLE_EQ(transform.originMm.y, -3);
    EXPECT_DOUBLE_EQ(transform.originMm.z, 27);
    EXPECT_EQ(after->summary().locallyReconstructedParts, std::vector<std::string>{"first"});
    EXPECT_NEAR(after->summary().volumeMm3, 60 * 80 * 10 + 100 * 80 * 18.5, 1e-5);
}
TEST(LocalReconstruction, FailuresAreAtomicIncludingSecondPartAndCancellation)
{
    Fixture f;
    auto partial = f.cloud;
    std::erase_if(partial.points, [](const auto &p) { return p.position.y > 100; });
    EXPECT_THROW((void)reconstructLocalParts(*f.model, partial, {{"first", "other"}}), GeometryError);
    EXPECT_THROW((void)reconstructLocalParts(*f.model, f.cloud, {{"unknown"}}), GeometryError);
    EXPECT_THROW((void)reconstructLocalParts(*f.model, f.cloud, {{"first", "first"}}), GeometryError);
    LocalReconstruction invalid{{"first"}};
    invalid.type = "horizontal";
    invalid.up = {1, 0, 0};
    EXPECT_THROW((void)reconstructLocalParts(*f.model, f.cloud, invalid), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)reconstructLocalParts(*f.model, f.cloud, {{"first"}}, stop.get_token()), GeometryError);
    EXPECT_TRUE(f.model->summary().locallyReconstructedParts.empty());
    EXPECT_NEAR(f.model->summary().volumeMm3, 100 * 80 * (10 + 12), 1e-5);
}
TEST(LocalReconstruction, ConcaveContourKeepsNotchUnlikeConvexHull)
{
    Fixture f;
    auto cloud = f.cloud;
    std::erase_if(cloud.points,
                  [](const auto &p) { return p.position.y < 100 && p.position.x > 50 && p.position.y > 40; });
    LocalReconstruction request{{"first"}};
    request.contour = "polygon";
    auto concave = reconstructLocalParts(*f.model, cloud, request);
    request.contour = "convex";
    auto convex = reconstructLocalParts(*f.model, cloud, request);
    EXPECT_LT(concave->summary().volumeMm3, convex->summary().volumeMm3 - 1000);
}
TaskSnapshot done(CommandService &service, const std::string &id)
{
    for (int i = 0; i < 20000; ++i)
    {
        auto task = service.task(id);
        if (task.finished())
        {
            if (!task.result.ok())
                throw std::runtime_error(task.errorCode + ": " + task.message);
            return task;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("local command timed out");
}
TEST(LocalReconstruction, ServiceCropPreservesModelPreviewHistoryAndReplay)
{
    const auto dir = std::filesystem::temp_directory_path() /
                     ("rm-local-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(dir);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{dir};
    CloudData cloud;
    auto add = [&](double x, double y, double z) { cloud.points.push_back({{x, y, z}, 0, cloud.points.size()}); };
    for (int x = -500; x <= 500; x += 5)
    {
        for (int y = -100; y <= 100; y += 5)
            add(x, y, 0);
        for (int i = 0; i <= 10; ++i)
        {
            add(x, -100 + i * 0.8, 100);
            add(x, 100 - i * 1.2, 100);
        }
    }
    const auto file = dir / "input.pcd", script = dir / "record.py";
    (void)exportPointCloud(cloud, file);
    CommandService service;
    done(service, service.submitStartRecording(script));
    done(service, service.submitImport(file));
    done(service, service.submitRecognizedReconstruct(false, service.project().revision));
    auto built = service.project();
    ASSERT_TRUE(built.model);
    ASSERT_FALSE(built.model->reconstructableParts.empty());
    rm::Array removed;
    for (std::size_t i = 0; i < built.cloud->points.size(); ++i)
        if (built.cloud->points[i].position.x + built.cloud->originMm.x > 100)
            removed.emplace_back(std::uint64_t(i));
    done(service, service.submitCommand("cloud.delete", {}, {{"indices", removed}}));
    auto cropped = service.project();
    ASSERT_EQ(cropped.model, built.model);
    ASSERT_EQ(cropped.mesh, built.mesh);
    LocalReconstruction request{{"web"}};
    const auto previewId = service.submitLocalReconstructionPreview(request, cropped.revision);
    done(service, previewId);
    auto preview = service.takeEdgePreview(previewId);
    ASSERT_TRUE(preview);
    EXPECT_EQ(service.project().revision, cropped.revision);
    done(service, service.submitLocalReconstruction(request, cropped.revision));
    auto after = service.project();
    ASSERT_TRUE(after.model);
    for (const auto &id : built.model->partIds)
        if (id != "web")
            sameVertices(*after.mesh, *built.mesh, id);
    EXPECT_LT(after.model->volumeMm3, built.model->volumeMm3);
    EXPECT_NEAR(after.model->volumeMm3, preview->summary().volumeMm3, 1e-5);
    done(service, service.submitLine("project.undo"));
    EXPECT_EQ(service.project().model, built.model);
    done(service, service.submitLine("project.redo"));
    EXPECT_EQ(service.project().model, after.model);
    done(service, service.submitStopRecording());
    std::ifstream stream(script);
    const std::string text{std::istreambuf_iterator<char>(stream), {}};
    EXPECT_NE(text.find("model.reconstruct_local"), std::string::npos);
    EXPECT_EQ(text.find("LocalReconstructionPreview"), std::string::npos);
    const auto lateRecording = dir / "late-record.py";
    done(service, service.submitStartRecording(lateRecording));
    done(service, service.submitStopRecording());
    std::ifstream lateStream(lateRecording);
    const std::string lateText{std::istreambuf_iterator<char>(lateStream), {}};
    EXPECT_NE(lateText.find("model.reconstruct_local"), std::string::npos);
    CommandService replay;
    done(replay, replay.submitScriptFile(lateRecording));
    ASSERT_TRUE(replay.project().model);
    EXPECT_EQ(replay.project().model->partIds, after.model->partIds);
    EXPECT_NEAR(replay.project().model->volumeMm3, after.model->volumeMm3, 1e-5);
}
} // namespace
