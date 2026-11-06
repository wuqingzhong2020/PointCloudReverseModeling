/*
 * 文件描述：用真实 OCCT owner 覆盖未命中、无对象和方向立方体命中，防止框选/双击空白崩溃。
 * 不创建 Qt 窗口、OpenGL 上下文或模拟界面操作。
 */
#include <AIS_PointCloud.hxx>
#include <AIS_ViewCube.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <Graphic3d_ArrayOfPoints.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Presentation.hxx>
#include <PrsMgr_Presentation.hxx>
#include <PrsMgr_PresentationManager.hxx>
#include <SelectBasics_PickResult.hxx>
#include <SelectMgr_SelectingVolumeManager.hxx>
#include <SelectMgr_SensitiveEntity.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <gtest/gtest.h>

#include "workbench/OccPicking.h"
#include "workbench/OccPartMesh.h"
#include "workbench/OccMeasurementPicking.h"

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
    const auto frontPoint = context->MainSelector()->PickedPoint(1);
    EXPECT_NEAR(frontPoint.X(), 2, 1e-9);
    EXPECT_NEAR(frontPoint.Y(), 2, 1e-9);
    EXPECT_NEAR(frontPoint.Z(), 5, 1e-9);
    front->setSelectable(context, false);
    EXPECT_TRUE(isDetectedInteractive(hit(2, 2), back));
    EXPECT_NEAR(context->MainSelector()->PickedPoint(1).Z(), 0, 1e-9);
    front->setSelectable(context, true);
    EXPECT_TRUE(isDetectedInteractive(hit(2, 2), front));
    EXPECT_TRUE(hit(9, 9).IsNull());
    context->Erase(front, false);
    context->Erase(back, false);
    EXPECT_TRUE(hit(2, 2).IsNull());
    context->RemoveAll(false);
    view->Remove();
}

TEST(ViewportPicking, CloudMeasurementReturnsActualPointAndHonorsVisibility)
{
    const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    const Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    const Handle(V3d_View) view = viewer->CreateView();
    const Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    const Handle(Graphic3d_ArrayOfPoints) points = new Graphic3d_ArrayOfPoints(2);
    points->AddVertex(gp_Pnt(2, 3, 7));
    points->AddVertex(gp_Pnt(2, 3, 1));
    const Handle(AIS_PointCloud) cloud = new AIS_PointCloud();
    cloud->SetPoints(points);
    context->Display(cloud, 0, -1, false);
    context->Load(cloud, AIS_PointCloud::SM_Points);
    context->Activate(cloud, AIS_PointCloud::SM_Points);
    const auto hit = [&]
    {
        context->MainSelector()->Pick(gp_Ax1(gp_Pnt(2, 3, 10), gp_Dir(0, 0, -1)), view);
        return context->MainSelector()->OnePicked();
    };
    ASSERT_TRUE(isDetectedInteractive(hit(), cloud));
    const auto point = context->MainSelector()->PickedPoint(1);
    EXPECT_DOUBLE_EQ(point.X(), 2);
    EXPECT_DOUBLE_EQ(point.Y(), 3);
    EXPECT_DOUBLE_EQ(point.Z(), 7);
    context->Deactivate(cloud);
    EXPECT_TRUE(hit().IsNull());
    context->Activate(cloud, AIS_PointCloud::SM_Points);
    ASSERT_TRUE(isDetectedInteractive(hit(), cloud));
    context->Erase(cloud, false);
    EXPECT_TRUE(hit().IsNull());
    context->RemoveAll(false);
    view->Remove();
}

