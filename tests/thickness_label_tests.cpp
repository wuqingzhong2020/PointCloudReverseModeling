/*
 * 文件描述：验证毫米标签格式、未知厚度与高 DPI 命中位置，避免双击错板或命中不可见标签。
 */
#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "workbench/ThicknessLabel.h"

namespace
{
using namespace reverse::workbench;

TEST(ThicknessLabel, NumericTextWithoutEstimateSuffix)
{
    EXPECT_EQ(thicknessLabelText(20.1), QStringLiteral("20.1 mm"));
    EXPECT_EQ(thicknessLabelText(14.236019), QStringLiteral("14.2 mm"));
    EXPECT_EQ(thicknessLabelText(13.97), QStringLiteral("14.0 mm"));
    EXPECT_EQ(thicknessLabelText(20.0), QStringLiteral("20.0 mm"));
    EXPECT_EQ(thicknessLabelText(0.001), QStringLiteral("0.0 mm"));
    EXPECT_EQ(thicknessLabelText(std::nullopt), QStringLiteral("未识别"));
    EXPECT_EQ(thicknessLabelText(0), QStringLiteral("未识别"));
    EXPECT_EQ(thicknessLabelText(std::numeric_limits<double>::quiet_NaN()), QStringLiteral("未识别"));
}

TEST(ThicknessLabel, MatchesVisiblePlateAndPreservesUnroundedValue)
{
    const std::vector<ThicknessLabelTarget> labels{
        {"web", 20.123456, QRect(100, 50, 80, 24)}, {"rib-1", 13.9, QRect(190, 100, 80, 24)}};
    const auto first = hitThicknessLabel(labels, QPointF(120, 60), 1);
    ASSERT_TRUE(first);
    EXPECT_EQ(first->partId, "web");
    EXPECT_DOUBLE_EQ(first->thicknessMm, 20.123456);
    const auto second = hitThicknessLabel(labels, QPointF(210, 110), 1);
    ASSERT_TRUE(second);
    EXPECT_EQ(second->partId, "rib-1");
    EXPECT_FALSE(hitThicknessLabel(labels, QPointF(180, 90), 1));
    EXPECT_FALSE(hitThicknessLabel({}, QPointF(120, 60), 1));
}

TEST(ThicknessLabel, HighDpiAndInvalidTargets)
{
    const std::vector<ThicknessLabelTarget> labels{{"web", 20.1, QRect(200, 100, 160, 48)}};
    ASSERT_TRUE(hitThicknessLabel(labels, QPointF(120, 60), 2));
    EXPECT_FALSE(hitThicknessLabel(labels, QPointF(120, 60), 1));
    ASSERT_TRUE(hitThicknessLabel(labels, QPointF(160, 80), 1.5));
    EXPECT_FALSE(hitThicknessLabel(labels, QPointF(120, 60), 0));
    EXPECT_FALSE(hitThicknessLabel(labels, QPointF(std::numeric_limits<double>::infinity(), 60), 2));
    const std::vector<ThicknessLabelTarget> invalid{
        {"", 20, QRect(100, 50, 80, 24)}, {"unknown", 0, QRect(100, 50, 80, 24)},
        {"hidden", 20, QRect()}};
    EXPECT_FALSE(hitThicknessLabel(invalid, QPointF(120, 60), 1));
}

TEST(ThicknessLabel, UnknownLabelIsEditableWithOneMillimeterDefault)
{
    const std::vector<ThicknessLabelTarget> labels{{"other-base", 1.0, QRect(200, 100, 160, 48), true}};
    const auto target = hitThicknessLabel(labels, QPointF(120, 60), 2);
    ASSERT_TRUE(target);
    EXPECT_TRUE(target->unresolved);
    EXPECT_DOUBLE_EQ(target->thicknessMm, 1.0);
}
}
