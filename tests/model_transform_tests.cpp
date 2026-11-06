/* 文件描述：验证实际实体刚体变换、轴方向、标签/拾取、偏差和 STEP 回读，不依赖桌面事件。 */
#include <chrono>
#include <cmath>
#include <limits>
#include <set>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "workbench/ModelDisplayData.h"

namespace
{
using namespace reverse;
void near(Vec3 a, Vec3 b, double tolerance = 1e-7)
{
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
}
auto beam()
{
    return buildBeam({{1000, 2000, -500}, {0, 1, 0}, {-1, 0, 0}, {0, 0, 1}},
                     {100, 40, 60, 4, 5, 3, {25, 75}, false, true});
}
TEST(ModelTransform, SignedRotationsRespectAxisAndPivot)
{
    ModelTransform r;
    r.kind = ModelTransformKind::Rotate;
    r.centerMm = {10, 20, 30};
    r.angleDegrees = 90;
    r.axis = RotationAxis::X;
    near(transformPoint(modelTransformFrame(r), {10, 21, 30}), {10, 20, 31});
    r.axis = RotationAxis::Y;
    near(transformPoint(modelTransformFrame(r), {11, 20, 30}), {10, 20, 29});
    r.axis = RotationAxis::Z;
    near(transformPoint(modelTransformFrame(r), {11, 20, 30}), {10, 21, 30});
    r.angleDegrees = -90;
    near(transformPoint(modelTransformFrame(r), {11, 20, 30}), {10, 19, 30});
    near(transformPoint(modelTransformFrame(r), r.centerMm), r.centerMm);
}
TEST(ModelTransform, MovesOnlyRequestedPartsAndAllTheirPickingGeometry)
{
    const auto original = beam();
    ModelTransform move;
    move.offsetMm = {-100, 12.5, 6};
    const auto edited = transformModelParts(*original, {"web", "rib-1-right", "web"}, move);
    EXPECT_EQ(edited->summary().partIds, original->summary().partIds);
    EXPECT_EQ(edited->summary().solidCount, original->summary().solidCount);
    EXPECT_DOUBLE_EQ(edited->summary().volumeMm3, original->summary().volumeMm3);
    const auto &a = original->mesh();
    const auto &b = edited->mesh();
    ASSERT_EQ(a.vertices.size(), b.vertices.size());
    const auto selected = [&](std::uint32_t i) { return a.partIds[i] == "web" || a.partIds[i] == "rib-1-right"; };
    const auto expected = [&](Vec3 p, std::uint32_t i) {
        return selected(i) ? transformPoint(modelTransformFrame(move), p) : p;
    };
    for (std::size_t i = 0; i < a.triangles.size(); ++i)
        for (const auto v : a.triangles[i])
            near(b.vertices[v], expected(a.vertices[v], a.trianglePartIndices[i]));
    for (std::size_t i = 0; i < a.keyPoints.size(); ++i)
        near(b.keyPoints[i].positionMm, expected(a.keyPoints[i].positionMm, a.keyPoints[i].partIndex));
    for (std::size_t i = 0; i < a.edges.size(); ++i)
        for (std::size_t j = 0; j < a.edges[i].pointsMm.size(); ++j)
            near(b.edges[i].pointsMm[j], expected(a.edges[i].pointsMm[j], a.edges[i].partIndex));
    EXPECT_EQ(a.triangleFaceIndices, b.triangleFaceIndices);
    EXPECT_EQ(a.planarFaces, b.planarFaces);
    EXPECT_TRUE(original->summary().partTransforms.empty());
    EXPECT_EQ(edited->summary().partTransforms.size(), 2);
}
TEST(ModelTransform, ComposesTransformsAndMovesLabelNormalsWithPlate)
{
    const auto original = beam();
    ModelTransform move;
    move.offsetMm = {30, 40, 50};
    auto edited = transformModelParts(*original, {"web"}, move);
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::Y;
    rotation.angleDegrees = -60;
    rotation.centerMm = {1000, 2000, -500};
    edited = transformModelParts(*edited, {"web"}, rotation);
    const auto a = workbench::makeModelDisplayParts(original->mesh(), original->summary(), {});
    const auto b = workbench::makeModelDisplayParts(edited->mesh(), edited->summary(), {});
    const auto accumulated = composeTransforms(modelTransformFrame(rotation), modelTransformFrame(move));
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        EXPECT_EQ(a[i].colorSrgb, b[i].colorSrgb);
        EXPECT_EQ(a[i].thicknessMm, b[i].thicknessMm);
        if (a[i].id != "web")
        {
            near(a[i].vertices.front(), b[i].vertices.front());
            continue;
        }
        for (std::size_t j = 0; j < a[i].vertices.size(); ++j)
            near(b[i].vertices[j], transformPoint(accumulated, a[i].vertices[j]));
        for (int face = 0; face < 2; ++face)
        {
            near(b[i].labelFaces[face].position, transformPoint(accumulated, a[i].labelFaces[face].position));
            near(b[i].labelFaces[face].normal, transformVector(accumulated, a[i].labelFaces[face].normal));
        }
    }
    edited = deleteModelParts(*edited, {"web"});
    EXPECT_TRUE(edited->summary().partTransforms.empty());
}
TEST(ModelTransform, DeviationUsesTransformedPlateRatherThanOriginalBox)
{
    auto original = beam();
    auto removed = original->summary().partIds;
    std::erase(removed, "web");
    original = deleteModelParts(*original, removed);
    const auto point = original->mesh().vertices.front();
    ModelTransform r;
    r.kind = ModelTransformKind::Rotate;
    r.axis = RotationAxis::Z;
    r.angleDegrees = 60;
    r.centerMm = {0, 0, 0};
    const auto edited = transformModelParts(*original, {"web"}, r);
    CloudData cloud;
    cloud.points.push_back({transformPoint(modelTransformFrame(r), point), 0, 0});
    EXPECT_NEAR(evaluateDeviation(cloud, *edited).maximumMm, 0, 1e-7);
    EXPECT_GT(evaluateDeviation(cloud, *original).meanMm, 100);
}
TEST(ModelTransform, RejectsInvalidUnknownAndCancelledRequestsAtomically)
{
    const auto original = beam();
    ModelTransform r;
    EXPECT_THROW((void)transformModelParts(*original, {}, r), GeometryError);
    EXPECT_THROW((void)transformModelParts(*original, {"web", "missing"}, r), GeometryError);
    r.offsetMm.x = std::numeric_limits<double>::infinity();
    EXPECT_THROW((void)transformModelParts(*original, {"web"}, r), GeometryError);
    r.kind = ModelTransformKind::Rotate;
    r.angleDegrees = 180.01;
    EXPECT_THROW((void)transformModelParts(*original, {"web"}, r), GeometryError);
    r.angleDegrees = 90;
    r.axis = static_cast<RotationAxis>(8);
    EXPECT_THROW((void)transformModelParts(*original, {"web"}, r), GeometryError);
    r.axis = RotationAxis::Z;
    r.centerMm.y = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)transformModelParts(*original, {"web"}, r), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)transformModelParts(*original, {"web"}, {}, stop.get_token()), GeometryError);
    EXPECT_TRUE(original->summary().partTransforms.empty());
}
TEST(ModelTransform, ExportAndReimportPreserveTransformedGeometry)
{
    const auto directory =
        std::filesystem::temp_directory_path() /
        ("rm-transform-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(directory);
    struct Cleanup
    {
        std::filesystem::path path;
        ~Cleanup()
        {
            std::error_code e;
            std::filesystem::remove_all(path, e);
        }
    } cleanup{directory};
    const auto original = beam();
    const auto before = directory / "before.step", after = directory / "after.step";
    (void)exportStep(*original, before);
    const auto imported = loadCadModel(before);
    ModelTransform r;
    r.kind = ModelTransformKind::Rotate;
    r.axis = RotationAxis::X;
    r.angleDegrees = -60;
    r.centerMm = {123, 456, 789};
    const auto edited = transformModelParts(*imported, imported->summary().partIds, r);
    const auto exported = exportStep(*edited, after);
    EXPECT_TRUE(exported.valid);
    EXPECT_EQ(exported.solidCount, original->summary().solidCount);
    const auto reread = loadCadModel(after);
    const auto bounds = [](const MeshData &mesh) {
        std::array<Vec3, 2> result{mesh.vertices.front(), mesh.vertices.front()};
        for (const auto &p : mesh.vertices)
        {
            result[0] = {std::min(result[0].x, p.x), std::min(result[0].y, p.y), std::min(result[0].z, p.z)};
            result[1] = {std::max(result[1].x, p.x), std::max(result[1].y, p.y), std::max(result[1].z, p.z)};
        }
        return result;
    };
    const auto a = bounds(edited->mesh()), b = bounds(reread->mesh());
    near(a[0], b[0], 1e-5);
    near(a[1], b[1], 1e-5);
    EXPECT_NEAR(reread->summary().volumeMm3, original->summary().volumeMm3, 1e-4);
    // 原模型再次导出，确保变换没有原地改写历史中共用的 OCCT 拓扑。
    (void)exportStep(*imported, directory / "original-again.step");
    const auto unchanged = loadCadModel(directory / "original-again.step");
    near(bounds(imported->mesh())[0], bounds(unchanged->mesh())[0], 1e-5);
}
} // namespace