TEST(ViewportPicking, TopologyModesReturnDistinctOwnersAndGroupWholeFaces)
{
    using namespace reverse::workbench;
    const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(4, 2, false);
    mesh->SetNode(1, gp_Pnt(0, 0, 0));
    mesh->SetNode(2, gp_Pnt(10, 0, 0));
    mesh->SetNode(3, gp_Pnt(10, 10, 0));
    mesh->SetNode(4, gp_Pnt(0, 10, 0));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    mesh->SetTriangle(2, Poly_Triangle(1, 3, 4));
    ModelDisplayPart data;
    data.keyPoints = {{0, 0, 0}, {10, 0, 0}, {10, 10, 0}, {0, 10, 0}};
    data.edges = {{{0, 0, 0}, {10, 0, 0}}, {{10, 0, 0}, {10, 10, 0}},
                  {{10, 10, 0}, {0, 10, 0}}, {{0, 10, 0}, {0, 0, 0}}};
    data.triangleFaceIndices = {7, 7};
    const Handle(OccPartMesh) part = new OccPartMesh(mesh, data);
    for (const auto mode : {ModelPickMode::Body, ModelPickMode::Vertex, ModelPickMode::Edge, ModelPickMode::Face})
    {
        part->RecomputePrimitives(static_cast<int>(mode));
        const auto &entities = part->Selection(static_cast<int>(mode))->Entities();
        EXPECT_EQ(entities.Size(), mode == ModelPickMode::Vertex || mode == ModelPickMode::Edge ? 4 : 1);
        for (const auto &entity : entities)
        {
            const auto *owner = dynamic_cast<const OccMeshOwner *>(entity->BaseSensitive()->OwnerId().get());
            ASSERT_NE(owner, nullptr);
            EXPECT_EQ(owner->kind, mode);
            if (mode == ModelPickMode::Face) EXPECT_EQ(owner->index, 7u);
        }
    }
    const auto face = part->Selection(3)->Entities().Value(0)->BaseSensitive();
    for (const auto &xy : {std::pair{8.0, 2.0}, std::pair{2.0, 8.0}})
    {
        SelectMgr_SelectingVolumeManager manager;
        manager.InitAxisSelectingVolume(gp_Ax1(gp_Pnt(xy.first, xy.second, 10), gp_Dir(0, 0, -1)));
        manager.BuildSelectingVolume();
        SelectBasics_PickResult result;
        EXPECT_TRUE(face->Matches(manager, result)); // 同一个原始面上两个三角形都属于面 7。
    }
}

TEST(ViewportPicking, CaptureRejectsOccludedVertexButKeepsFrontSurface)
{
    using namespace reverse::workbench;
    const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    const Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    const Handle(V3d_View) view = viewer->CreateView();
    view->Camera()->SetEye(gp_Pnt(2, 2, 10));
    view->Camera()->SetDirection(gp_Dir(0, 0, -1));
    const Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(3, 1, false);
    mesh->SetNode(1, gp_Pnt(0, 0, 5));
    mesh->SetNode(2, gp_Pnt(10, 0, 5));
    mesh->SetNode(3, gp_Pnt(0, 10, 5));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    const Handle(OccPartMesh) part = new OccPartMesh(mesh);
    context->Display(part, 0, -1, false);
    part->setSelectable(context, true);
    const auto surface = [&](const auto &owner) { return isDetectedInteractive(owner, part); };
    EXPECT_TRUE(isMeasurementPointVisible(gp_Pnt(2, 2, 5), context, view, surface));
    EXPECT_TRUE(isMeasurementPointVisible(gp_Pnt(0, 0, 5), context, view, surface));
    EXPECT_FALSE(isMeasurementPointVisible(gp_Pnt(2, 2, 0), context, view, surface));
    EXPECT_TRUE(isMeasurementPointVisible(gp_Pnt(9, 9, 0), context, view, surface));
    context->Erase(part, false);
    EXPECT_TRUE(isMeasurementPointVisible(gp_Pnt(2, 2, 0), context, view, surface));
    context->RemoveAll(false);
    view->Remove();
}

TEST(ViewportPicking, WireframeKeepsTopologyAndSelectionWhenSwitchingBackToSolid)
{
    using namespace reverse::workbench;
    const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    const Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    const Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(4, 2, false);
    mesh->SetNode(1, gp_Pnt(0, 0, 0)); mesh->SetNode(2, gp_Pnt(10, 0, 0));
    mesh->SetNode(3, gp_Pnt(10, 10, 0)); mesh->SetNode(4, gp_Pnt(0, 10, 0));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3)); mesh->SetTriangle(2, Poly_Triangle(1, 3, 4));
    ModelDisplayPart data;
    data.edges = {{{0, 0, 0}, {10, 0, 0}}, {{10, 0, 0}, {10, 10, 0}},
                  {{10, 10, 0}, {0, 10, 0}}, {{0, 10, 0}, {0, 0, 0}}};
    data.triangleFaceIndices = {1, 1};
    const Handle(OccPartMesh) part = new OccPartMesh(mesh, data);
    context->Display(part, 0, -1, false);
    part->setSelectable(context, true);
    const auto owner = part->selectionOwner(ModelPickMode::Body, 0);
    ASSERT_FALSE(owner.IsNull());
    setSelectedMeshOwners(context, {owner});
    const auto presentation = context->MainPrsMgr()->Presentation(part, 0);
    ASSERT_FALSE(presentation.IsNull());
    const auto solidGroups = presentation->NumberOfGroups();
    for (const bool wireframe : {true, false, true})
    {
        part->setWireframe(wireframe);
        context->Redisplay(part, false);
        context->HilightSelected(false);
        EXPECT_TRUE(context->IsSelected(owner));
        EXPECT_EQ(part->GetTriangulation(), mesh);
        // 四条拓扑边；切换线框时移除填充组，不能出现矩形的三角剖分对角线。
        EXPECT_EQ(presentation->NumberOfGroups(), wireframe ? 4 : solidGroups);
        EXPECT_FALSE(part->GetSelectPresentation({})->IsEmpty());
    }
    context->RemoveAll(false);
}

