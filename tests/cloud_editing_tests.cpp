/*
 * 文件描述：
 * 验证完整点云的正交/透视框选、增减选择、裁剪属性保留、边界和取消语义。
 */
#include <limits>

#include <gtest/gtest.h>

#include "geometry/CloudEditing.h"
#include "workbench/PointCloudDisplayData.h"

namespace
{
using namespace reverse;

CloudSelectionRect identityRectangle()
{
    CloudSelectionRect rectangle;
    rectangle.viewProjection = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    rectangle.minX = rectangle.minY = -0.25;
    rectangle.maxX = rectangle.maxY = 0.25;
    return rectangle;
}

CloudData layeredCloud()
{
    CloudData cloud;
    cloud.fields = {"x", "y", "z", "intensity"};
    cloud.originMm = {1e9, -1e9, 5e9};
    cloud.hasIntensity = true;
    cloud.sourcePointCount = 8;
    cloud.invalidPointCount = 2;
    cloud.sourceUnit = LengthUnit::Meter;
    cloud.sourceScaleToMm = 1000;
    cloud.points = {{{0, 0, -0.5}, 12, 0}, {{0, 0, 0.5}, 13, 2}, {{0.25, -0.25, 0}, 14, 3},
                    {{0.8, 0, 0}, 15, 4},  {{0, 0, 2}, 16, 6},   {{0, 0, -2}, 17, 7}};
    return cloud;
}

TEST(CloudEditing, SelectsThroughDepthAndCombinesSelectionWithoutDuplicates)
{
    const auto cloud = layeredCloud();
    auto rectangle = identityRectangle();
    const auto selected = selectCloudPoints(cloud, rectangle, CloudSelectionMode::Replace);
    EXPECT_EQ(selected, (CloudSelection{0, 1, 2})); // 含边界和后层；视锥外的点被排除。
    rectangle.minX = 0.2;
    rectangle.maxX = 0.9;
    const auto added = selectCloudPoints(cloud, rectangle, CloudSelectionMode::Add, selected);
    EXPECT_EQ(added, (CloudSelection{0, 1, 2, 3}));
    EXPECT_EQ(selectCloudPoints(cloud, rectangle, CloudSelectionMode::Remove, added), (CloudSelection{0, 1}));
    EXPECT_TRUE(selectCloudPoints(cloud, {}, CloudSelectionMode::Clear, added).empty());
    rectangle = identityRectangle();
    rectangle.zeroToOneDepth = true;
    EXPECT_EQ(selectCloudPoints(cloud, rectangle, CloudSelectionMode::Replace), (CloudSelection{1, 2}));
}

TEST(CloudEditing, HonorsRotationTranslationPerspectiveAndBehindCamera)
{
    CloudData cloud;
    cloud.points = {
        {{10, 20, 2}, 0, 0}, {{10, 21, 2}, 0, 1}, {{10, 21, 4}, 0, 2}, {{10, 20, -2}, 0, 3}, {{10, 20, 0}, 0, 4}};
    auto rectangle = identityRectangle();
    // 平移至 (10,20)，旋转 XY 轴，透视除以 Z；第二点投影 x=0.5，第三点 x=0.25。
    rectangle.viewProjection = {0, 1, 0, -20, -1, 0, 0, 10, 0, 0, 0.5, 0, 0, 0, 1, 0};
    EXPECT_EQ(selectCloudPoints(cloud, rectangle, CloudSelectionMode::Replace), (CloudSelection{0, 2}));
}

TEST(CloudEditing, UsesFullResolutionAndPreservesAttributesOriginAndBounds)
{
    auto cloud = layeredCloud();
    const auto selected = selectCloudPoints(cloud, identityRectangle(), CloudSelectionMode::Replace);
    const auto kept = editCloudPoints(cloud, selected, CloudEditMode::KeepSelection);
    const auto removed = editCloudPoints(cloud, selected, CloudEditMode::DeleteSelection);
    ASSERT_EQ(kept->points.size(), 3u);
    ASSERT_EQ(removed->points.size(), 3u);
    EXPECT_EQ(kept->points[1].sourceIndex, 2u);
    EXPECT_EQ(kept->points[1].intensity, 13);
    EXPECT_EQ(removed->points[0].sourceIndex, 4u);
    EXPECT_EQ(kept->fields, cloud.fields);
    EXPECT_TRUE(kept->hasIntensity);
    EXPECT_EQ(kept->sourceUnit, LengthUnit::Meter);
    EXPECT_EQ(kept->sourceScaleToMm, 1000);
    EXPECT_EQ(kept->sourcePointCount, 8u);
    EXPECT_EQ(kept->invalidPointCount, 2u);
    EXPECT_EQ(kept->originMm.x, cloud.originMm.x);
    EXPECT_EQ(kept->originMm.y, cloud.originMm.y);
    EXPECT_EQ(kept->originMm.z, cloud.originMm.z);
    EXPECT_EQ(kept->boundsMinMm.y, -0.25);
    EXPECT_EQ(kept->boundsMaxMm.x, 0.25);
    EXPECT_EQ(kept->boundsMinMm.z, -0.5);
    EXPECT_EQ(kept->boundsMaxMm.z, 0.5);
    EXPECT_EQ(cloud.points.size(), 6u);

    cloud.points.resize(1000, {{0, 0, 0}, 23, 2000});
    const auto display = workbench::makeCloudDisplayPoints(cloud, {0, 0, 1}, 10);
    EXPECT_EQ(display.size(), 10u);
    EXPECT_EQ(display[1].pointIndex, 100u);
    const auto full = selectCloudPoints(cloud, identityRectangle(), CloudSelectionMode::Replace);
    EXPECT_EQ(full.size(), 997u);
    EXPECT_EQ(editCloudPoints(cloud, full, CloudEditMode::KeepSelection)->points.size(), 997u);
}

TEST(CloudEditing, RejectsInvalidSelectionAndSupportsEmptyResultAndCancellation)
{
    const auto cloud = layeredCloud();
    auto rectangle = identityRectangle();
    rectangle.maxX = rectangle.minX;
    EXPECT_THROW((void)selectCloudPoints(cloud, rectangle, CloudSelectionMode::Replace), GeometryError);
    rectangle = identityRectangle();
    rectangle.viewProjection[0] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW((void)selectCloudPoints(cloud, rectangle, CloudSelectionMode::Replace), GeometryError);
    EXPECT_THROW((void)editCloudPoints(cloud, {}, CloudEditMode::DeleteSelection), GeometryError);
    EXPECT_THROW((void)editCloudPoints(cloud, CloudSelection{2, 1}, CloudEditMode::DeleteSelection), GeometryError);
    EXPECT_THROW((void)editCloudPoints(cloud, CloudSelection{0, 0}, CloudEditMode::KeepSelection), GeometryError);
    EXPECT_THROW((void)editCloudPoints(cloud, CloudSelection{6}, CloudEditMode::DeleteSelection), GeometryError);
    EXPECT_TRUE(
        editCloudPoints(cloud, CloudSelection{0, 1, 2, 3, 4, 5}, CloudEditMode::DeleteSelection)->points.empty());
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)selectCloudPoints(cloud, identityRectangle(), CloudSelectionMode::Replace, {}, stop.get_token()),
                 GeometryError);
    EXPECT_THROW((void)editCloudPoints(cloud, CloudSelection{0}, CloudEditMode::KeepSelection, stop.get_token()),
                 GeometryError);
    // 中途取消不得修改传入点云。
    std::stop_source during;
    EXPECT_THROW((void)editCloudPoints(cloud, CloudSelection{0}, CloudEditMode::KeepSelection, during.get_token(),
                                       [&](double, auto) { during.request_stop(); }),
                 GeometryError);
    EXPECT_EQ(cloud.points.size(), 6u);
}
} // namespace
