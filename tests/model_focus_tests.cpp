/* 文件描述：验证整体取景、所选对象可见方向、显示原点以及视口宽高比。 */
#include <cmath>
#include <limits>
#include <gtest/gtest.h>
#include "workbench/ModelFocus.h"

namespace
{
using namespace reverse;
using namespace reverse::workbench;
ModelDisplayPart box(std::string id, Vec3 low, Vec3 high)
{
    ModelDisplayPart part; part.id = std::move(id);
    for (int i=0; i<8; ++i) part.vertices.push_back({i&1 ? high.x:low.x,i&2 ? high.y:low.y,i&4 ? high.z:low.z});
    part.triangles = {{0,2,3},{0,3,1},{4,5,7},{4,7,6},{0,1,5},{0,5,4},
        {2,6,7},{2,7,3},{0,4,6},{0,6,2},{1,3,7},{1,7,5}};
    return part;
}
double dot(Vec3 a,Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 cross(Vec3 a,Vec3 b) { return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x}; }

TEST(ModelFocus, ShowsWholeModelAroundSmallTargetAndPreservesClearView)
{
    auto target = box("target",{10,20,30},{20,30,32});
    // 元数据是世界坐标；相机和三角网格已经减去显示原点。
    target.boundsMinMm = {1000010,2000020,3000030};
    target.boundsMaxMm = {1000020,2000030,3000032};
    const auto focus = modelFocusView({box("beam",{-9000,-800,-300},{-20,800,-100}),target},"target",{0,0,1},{0,1,0},2);
    ASSERT_TRUE(focus);
    EXPECT_DOUBLE_EQ(focus->center.x,-4490); EXPECT_DOUBLE_EQ(focus->center.y,0);
    EXPECT_DOUBLE_EQ(focus->towardEye.z,1); EXPECT_NEAR(focus->scale,5637.5,1e-8);
}
TEST(ModelFocus, FindsVisibleDirectionForObjectBelowWorkpiece)
{
    const auto focus = modelFocusView({box("beam",{-100,-100,0},{100,100,10}),
        box("obstacle",{-10,-10,-12},{10,10,-10})},"obstacle",{0,0,1},{0,1,0},1);
    ASSERT_TRUE(focus);
    EXPECT_LT(focus->towardEye.z,-.8);
    EXPECT_NEAR(focus->scale,250,1e-8); // 观察下方目标时仍包含上方整块工件。
}
TEST(ModelFocus, EveryPartStaysInViewInPortraitAndLandscape)
{
    const std::vector<ModelDisplayPart> parts{box("beam",{-4500,-300,0},{4500,300,200}),
        box("obstacle-1",{-2800,360,-10},{-2710,440,10}),box("obstacle-2",{2100,360,-10},{2190,440,10})};
    for (double aspect : {.5,2.1})
        for (const auto &selected : {"obstacle-1","obstacle-2"})
        {
            const auto focus = modelFocusView(parts,selected,{0,0,1},{0,1,0},aspect);
            ASSERT_TRUE(focus);
            const auto right = cross(focus->up,focus->towardEye);
            for (const auto &part : parts) for (auto p : part.vertices)
            {
                p = {p.x-focus->center.x,p.y-focus->center.y,p.z-focus->center.z};
                EXPECT_LT(std::abs(dot(p,right)),focus->scale*aspect*.5);
                EXPECT_LT(std::abs(dot(p,focus->up)),focus->scale*.5);
            }
        }
}
TEST(ModelFocus, TurnsEdgeOnPlateAndFitsPortraitAndLandscape)
{
    const auto part = box("plate",{-50,-20,0},{50,20,1});
    for (double aspect : {.4,2.5})
    {
        const auto focus = modelFocusView({part},"plate",{1,0,0},{0,0,1},aspect);
        ASSERT_TRUE(focus);
        EXPECT_GT(std::abs(focus->towardEye.z),.8);
        EXPECT_NEAR(dot(focus->up,focus->towardEye),0,1e-9);
        EXPECT_NEAR(dot(focus->up,focus->up),1,1e-9);
        const auto right = cross(focus->up,focus->towardEye);
        for (auto p : part.vertices)
        {
            p = {p.x-focus->center.x,p.y-focus->center.y,p.z-focus->center.z};
            EXPECT_LT(std::abs(dot(p,right)),focus->scale*aspect*.5);
            EXPECT_LT(std::abs(dot(p,focus->up)),focus->scale*.5);
        }
    }
}
TEST(ModelFocus, InvalidTargetsDoNotChangeCamera)
{
    auto part = box("plate",{0,0,0},{10,10,1});
    EXPECT_FALSE(modelFocusView({part},"missing",{0,0,1},{0,1,0},1));
    EXPECT_FALSE(modelFocusView({part},"plate",{0,0,1},{0,1,0},0));
    part.vertices[0].x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_FALSE(modelFocusView({part},"plate",{0,0,1},{0,1,0},1));
    EXPECT_FALSE(modelFocusView({box("plate",{0,0,0},{0,0,0})},"plate",{0,0,1},{0,1,0},1));
}
}
