/* 文件描述：验证部件删除的真实几何、稳定 ID、显示拓扑、原子失败和选择集合。 */
#include <algorithm>
#include <set>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "workbench/ModelDisplayData.h"

namespace
{
using namespace reverse;

TEST(ModelEditing, RemovesGeometryAndRemapsAllPickingDataWithoutChangingSurvivors)
{
    const auto model = buildBeam({}, {100, 40, 60, 4, 5, 3, {25, 75}, false, true});
    const auto edited = deleteModelParts(*model, {"flange-bottom", "rib-1-right", "flange-bottom"});
    ASSERT_TRUE(edited);
    EXPECT_EQ(model->summary().solidCount, 7u);
    EXPECT_EQ(edited->summary().solidCount, 5u);
    EXPECT_DOUBLE_EQ(edited->summary().volumeMm3, model->summary().volumeMm3 - 20000 - 2700);
    const auto &mesh = edited->mesh();
    EXPECT_EQ(mesh.partIds, edited->summary().partIds);
    EXPECT_EQ(mesh.partIds.front(), "flange-top");
    EXPECT_EQ(std::count(mesh.partIds.begin(), mesh.partIds.end(), "rib-2-right"), 1);
    EXPECT_EQ(mesh.triangles.size(), mesh.trianglePartIndices.size());
    EXPECT_EQ(mesh.triangles.size(), mesh.triangleFaceIndices.size());
    for (const auto &triangle : mesh.triangles)
        for (const auto vertex : triangle) EXPECT_LT(vertex, mesh.vertices.size());
    for (const auto part : mesh.trianglePartIndices) EXPECT_LT(part, mesh.partIds.size());
    for (const auto &point : mesh.keyPoints) EXPECT_LT(point.partIndex, mesh.partIds.size());
    for (const auto &edge : mesh.edges) EXPECT_LT(edge.partIndex, mesh.partIds.size());
    for (const auto &face : mesh.planarFaces) EXPECT_LT(face[0], mesh.partIds.size());
    const auto originalParts = workbench::makeModelDisplayParts(model->mesh(), model->summary(), {});
    const auto remainingParts = workbench::makeModelDisplayParts(mesh, edited->summary(), {});
    for (const auto &part : remainingParts)
    {
        const auto before = std::find_if(originalParts.begin(), originalParts.end(),
            [&](const auto &p) { return p.id == part.id; });
        ASSERT_NE(before, originalParts.end());
        EXPECT_EQ(before->colorSrgb, part.colorSrgb);
        EXPECT_EQ(before->thicknessMm, part.thicknessMm);
        EXPECT_EQ(before->triangles.size(), part.triangles.size());
        EXPECT_EQ(before->boundsMinMm.x, part.boundsMinMm.x);
        EXPECT_EQ(before->boundsMaxMm.z, part.boundsMaxMm.z);
    }
    // 偏差使用保留下来的真实板件，不能仍然测到已删除的底翼板。
    CloudData cloud;
    cloud.points.push_back({{0, 18, -30}, 0, 0});
    EXPECT_NEAR(evaluateDeviation(cloud, *model).meanMm, 0, 1e-8);
    EXPECT_GT(evaluateDeviation(cloud, *edited).meanMm, 10);
}

TEST(ModelEditing, FiltersRecognizedPlateMetadataAndCanRemoveLastObservedSurface)
{
    PlateRecognition recognition;
    recognition.plates = {
        {"web", {-50, 0, -20}, {50, 0, 20}, 1, std::nullopt, 0, 100, "observed_surface"},
        {"rib-1-right", {-2, 0, -20}, {2, 20, 20}, 0, 4.0, 0.1, 50, "top_width"}};
    const auto model = buildRecognizedPlates(recognition);
    ASSERT_EQ(model->summary().solidCount, 1u);
    ASSERT_EQ(model->summary().surfaceCount, 1u);
    const auto edited = deleteModelParts(*model, {"rib-1-right"});
    ASSERT_TRUE(edited);
    EXPECT_EQ(edited->summary().solidCount, 0u);
    EXPECT_EQ(edited->summary().surfaceCount, 1u);
    EXPECT_DOUBLE_EQ(edited->summary().volumeMm3, 0);
    ASSERT_EQ(edited->summary().plates.size(), 1u);
    EXPECT_EQ(edited->summary().plates[0].id, "web");
    EXPECT_FALSE(edited->summary().plates[0].thicknessMm);
    EXPECT_FALSE(deleteModelParts(*edited, {"web"}));
}

TEST(ModelEditing, RejectsUnknownEmptyAndCancelledRequestsWithoutTouchingSource)
{
    const auto model = buildBeam({}, {100, 40, 60, 4, 5, 3, {}, false, false});
    EXPECT_THROW((void)deleteModelParts(*model, {}), GeometryError);
    EXPECT_THROW((void)deleteModelParts(*model, {"web", "missing"}), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)deleteModelParts(*model, {"web"}, stop.get_token()), GeometryError);
    EXPECT_EQ(model->summary().solidCount, 3u);
    EXPECT_EQ(model->mesh().partIds.size(), 3u);
    EXPECT_FALSE(deleteModelParts(*model, model->summary().partIds));
}

