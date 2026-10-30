/*
 * 文件描述：用真实 OCCT owner 覆盖未命中、无对象和方向立方体命中，防止框选/双击空白崩溃。
 * 不创建 Qt 窗口、OpenGL 上下文或模拟界面操作。
 */
#include <AIS_PointCloud.hxx>
#include <AIS_ViewCube.hxx>
#include <gtest/gtest.h>

#include "workbench/OccPicking.h"

namespace
{
using reverse::workbench::isDetectedInteractive;

TEST(ViewportPicking, EmptyHitIsNotTheNavigationCube)
{
    const Handle(AIS_ViewCube) cube = new AIS_ViewCube();
    const Handle(SelectMgr_EntityOwner) noHit;
    EXPECT_FALSE(isDetectedInteractive(noHit, cube));
    EXPECT_FALSE(isDetectedInteractive(noHit, {}));
    const Handle(SelectMgr_EntityOwner) noObject = new SelectMgr_EntityOwner();
    EXPECT_FALSE(isDetectedInteractive(noObject, cube));
}

TEST(ViewportPicking, DistinguishesCubeAndCloudOwners)
{
    const Handle(AIS_ViewCube) cube = new AIS_ViewCube();
    const Handle(AIS_PointCloud) cloud = new AIS_PointCloud();
    const Handle(SelectMgr_EntityOwner) cubeOwner = new SelectMgr_EntityOwner(cube);
    const Handle(SelectMgr_EntityOwner) cloudOwner = new SelectMgr_EntityOwner(cloud);
    EXPECT_TRUE(isDetectedInteractive(cubeOwner, cube));
    EXPECT_FALSE(isDetectedInteractive(cloudOwner, cube));
    EXPECT_FALSE(isDetectedInteractive(cubeOwner, {}));
    cubeOwner->SetSelectable({});
    EXPECT_FALSE(isDetectedInteractive(cubeOwner, cube));
}
}
