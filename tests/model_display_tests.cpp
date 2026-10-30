/*
 * 文件描述：
 * 验证显示网格按真实部件分组、板厚来自已生成模型，以及旋转框架和显示原点下的标注位置。
 * 覆盖部件排序变化和错误归属输入，避免着色或数值标注错配到其他板件。
 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "workbench/ModelDisplayData.h"
#include "workbench/ModelLabelVisibility.h"
#include "workbench/PointCloudDisplayData.h"

namespace
{
using namespace reverse;
using namespace reverse::workbench;

TEST(ModelDisplay, PreservesPartMembershipAndModelMeasurements)
{
    const double axis = std::sqrt(0.5);
    const BeamFrame frame{{1000, -400, 200}, {axis, axis, 0}, {-axis, axis, 0}, {0, 0, 1}};
    const BeamParameters parameters{100, 40, 60, 4, 5, 3, {25, 75}, true, true};
    const Vec3 displayOrigin{970, -420, 180};
    const auto model = buildBeam(frame, parameters);
    const auto &mesh = model->mesh();
    const auto parts = makeModelDisplayParts(mesh, model->summary(), displayOrigin);
    ASSERT_EQ(parts.size(), 7u);
    std::vector<std::size_t> offsets(parts.size());
    std::set<std::array<double, 3>> colors;
    for (std::size_t triangle = 0; triangle < mesh.triangles.size(); ++triangle)
    {
        const auto owner = mesh.trianglePartIndices[triangle];
        const auto &part = parts.at(owner);
        ASSERT_EQ(part.id, mesh.partIds.at(owner));
        const auto &localTriangle = part.triangles.at(offsets[owner]++);
        for (std::size_t corner = 0; corner < 3; ++corner)
        {
            const auto &source = mesh.vertices.at(mesh.triangles[triangle][corner]);
            const auto &actual = part.vertices.at(localTriangle[corner]);
            EXPECT_DOUBLE_EQ(actual.x, source.x - displayOrigin.x);
            EXPECT_DOUBLE_EQ(actual.y, source.y - displayOrigin.y);
            EXPECT_DOUBLE_EQ(actual.z, source.z - displayOrigin.z);
        }
    }
    const auto &modelFrame = model->summary().frame; // 已包含截面方向交换，不能再次交换。
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        const auto &part = parts[index];
        EXPECT_EQ(offsets[index], part.triangles.size());
        ASSERT_TRUE(part.thicknessMm);
        EXPECT_FALSE(part.thicknessEstimated);
        EXPECT_DOUBLE_EQ(*part.thicknessMm, part.id == "web" ? 4 : part.id.starts_with("flange-") ? 5 : 3);
        colors.insert(part.colorSrgb);
        for (const auto &direction : {modelFrame.xAxis, modelFrame.yAxis, modelFrame.zAxis})
        {
            const auto project = [direction](const Vec3 &point)
            {
                return point.x * direction.x + point.y * direction.y + point.z * direction.z;
            };
            double minimum = std::numeric_limits<double>::max();
            double maximum = std::numeric_limits<double>::lowest();
            for (const auto &point : part.vertices)
            {
                minimum = std::min(minimum, project(point));
                maximum = std::max(maximum, project(point));
            }
            for (const auto &face : part.labelFaces)
            {
                EXPECT_GE(project(face.position), minimum - 1e-8);
                EXPECT_LE(project(face.position), maximum + 1e-8);
            }
        }
    }
    EXPECT_EQ(colors.size(), parts.size());
}

TEST(ModelDisplay, KeepsColorsAndLabelsWhenPartOrderingChanges)
{
    const auto model = buildBeam({}, BeamParameters{100, 40, 60, 4.25, 5.5, 3.75, {25, 75}, false, true});
    auto mesh = model->mesh();
    auto summary = model->summary();
    const auto original = makeModelDisplayParts(mesh, summary, {});
    const Vec3 shiftedOrigin{1e6, -2e6, 3e6};
    const auto shifted = makeModelDisplayParts(mesh, summary, shiftedOrigin);
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        const auto &part = original[index];
        EXPECT_DOUBLE_EQ(shifted[index].boundsMinMm.x, part.boundsMinMm.x);
        EXPECT_DOUBLE_EQ(shifted[index].boundsMinMm.y, part.boundsMinMm.y);
        EXPECT_DOUBLE_EQ(shifted[index].boundsMinMm.z, part.boundsMinMm.z);
        EXPECT_DOUBLE_EQ(shifted[index].boundsMaxMm.x, part.boundsMaxMm.x);
        EXPECT_DOUBLE_EQ(shifted[index].boundsMaxMm.y, part.boundsMaxMm.y);
        EXPECT_DOUBLE_EQ(shifted[index].boundsMaxMm.z, part.boundsMaxMm.z);
        EXPECT_DOUBLE_EQ(part.boundsMinMm.x, std::min_element(part.vertices.begin(), part.vertices.end(),
            [](const auto &a, const auto &b) { return a.x < b.x; })->x);
        EXPECT_DOUBLE_EQ(part.boundsMaxMm.x, std::max_element(part.vertices.begin(), part.vertices.end(),
            [](const auto &a, const auto &b) { return a.x < b.x; })->x);
    }
    std::reverse(mesh.partIds.begin(), mesh.partIds.end());
    std::reverse(summary.partIds.begin(), summary.partIds.end());
    for (auto &owner : mesh.trianglePartIndices)
    {
        owner = static_cast<std::uint32_t>(mesh.partIds.size() - 1 - owner);
    }
    const auto reordered = makeModelDisplayParts(mesh, summary, {});
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        const auto &actual = reordered[original.size() - 1 - index];
        EXPECT_EQ(actual.id, original[index].id);
        EXPECT_EQ(actual.colorSrgb, original[index].colorSrgb);
        EXPECT_EQ(actual.thicknessMm, original[index].thicknessMm);
        for (std::size_t side = 0; side < actual.labelFaces.size(); ++side)
        {
            EXPECT_DOUBLE_EQ(actual.labelFaces[side].position.x, original[index].labelFaces[side].position.x);
            EXPECT_DOUBLE_EQ(actual.labelFaces[side].normal.x, original[index].labelFaces[side].normal.x);
        }
    }
}

TEST(ModelDisplay, RejectsInconsistentSnapshots)
{
    const auto model = buildBeam({}, BeamParameters{100, 40, 60, 4, 5, 0, {}, false});
    auto mesh = model->mesh();
    mesh.trianglePartIndices.pop_back();
    EXPECT_THROW((void)makeModelDisplayParts(mesh, model->summary(), {}), std::invalid_argument);
    mesh = model->mesh();
    mesh.trianglePartIndices[0] = static_cast<std::uint32_t>(mesh.partIds.size());
    EXPECT_THROW((void)makeModelDisplayParts(mesh, model->summary(), {}), std::invalid_argument);
    mesh = model->mesh();
    mesh.triangles[0][0] = static_cast<std::uint32_t>(mesh.vertices.size());
    EXPECT_THROW((void)makeModelDisplayParts(mesh, model->summary(), {}), std::invalid_argument);
    mesh = model->mesh();
    mesh.partIds[0] = "wrong-part";
    EXPECT_THROW((void)makeModelDisplayParts(mesh, model->summary(), {}), std::invalid_argument);
}

TEST(ModelDisplay, HidesOccludedRibsAndSwitchesSidesWithCamera)
{
    const auto model = buildBeam({}, BeamParameters{100, 40, 60, 4, 5, 3, {35}, false, true});
    const auto parts = makeModelDisplayParts(model->mesh(), model->summary(), {});
    const ModelLabelVisibility visibility(parts);
    const auto visibleFaces = [&](std::string_view id, const Vec3 &toEye)
    {
        const auto part = std::find_if(parts.begin(), parts.end(), [id](const auto &value) { return value.id == id; });
        return std::count_if(part->labelFaces.begin(), part->labelFaces.end(),
            [&](const auto &face) { return visibility.isVisible(face, toEye); });
    };
    // 从腹板左侧斜看：左肋板只有朝向相机的一面标注，右肋板被腹板遮住。
    EXPECT_EQ(visibleFaces("rib-1-left", {0.2, -1, 0.1}), 1);
    EXPECT_EQ(visibleFaces("rib-1-right", {0.2, -1, 0.1}), 0);
    EXPECT_EQ(visibleFaces("web", {0.2, -1, 0.1}), 1);
    EXPECT_EQ(visibleFaces("rib-1-left", {-0.2, 1, 0.1}), 0);
    EXPECT_EQ(visibleFaces("rib-1-right", {-0.2, 1, 0.1}), 1);
    EXPECT_EQ(visibleFaces("rib-1-left", {0, -1, 0}), 0); // 掠视主表面不挂标注。
    EXPECT_EQ(visibleFaces("flange-top", {0, 0, 1}), 1);
    EXPECT_EQ(visibleFaces("flange-bottom", {0, 0, 1}), 0);
}

TEST(ModelDisplay, OcclusionUsesDisplayCoordinatesAndFiniteEyeDistance)
{
    const double axis = std::sqrt(0.5);
    const BeamFrame frame{{1e6, 2e6, -3e6}, {axis, axis, 0}, {-axis, axis, 0}, {0, 0, 1}};
    const auto model = buildBeam(frame, BeamParameters{100, 40, 60, 4, 5, 3, {50}, false, true});
    const auto parts = makeModelDisplayParts(model->mesh(), model->summary(), frame.originMm);
    const ModelLabelVisibility visibility(parts);
    const auto &farRibFace = parts.at(4).labelFaces[1];
    const Vec3 toEye{axis * 1.2, -axis * 0.8, 0.1}; // 局部 (0.2, -1, 0.1)。
    EXPECT_FALSE(visibility.isVisible(farRibFace, toEye));
    // 相机处于右侧腔体内，腹板在眼睛之后，不得用无限射线误判遮挡。
    EXPECT_TRUE(visibility.isVisible(farRibFace, toEye, 1.0));
    EXPECT_FALSE(visibility.isVisible(farRibFace, {}));
}

TEST(PointCloudDisplay, ColorsByHeightWithoutChangingPointsOrFollowingView)
{
    CloudData cloud;
    cloud.originMm = {1e9, -1e9, 2e9};
    for (int index = 0; index <= 100; ++index)
    {
        cloud.points.push_back({{static_cast<double>(index), 0, static_cast<double>(index)}, 0, 0});
    }
    const auto points = makeCloudDisplayPoints(cloud, {0, 0, 1});
    const auto reversedAxis = makeCloudDisplayPoints(cloud, {0, 0, -1});
    ASSERT_EQ(points.size(), cloud.points.size());
    EXPECT_NE(points.front().colorSrgb, points.back().colorSrgb);
    EXPECT_GT(points.front().colorSrgb[2], points.front().colorSrgb[1]); // 紫色低层。
    EXPECT_GT(points.back().colorSrgb[1], points.back().colorSrgb[0]); // 青色高层。
    for (std::size_t index = 0; index < points.size(); ++index)
    {
        EXPECT_EQ(points[index].position.x, cloud.points[index].position.x);
        EXPECT_EQ(points[index].position.z, cloud.points[index].position.z);
        EXPECT_EQ(points[index].colorSrgb, reversedAxis[index].colorSrgb);
    }
    const auto sampled = makeCloudDisplayPoints(cloud, {0, 0, 1}, 10);
    EXPECT_LE(sampled.size(), 10u);
    EXPECT_EQ(sampled.at(1).position.x, 11);
}

TEST(PointCloudDisplay, HandlesEmptyFlatAndOutlierClouds)
{
    CloudData cloud;
    EXPECT_TRUE(makeCloudDisplayPoints(cloud, {0, 0, 1}).empty());
    cloud.points.resize(100);
    cloud.points.back().position.z = 1e9;
    const auto points = makeCloudDisplayPoints(cloud, {0, 0, 1});
    EXPECT_EQ(points.front().colorSrgb, points.back().colorSrgb); // 单个离群点不拉伸退化色域。
    for (const double channel : points.front().colorSrgb)
    {
        EXPECT_TRUE(std::isfinite(channel));
        EXPECT_GE(channel, 0.0);
        EXPECT_LE(channel, 1.0);
    }
    EXPECT_THROW((void)makeCloudDisplayPoints(cloud, {}), std::invalid_argument);
    EXPECT_THROW((void)makeCloudDisplayPoints(cloud, {0, 0, 1}, 0), std::invalid_argument);
}
}
