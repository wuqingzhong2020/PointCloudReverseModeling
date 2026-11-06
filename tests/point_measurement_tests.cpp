/*
 * 文件描述：验证测量距离、坐标分解、交互状态及无效取点，不启动 GUI。
 */
#include <limits>

#include <gtest/gtest.h>

#include "workbench/PointMeasurement.h"

namespace
{
using reverse::Vec3;
using reverse::workbench::PointMeasurement;

TEST(PointMeasurement, DistanceUsesWorldAxisDifferencesInMillimeters)
{
    PointMeasurement measurement;
    ASSERT_TRUE(measurement.pick({1000000, -2000000, 3000000}));
    ASSERT_TRUE(measurement.pick({999997, -1999996, 2999988}));
    const auto result = measurement.result();
    ASSERT_TRUE(result);
    EXPECT_DOUBLE_EQ(result->distanceMm, 13);
    EXPECT_DOUBLE_EQ(result->componentsMm.x, 3);
    EXPECT_DOUBLE_EQ(result->componentsMm.y, 4);
    EXPECT_DOUBLE_EQ(result->componentsMm.z, 12);
    EXPECT_DOUBLE_EQ(result->componentPath[1].x, 999997);
    EXPECT_DOUBLE_EQ(result->componentPath[1].y, -2000000);
    EXPECT_DOUBLE_EQ(result->componentPath[2].y, -1999996);
    EXPECT_DOUBLE_EQ(result->componentPath[2].z, 3000000);
}

TEST(PointMeasurement, PreviewDoesNotCommitAndCompletedPairIgnoresHover)
{
    PointMeasurement measurement;
    EXPECT_FALSE(measurement.result(Vec3{1, 2, 3}));
    ASSERT_TRUE(measurement.pick({0, 0, 0}));
    ASSERT_TRUE(measurement.result(Vec3{3, 4, 0}));
    EXPECT_DOUBLE_EQ(measurement.result(Vec3{3, 4, 0})->distanceMm, 5);
    EXPECT_FALSE(measurement.second());
    EXPECT_FALSE(measurement.result());
    ASSERT_TRUE(measurement.pick({0, 0, 12}));
    EXPECT_DOUBLE_EQ(measurement.result(Vec3{300, 400, 0})->distanceMm, 12);
    ASSERT_TRUE(measurement.pick({5, 6, 7}));
    EXPECT_FALSE(measurement.second());
    EXPECT_DOUBLE_EQ(measurement.first()->x, 5);
    measurement.clear();
    EXPECT_FALSE(measurement.first());
    EXPECT_FALSE(measurement.result());
}

TEST(PointMeasurement, SamePointAndAxisAlignedPairsAreValid)
{
    PointMeasurement measurement;
    ASSERT_TRUE(measurement.pick({1, 2, 3}));
    ASSERT_TRUE(measurement.pick({1, 2, 3}));
    EXPECT_DOUBLE_EQ(measurement.result()->distanceMm, 0);
    ASSERT_TRUE(measurement.pick({1, 2, 3}));
    ASSERT_TRUE(measurement.pick({1, -18, 3}));
    EXPECT_DOUBLE_EQ(measurement.result()->distanceMm, 20);
    EXPECT_DOUBLE_EQ(measurement.result()->componentsMm.x, 0);
    EXPECT_DOUBLE_EQ(measurement.result()->componentsMm.z, 0);
}

TEST(PointMeasurement, InvalidCoordinatesAndOverflowDoNotAdvanceSelection)
{
    PointMeasurement measurement;
    const auto inf = std::numeric_limits<double>::infinity();
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(measurement.pick({nan, 0, 0}));
    EXPECT_FALSE(measurement.first());
    ASSERT_TRUE(measurement.pick({std::numeric_limits<double>::max(), 0, 0}));
    EXPECT_FALSE(measurement.pick({-std::numeric_limits<double>::max(), 0, 0}));
    EXPECT_FALSE(measurement.result(Vec3{0, inf, 0}));
    EXPECT_FALSE(measurement.second());
    ASSERT_TRUE(measurement.pick(*measurement.first()));
    EXPECT_DOUBLE_EQ(measurement.result()->distanceMm, 0);
    EXPECT_FALSE(measurement.pick({0, 0, nan}));
    EXPECT_TRUE(measurement.second());
}
TEST(PointMeasurement, KeepsCaptureFlagsForBothEndpointsAndResetsThem)
{
    PointMeasurement value;
    ASSERT_TRUE(value.pick({1, 2, 3}, true));
    ASSERT_TRUE(value.pick({4, 5, 6}));
    EXPECT_TRUE(value.firstSnapped());
    EXPECT_FALSE(value.secondSnapped());
    ASSERT_TRUE(value.pick({7, 8, 9}, true));
    EXPECT_TRUE(value.firstSnapped());
    EXPECT_FALSE(value.second());
    value.clear();
    EXPECT_FALSE(value.firstSnapped());
    EXPECT_FALSE(value.secondSnapped());
}
}
