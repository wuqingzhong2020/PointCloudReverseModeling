/*
 * 文件描述：用真实 OCCT owner 覆盖未命中、无对象和方向立方体命中，防止框选/双击空白崩溃。
 * 不创建 Qt 窗口、OpenGL 上下文或模拟界面操作。
 */
#include <AIS_PointCloud.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Poly_Triangulation.hxx>
#include <SelectBasics_PickResult.hxx>
#include <SelectMgr_SelectingVolumeManager.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gtest/gtest.h>

#include "workbench/OccPicking.h"
#include "workbench/OccPartMesh.h"

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

TEST(ViewportPicking, ModelPartHitsTrianglesRatherThanTheirBoundingBox)
{
    const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(3, 1, false);
    mesh->SetNode(1, gp_Pnt(0, 0, 0));
    mesh->SetNode(2, gp_Pnt(10, 0, 0));
    mesh->SetNode(3, gp_Pnt(0, 10, 0));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    const Handle(AIS_Triangulation) part = new reverse::workbench::OccPartMesh(mesh);
    part->RecomputePrimitives(0);
    const auto selection = part->Selection(0);
    ASSERT_FALSE(selection.IsNull());
    ASSERT_EQ(selection->Entities().Size(), 1);
    const auto sensitive = selection->Entities().Value(0)->BaseSensitive();
    EXPECT_TRUE(isDetectedInteractive(sensitive->OwnerId(), part));
    const auto hit = [&sensitive](double x, double y)
    {
        SelectMgr_SelectingVolumeManager manager;
        manager.InitAxisSelectingVolume(gp_Ax1(gp_Pnt(x, y, 10), gp_Dir(0, 0, -1)));
        manager.BuildSelectingVolume();
        SelectBasics_PickResult result;
        return sensitive->Matches(manager, result);
    };
    EXPECT_TRUE(hit(2, 2));
    EXPECT_FALSE(hit(9, 9)); // 在包围盒内、三角面外，不应误选。
    EXPECT_FALSE(hit(20, 20));
}

TEST(ViewportPicking, RegisteredPartsPickNearestAndRespectSelectionMode)
{
    // 使用真实 AIS 上下文和选择管理器，不初始化 OpenGL 上下文，不创建窗口。
    const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    const Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    const Handle(V3d_View) view = viewer->CreateView();
    const Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    const auto makePart = [](double z)
    {
        const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(3, 1, false);
        mesh->SetNode(1, gp_Pnt(0, 0, z));
        mesh->SetNode(2, gp_Pnt(10, 0, z));
        mesh->SetNode(3, gp_Pnt(0, 10, z));
        mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
        return Handle(reverse::workbench::OccPartMesh)(new reverse::workbench::OccPartMesh(mesh));
    };
    const auto front = makePart(5);
    const auto back = makePart(0);
    for (const auto &part : {front, back})
    {
        context->Display(part, 0, -1, false);
        part->setSelectable(context, true);
    }
    const auto hit = [&](double x, double y)
    {
        context->MainSelector()->Pick(gp_Ax1(gp_Pnt(x, y, 10), gp_Dir(0, 0, -1)), view);
        return context->MainSelector()->OnePicked();
    };
    EXPECT_TRUE(isDetectedInteractive(hit(2, 2), front));
    front->setSelectable(context, false);
    EXPECT_TRUE(isDetectedInteractive(hit(2, 2), back));
    front->setSelectable(context, true);
    EXPECT_TRUE(isDetectedInteractive(hit(2, 2), front));
    EXPECT_TRUE(hit(9, 9).IsNull());
    context->Erase(front, false);
    context->Erase(back, false);
    EXPECT_TRUE(hit(2, 2).IsNull());
    context->RemoveAll(false);
    view->Remove();
}
}
