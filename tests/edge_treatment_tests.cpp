/* 文件描述：验证真实 BRep 圆角/倒角、连续边处理、刚体变换、失败原子性及 STEP 导出。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"

namespace
{
using namespace reverse;
auto sourceModel()
{
    return buildBeam({}, {100, 40, 60, 4, 5, 3, {25}, false, false});
}
TEST(EdgeTreatment, FilletAndChamferChangeSolidWithoutChangingSource)
{
    const auto source = sourceModel();
    const double volume = source->summary().volumeMm3;
    for (auto kind : {EdgeTreatmentKind::Fillet, EdgeTreatmentKind::Chamfer})
    {
        auto result = treatModelEdges(*source, {{{"web", {1}}}, kind, 1});
        EXPECT_LT(result->summary().volumeMm3, volume);
        EXPECT_EQ(result->summary().solidCount, source->summary().solidCount);
        EXPECT_EQ(result->summary().partIds, source->summary().partIds);
        EXPECT_EQ(result->summary().edgeTreatedParts, std::vector<std::string>{"web"});
        EXPECT_FALSE(result->mesh().triangles.empty());
    }
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, volume);
    EXPECT_TRUE(source->summary().edgeTreatedParts.empty());
}
TEST(EdgeTreatment, MoveAndRotatePreserveTreatedGeometry)
{
    const auto source = sourceModel();
    const auto treated = treatModelEdges(*source, {{{"web", {1}}}, EdgeTreatmentKind::Fillet, 1});
    ModelTransform move;
    move.offsetMm = {200, -33, 44};
    auto edited = transformModelParts(*treated, {"web"}, move);
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::Y;
    rotation.angleDegrees = 63;
    rotation.centerMm = {20, 15, 30};
    edited = transformModelParts(*edited, {"web"}, rotation);
    EXPECT_NEAR(edited->summary().volumeMm3, treated->summary().volumeMm3, 1e-5);
    EXPECT_EQ(edited->summary().edgeTreatedParts, treated->summary().edgeTreatedParts);
    EXPECT_EQ(edited->summary().partTransforms.size(), 1);
}
TEST(EdgeTreatment, InvalidInputAndCancellationKeepOriginal)
{
    const auto source = sourceModel();
    for (double size : {0.0, -1.0, 100000.0, std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {1}}}, EdgeTreatmentKind::Fillet, size}), GeometryError);
    EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {0}}}, EdgeTreatmentKind::Fillet, 1}), GeometryError);
    EXPECT_THROW((void)treatModelEdges(*source, {{{"missing", {1}}}, EdgeTreatmentKind::Fillet, 1}), GeometryError);
    EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {}}}, EdgeTreatmentKind::Fillet, 1}), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {1}}}, EdgeTreatmentKind::Fillet, 1}, stop.get_token()),
                 GeometryError);
    EXPECT_TRUE(source->summary().edgeTreatedParts.empty());
}
TEST(EdgeTreatment, DeletePrunesTreatedPartsAndStepPreservesEditedGeometry)
{
    const auto source = sourceModel();
    const auto edited = treatModelEdges(*source, {{{"web", {1}}}, EdgeTreatmentKind::Chamfer, 1});
    const auto removed = deleteModelParts(*edited, {"web"});
    EXPECT_TRUE(removed->summary().edgeTreatedParts.empty());
    const auto path =
        std::filesystem::temp_directory_path() /
        ("rm-edge-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    const auto exported = exportStep(*edited, path);
    const auto imported = loadCadModel(path);
    EXPECT_NEAR(imported->summary().volumeMm3, edited->summary().volumeMm3, 1e-4);
    EXPECT_EQ(exported.solidCount, edited->summary().solidCount);
    std::filesystem::remove(path);
}
TEST(EdgeTreatment, MultipleEdgesAndFurtherOperationsUseCurrentGeometry)
{
    const auto source = sourceModel();
    std::vector<std::uint32_t> corners;
    for (const auto &edge : source->mesh().edges)
    {
        const auto a = edge.pointsMm.front(), b = edge.pointsMm.back();
        if (source->mesh().partIds.at(edge.partIndex) == "web" &&
            std::abs(std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) - 4) < 1e-6)
        {
            corners.push_back(edge.topologyIndex);
        }
    }
    ASSERT_EQ(corners.size(), 4);
    const auto firstEdge = corners[0], nextEdge = corners[2];
    const auto multi = treatModelEdges(*source, {{{"web", {firstEdge, nextEdge, firstEdge}}}, EdgeTreatmentKind::Fillet, 0.5});
    const auto distinct = treatModelEdges(*source, {{{"web", {firstEdge, nextEdge}}}, EdgeTreatmentKind::Fillet, 0.5});
    EXPECT_NEAR(multi->summary().volumeMm3, distinct->summary().volumeMm3, 1e-6);
    const auto first = treatModelEdges(*source, {{{"web", {firstEdge}}}, EdgeTreatmentKind::Fillet, 0.5});
    EXPECT_LT(multi->summary().volumeMm3, first->summary().volumeMm3);
    // 在当前实体的另一条边上继续倒角，原圆角几何应保留。
    const auto originalIt =
        std::find_if(source->mesh().edges.begin(), source->mesh().edges.end(), [&](const auto &edge) {
            return source->mesh().partIds.at(edge.partIndex) == "web" && edge.topologyIndex == nextEdge;
        });
    ASSERT_NE(originalIt, source->mesh().edges.end());
    const auto &original = *originalIt;
    const auto same = [](Vec3 a, Vec3 b) { return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) < 1e-6; };
    const auto found = std::find_if(first->mesh().edges.begin(), first->mesh().edges.end(), [&](const auto &edge) {
        return edge.partIndex == original.partIndex && same(edge.pointsMm.front(), original.pointsMm.front()) &&
               same(edge.pointsMm.back(), original.pointsMm.back());
    });
    ASSERT_NE(found, first->mesh().edges.end());
    const auto chain = treatModelEdges(*first, {{{"web", {found->topologyIndex}}}, EdgeTreatmentKind::Chamfer, 0.25});
    const auto chamferOnly = treatModelEdges(*source, {{{"web", {nextEdge}}}, EdgeTreatmentKind::Chamfer, 0.25});
    EXPECT_LT(chain->summary().volumeMm3, first->summary().volumeMm3);
    EXPECT_NEAR(chain->summary().volumeMm3,
                first->summary().volumeMm3 + chamferOnly->summary().volumeMm3 - source->summary().volumeMm3, 1e-6);
    EXPECT_EQ(chain->summary().edgeTreatedParts, std::vector<std::string>{"web"});
}


TEST(EdgeTreatment, MultiplePartsAreProcessedTogetherWithoutChangingSource)
{
    const auto source = sourceModel();
    const double original = source->summary().volumeMm3;
    for (const auto kind : {EdgeTreatmentKind::Fillet, EdgeTreatmentKind::Chamfer})
    {
        const auto left = treatModelEdges(*source, {{{"web", {1}}}, kind, 0.5});
        const auto right = treatModelEdges(*source, {{{"flange-bottom", {1}}}, kind, 0.5});
        const auto both = treatModelEdges(*source, {{{"web", {1}}, {"flange-bottom", {1}}}, kind, 0.5});
        EXPECT_NEAR(both->summary().volumeMm3, left->summary().volumeMm3 + right->summary().volumeMm3 - original, 1e-6);
        EXPECT_EQ(both->summary().edgeTreatedParts, (std::vector<std::string>{"web", "flange-bottom"}));
        EXPECT_EQ(both->summary().solidCount, source->summary().solidCount);
        EXPECT_EQ(both->mesh().partIds, source->mesh().partIds);
    }
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, original);
    EXPECT_TRUE(source->summary().edgeTreatedParts.empty());
}

TEST(EdgeTreatment, FailedOrCancelledBatchIdentifiesPartAndPreservesSource)
{
    const auto source = sourceModel();
    const auto triangles = source->mesh().triangles;
    const double original = source->summary().volumeMm3;
    try
    {
        (void)treatModelEdges(*source, {{{"web", {1}}, {"flange-bottom", {99999}}}, EdgeTreatmentKind::Fillet, 0.5});
        FAIL() << "invalid second part must reject entire batch";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "unknown_edge");
        EXPECT_NE(std::string(error.what()).find("flange-bottom"), std::string::npos);
    }
    EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {1}}, {"web", {3}}}, EdgeTreatmentKind::Fillet, 0.5}), GeometryError);
    EXPECT_THROW((void)treatModelEdges(*source, {}), GeometryError);
    std::stop_source stop;
    EXPECT_THROW((void)treatModelEdges(*source, {{{"web", {1}}, {"flange-bottom", {1}}}, EdgeTreatmentKind::Fillet, 0.5},
        stop.get_token(), [&](double fraction, std::string_view) {
            if (fraction >= 0.4) stop.request_stop();
        }), GeometryError);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, original);
    EXPECT_EQ(source->mesh().triangles, triangles);
    EXPECT_TRUE(source->summary().edgeTreatedParts.empty());
}

std::uint32_t edgeWithLength(const Model &model, const std::string &partId, double length)
{
    for (const auto &edge : model.mesh().edges)
    {
        const auto a = edge.pointsMm.front(), b = edge.pointsMm.back();
        if (model.mesh().partIds.at(edge.partIndex) == partId &&
            std::abs(std::hypot(a.x - b.x, a.y - b.y, a.z - b.z) - length) < 1e-5)
        {
            return edge.topologyIndex;
        }
    }
    throw std::runtime_error("fixture edge not found");
}

void expectPlateThickness(const Model &model, const std::string &partId, Vec3 normal, double thickness)
{
    const auto &mesh = model.mesh();
    double minimum = std::numeric_limits<double>::infinity(), maximum = -minimum;
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
    {
        if (mesh.partIds.at(mesh.trianglePartIndices[i]) != partId)
        {
            continue;
        }
        for (const auto index : mesh.triangles[i])
        {
            const auto p = mesh.vertices[index];
            const double coordinate = p.x * normal.x + p.y * normal.y + p.z * normal.z;
            minimum = std::min(minimum, coordinate);
            maximum = std::max(maximum, coordinate);
        }
    }
    EXPECT_NEAR(maximum - minimum, thickness, 1e-5);
}

TEST(EdgeTreatment, Radius20On12mmPlatesRoundsOutlineCornersInOneBatch)
{
    const BeamFrame frame{{-2000, 1500, -400}, {0.6, 0.8, 0}, {-0.48, 0.36, 0.8}, {0.64, -0.48, 0.6}};
    const auto source = buildBeam(frame, {1000, 186.494, 627.8323, 6, 12, 8, {}, false, true});
    const auto first = edgeWithLength(*source, "flange-bottom", 186.494);
    const auto second = edgeWithLength(*source, "flange-top", 186.494);
    for (const auto kind : {EdgeTreatmentKind::Fillet, EdgeTreatmentKind::Chamfer})
    {
        const auto result = treatModelEdges(*source,
            {{{"flange-bottom", {first}}, {"flange-top", {second}}}, kind, 20});
        const double cornerArea = kind == EdgeTreatmentKind::Fillet
            ? 20 * 20 * (1 - std::numbers::pi / 4) : 20 * 20 / 2.0;
        EXPECT_NEAR(source->summary().volumeMm3 - result->summary().volumeMm3, 4 * cornerArea * 12, 1e-4);
        expectPlateThickness(*result, "flange-bottom", frame.zAxis, 12);
        expectPlateThickness(*result, "flange-top", frame.zAxis, 12);
        EXPECT_EQ(result->summary().solidCount, source->summary().solidCount);
        const auto onlyFirst = treatModelEdges(*source, {{{"flange-bottom", {first}}}, kind, 20});
        const auto onlySecond = treatModelEdges(*source, {{{"flange-top", {second}}}, kind, 20});
        EXPECT_NEAR(result->summary().volumeMm3,
            onlyFirst->summary().volumeMm3 + onlySecond->summary().volumeMm3 - source->summary().volumeMm3, 1e-4);
    }
    EXPECT_TRUE(source->summary().edgeTreatedParts.empty());
}

TEST(EdgeTreatment, ThroughThicknessSelectionRoundsOnlyOneCorner)
{
    const auto source = buildBeam({}, {1000, 186.494, 627.8323, 6, 12, 8, {}, false, true});
    const auto edge = edgeWithLength(*source, "flange-bottom", 12);
    const auto result = treatModelEdges(*source, {{{"flange-bottom", {edge}}}, EdgeTreatmentKind::Fillet, 20});
    EXPECT_NEAR(source->summary().volumeMm3 - result->summary().volumeMm3,
                20 * 20 * (1 - std::numbers::pi / 4) * 12, 1e-4);
    expectPlateThickness(*result, "flange-bottom", {0, 0, 1}, 12);
}

TEST(EdgeTreatment, BothSidesOfOutlineResolveToTheSameCorners)
{
    const auto source = buildBeam({}, {1000, 186.494, 627.8323, 6, 12, 8, {}, false, true});
    std::vector<std::uint32_t> boundary;
    for (const auto &edge : source->mesh().edges)
    {
        const auto a = edge.pointsMm.front(), b = edge.pointsMm.back();
        if (source->mesh().partIds.at(edge.partIndex) == "flange-bottom" &&
            std::abs(a.x + 500) < 1e-5 && std::abs(b.x + 500) < 1e-5 && std::abs(a.z - b.z) < 1e-5)
        {
            boundary.push_back(edge.topologyIndex);
        }
    }
    ASSERT_EQ(boundary.size(), 2);
    const auto oneSide = treatModelEdges(*source, {{{"flange-bottom", {boundary.front()}}}, EdgeTreatmentKind::Fillet, 20});
    const auto twoSides = treatModelEdges(*source, {{{"flange-bottom", boundary}}, EdgeTreatmentKind::Fillet, 20});
    EXPECT_NEAR(oneSide->summary().volumeMm3, twoSides->summary().volumeMm3, 1e-4);
    expectPlateThickness(*twoSides, "flange-bottom", {0, 0, 1}, 12);
}

} // namespace