TEST(ViewportPicking, MultipleOwnersRetainIndependentHighlightsInEveryMode)
{
    using namespace reverse::workbench;
    const Handle(OpenGl_GraphicDriver) driver = new OpenGl_GraphicDriver(new Aspect_DisplayConnection(), false);
    const Handle(V3d_Viewer) viewer = new V3d_Viewer(driver);
    const Handle(AIS_InteractiveContext) context = new AIS_InteractiveContext(viewer);
    const Handle(Poly_Triangulation) mesh = new Poly_Triangulation(4, 2, false);
    mesh->SetNode(1, gp_Pnt(0, 0, 0));
    mesh->SetNode(2, gp_Pnt(10, 0, 0));
    mesh->SetNode(3, gp_Pnt(10, 10, 0));
    mesh->SetNode(4, gp_Pnt(0, 10, 0));
    mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    mesh->SetTriangle(2, Poly_Triangle(1, 3, 4));
    ModelDisplayPart data;
    data.keyPoints = {{0, 0, 0}, {10, 0, 0}};
    data.edges = {{{0, 0, 0}, {10, 0, 0}}, {{10, 0, 0}, {10, 10, 0}}};
    data.triangleFaceIndices = {3, 7};
    const Handle(OccPartMesh) part = new OccPartMesh(mesh, data);
    const Handle(OccPartMesh) otherPart = new OccPartMesh(mesh, data);
    context->Display(part, 0, -1, false);
    context->Display(otherPart, 0, -1, false);
    for (const auto mode : {ModelPickMode::Vertex, ModelPickMode::Edge, ModelPickMode::Face, ModelPickMode::Body})
    {
        context->ClearSelected(false);
        part->setSelectable(context, true, mode);
        otherPart->setSelectable(context, true, mode);
        const auto first = part->selectionOwner(mode, mode == ModelPickMode::Face ? 3 : 0);
        const auto other = otherPart->selectionOwner(mode, mode == ModelPickMode::Face ? 3 : 0);
        ASSERT_FALSE(first.IsNull());
        ASSERT_FALSE(other.IsNull());
        EXPECT_TRUE(part->selectionOwner(mode, 999).IsNull());
        std::vector<Handle(SelectMgr_EntityOwner)> owners{first};
        if (mode != ModelPickMode::Body)
        {
            const auto second = part->selectionOwner(mode, mode == ModelPickMode::Face ? 7 : 1);
            ASSERT_FALSE(second.IsNull());
            owners.push_back(second);
        }
        owners.push_back(other);
        setSelectedMeshOwners(context, owners);
        EXPECT_EQ(context->NbSelected(), mode == ModelPickMode::Body ? 2 : 3);
        const auto highlightedGroups = part->GetSelectPresentation({})->NumberOfGroups();
        EXPECT_GT(highlightedGroups, 0); // 面/体同时包含填充和轮廓，不假定一个对象只有一个绘制组。
        owners.erase(owners.begin());
        setSelectedMeshOwners(context, owners);
        EXPECT_FALSE(context->IsSelected(first));
        EXPECT_TRUE(context->IsSelected(other));
        EXPECT_EQ(context->NbSelected(), mode == ModelPickMode::Body ? 1 : 2);
        const auto remainingGroups = part->GetSelectPresentation({})->NumberOfGroups();
        EXPECT_LT(remainingGroups, highlightedGroups);
        if (mode == ModelPickMode::Body) EXPECT_EQ(remainingGroups, 0);
        else EXPECT_GT(remainingGroups, 0);
        setSelectedMeshOwners(context, {});
        EXPECT_EQ(context->NbSelected(), 0);
        EXPECT_EQ(otherPart->GetSelectPresentation({})->NumberOfGroups(), 0);
    }
    context->RemoveAll(false);
}
}