TEST(ModelPartSelection, SupportsReplaceAddRemoveToggleAndClear)
{
    using workbench::updateModelPartSelection;
    auto ids = updateModelPartSelection({"web"}, "rib-1", CloudSelectionMode::Replace);
    EXPECT_EQ(ids, (std::vector<std::string>{"rib-1"}));
    ids = updateModelPartSelection(ids, "web", CloudSelectionMode::Add);
    ids = updateModelPartSelection(ids, "web", CloudSelectionMode::Add);
    EXPECT_EQ(ids, (std::vector<std::string>{"rib-1", "web"}));
    ids = updateModelPartSelection(ids, "web", CloudSelectionMode::Replace, true);
    EXPECT_EQ(ids, (std::vector<std::string>{"rib-1"}));
    ids = updateModelPartSelection(ids, "web", CloudSelectionMode::Replace, true);
    EXPECT_EQ(ids.size(), 2u);
    EXPECT_EQ(updateModelPartSelection(ids, "", CloudSelectionMode::Replace, true), ids);
    EXPECT_EQ(updateModelPartSelection(ids, "", CloudSelectionMode::Add), ids);
    EXPECT_TRUE(updateModelPartSelection(ids, "", CloudSelectionMode::Replace).empty());
    ids = updateModelPartSelection(ids, "rib-1", CloudSelectionMode::Remove);
    EXPECT_EQ(ids, (std::vector<std::string>{"web"}));
    EXPECT_TRUE(updateModelPartSelection(ids, "", CloudSelectionMode::Clear).empty());
}

TEST(ModelObjectSelection, EveryTopologyModeCanAccumulateToggleReplaceAndClear)
{
    using namespace workbench;
    for (const auto kind : {ModelPickMode::Vertex, ModelPickMode::Edge, ModelPickMode::Face, ModelPickMode::Body})
    {
        const ModelSelection first{"web", kind, 0};
        const ModelSelection second{kind == ModelPickMode::Body ? "rib-1" : "web", kind,
                                    kind == ModelPickMode::Body ? 0u : 1u};
        auto selected = updateModelSelection({}, first, CloudSelectionMode::Replace);
        selected = updateModelSelection(selected, second, CloudSelectionMode::Replace, true);
        EXPECT_EQ(selected, (std::vector<ModelSelection>{first, second}));
        selected = updateModelSelection(selected, first, CloudSelectionMode::Replace, true);
        EXPECT_EQ(selected, (std::vector<ModelSelection>{second}));
        EXPECT_EQ(updateModelSelection(selected, {}, CloudSelectionMode::Replace, true), selected);
        selected = updateModelSelection(selected, first, CloudSelectionMode::Add);
        selected = updateModelSelection(selected, first, CloudSelectionMode::Add);
        EXPECT_EQ(selected, (std::vector<ModelSelection>{second, first}));
        selected = updateModelSelection(selected, second, CloudSelectionMode::Remove);
        EXPECT_EQ(selected, (std::vector<ModelSelection>{first}));
        EXPECT_TRUE(updateModelSelection(selected, {}, CloudSelectionMode::Replace).empty());
        EXPECT_TRUE(updateModelSelection(selected, {}, CloudSelectionMode::Clear).empty());
        EXPECT_EQ(updateModelSelection({first, second}, first, CloudSelectionMode::Replace),
                  (std::vector<ModelSelection>{first}));
    }
}

