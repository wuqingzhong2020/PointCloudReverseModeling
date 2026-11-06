/*
 * 文件描述：验证关键点捕获范围、深度和遮挡、相机更新及真实拓扑点筛选。
 */
#include <gtest/gtest.h>

#include "workbench/MeasurementSnap.h"

namespace
{
using namespace reverse;
using namespace reverse::workbench;
MeasurementSnapView identityView()
{
    return {{{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}}, 200, 200, false};
}
auto visible = [](const Vec3 &) { return true; };

TEST(MeasurementSnap, RadiusUsesScreenDistanceAndReturnsExactVertex)
{
    ModelDisplayPart part;
    part.keyPoints = {{0, 0, 0}};
    part.vertices = {{0.5, 0, 0}}; // 显示细分点不能被当作关键点。
    MeasurementSnap snap;
    snap.rebuild({part});
    snap.setView(identityView());
    ASSERT_TRUE(snap.find(116, 100, 16, visible));
    EXPECT_DOUBLE_EQ(snap.find(113, 107, 16, visible)->x, 0);
    EXPECT_FALSE(snap.find(117, 100, 16, visible));
    EXPECT_FALSE(snap.find(150, 100, 16, visible));
}

TEST(MeasurementSnap, PrefersNearestVisiblePointThenDepth)
{
    ModelDisplayPart part;
    part.keyPoints = {{0, 0, 0.4}, {0, 0, -0.4}, {0.1, 0, 0}};
    MeasurementSnap snap;
    snap.rebuild({part});
    snap.setView(identityView());
    EXPECT_DOUBLE_EQ(snap.find(100, 100, 16, visible)->z, -0.4);
    const auto result = snap.find(100, 100, 16, [](const Vec3 &p) { return p.x > 0; });
    ASSERT_TRUE(result);
    EXPECT_DOUBLE_EQ(result->x, 0.1);
    EXPECT_FALSE(snap.find(100, 100, 16, [](const Vec3 &) { return false; }));
}

TEST(MeasurementSnap, CameraAndModelChangesInvalidateScreenCache)
{
    ModelDisplayPart part;
    part.keyPoints = {{0, 0, 0}};
    MeasurementSnap snap;
    snap.rebuild({part});
    auto view = identityView();
    snap.setView(view);
    ASSERT_TRUE(snap.find(100, 100, 16, visible));
    view.viewProjection[3] = 0.5;
    snap.setView(view);
    EXPECT_FALSE(snap.find(100, 100, 16, visible));
    EXPECT_TRUE(snap.find(150, 100, 16, visible));
    snap.rebuild({});
    snap.setView(view);
    EXPECT_FALSE(snap.find(150, 100, 16, visible));
}

TEST(MeasurementSnap, HonorsDpiFrustumAndPerspectiveW)
{
    ModelDisplayPart part;
    part.keyPoints = {{0, 0, 0}};
    MeasurementSnap snap;
    snap.rebuild({part});
    auto view = identityView();
    view.width = view.height = 400;
    snap.setView(view);
    EXPECT_TRUE(snap.find(231, 200, 32, visible));
    EXPECT_FALSE(snap.find(233, 200, 32, visible));
    view.viewProjection[15] = -1;
    snap.setView(view);
    EXPECT_FALSE(snap.find(200, 200, 32, visible));
    view.viewProjection[15] = 1;
    view.viewProjection[11] = -0.1;
    view.zeroToOneDepth = true;
    snap.setView(view);
    EXPECT_FALSE(snap.find(200, 200, 32, visible));
    view.zeroToOneDepth = false;
    snap.setView(view);
    EXPECT_TRUE(snap.find(200, 200, 32, visible));
}
}