TEST(ModelObjectSelection, SameIndexOnDifferentPartsOrTopologyIsNotTheSameObject)
{
    using namespace workbench;
    const std::vector<ModelSelection> objects{{"web", ModelPickMode::Vertex, 0},
        {"web", ModelPickMode::Edge, 0}, {"rib-1", ModelPickMode::Edge, 0}};
    std::vector<ModelSelection> selected;
    for (const auto &object : objects)
        selected = updateModelSelection(selected, object, CloudSelectionMode::Replace, true);
    EXPECT_EQ(selected, objects);
    EXPECT_EQ(updateModelSelection(selected, objects[1], CloudSelectionMode::Replace, true),
              (std::vector<ModelSelection>{objects[0], objects[2]}));
}

TEST(ModelObjectSelection, RejectsMissingAndStaleTopologyIndices)
{
    using namespace workbench;
    ModelDisplayPart part;
    part.id = "web";
    part.keyPoints = {{0, 0, 0}, {1, 0, 0}};
    part.edges = {part.keyPoints, {}};
    part.triangleFaceIndices = {3, 3, 7};
    EXPECT_TRUE(isValidModelSelection(part, {"web", ModelPickMode::Body, 0}));
    EXPECT_TRUE(isValidModelSelection(part, {"web", ModelPickMode::Vertex, 1}));
    EXPECT_TRUE(isValidModelSelection(part, {"web", ModelPickMode::Edge, 0}));
    EXPECT_TRUE(isValidModelSelection(part, {"web", ModelPickMode::Face, 7}));
    EXPECT_FALSE(isValidModelSelection(part, {"missing", ModelPickMode::Body, 0}));
    EXPECT_FALSE(isValidModelSelection(part, {"web", ModelPickMode::Vertex, 2}));
    EXPECT_FALSE(isValidModelSelection(part, {"web", ModelPickMode::Edge, 1}));
    EXPECT_FALSE(isValidModelSelection(part, {"web", ModelPickMode::Face, 0}));
}

TEST(ModelObjectSelection, EdgeTreatmentGroupsCrossPartSelectionsAndUsesExactTopologyIndices)
{
    using namespace workbench;
    MeshData mesh;
    mesh.partIds = {"left", "right"};
    const std::vector<Vec3> line{{0, 0, 0}, {1, 0, 0}};
    mesh.edges = {{line, 0, 5}, {line, 1, 12}, {line, 0, 9}};
    const auto groups = edgeTreatmentTargets(mesh, {{"left", ModelPickMode::Edge, 1},
        {"right", ModelPickMode::Edge, 0}, {"left", ModelPickMode::Edge, 0}, {"left", ModelPickMode::Edge, 1}});
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0].partId, "left");
    EXPECT_EQ(groups[0].edges, (std::vector<std::uint32_t>{9, 5}));
    EXPECT_EQ(groups[1].partId, "right");
    EXPECT_EQ(groups[1].edges, (std::vector<std::uint32_t>{12}));
    EXPECT_TRUE(edgeTreatmentTargets(mesh, {}).empty());
    EXPECT_THROW((void)edgeTreatmentTargets(mesh, {{"left", ModelPickMode::Edge, 0},
        {"right", ModelPickMode::Edge, 99}}), std::invalid_argument);
    EXPECT_THROW((void)edgeTreatmentTargets(mesh, {{"left", ModelPickMode::Face, 0}}), std::invalid_argument);
}

}
