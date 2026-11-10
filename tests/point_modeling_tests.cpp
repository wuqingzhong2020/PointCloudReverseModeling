/* 文件描述：选点建模的尺寸、任意参考面、切除事务及命令历史回放回归。 */
#include <gtest/gtest.h>
#include "geometry/PointModeling.h"
#include "commands/CommandService.h"
#include "commands/PointModelingCommands.h"
#include "geometry/ModelImport.h"
#include <BRepPrimAPI_MakeBox.hxx>
#include <chrono>
#include <thread>
#include <fstream>
#include <cmath>
#include <functional>
#include <limits>
namespace
{
using namespace reverse;
PointModeling rectangle()
{
    PointModeling r; r.points={{0,0,50},{100,0,50}}; r.width=20; r.height=50; return r;
}
TaskSnapshot wait(CommandService &service,const std::string &id)
{
    for (int i=0;i<6000;++i)
    {
        auto task=service.task(id); if (task.finished()) return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("task timeout");
}
TEST(PointModeling, RectangleAxisDiagonalCornerAndSizeOverrides)
{
    auto r=rectangle();
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,100000,1e-5);
    r.method="diagonal"; r.points[1]={100,20,50}; r.width=0;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,100000,1e-5);
    r.method="corner"; r.points={{0,0,50},{100,0,50},{100,20,50}};
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,100000,1e-5);
    r.length=120; r.width=30; r.height=10;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,36000,1e-5);
}
TEST(PointModeling, TwoAndThreePointCirclesHaveExactVolume)
{
    auto r=rectangle(); r.shape="circle"; r.method="center"; r.points={{0,0,50},{10,0,50}};
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,5000*std::acos(-1.0),1e-5);
    r.method="three"; r.points={{10,0,50},{0,10,50},{-10,0,50}};
    const auto p=pointModelProfile(r); EXPECT_NEAR(p.center.x,0,1e-7); EXPECT_NEAR(p.center.y,0,1e-7);
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,5000*std::acos(-1.0),1e-5);
    r.diameter=40;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,20000*std::acos(-1.0),1e-5);
}
TEST(PointModeling, ConcavePolygonAndPolylineKeepRealContours)
{
    auto r=rectangle(); r.shape="polygon"; r.points={{0,0,10},{20,0,10},{20,10,10},{10,10,10},{10,20,10},{0,20,10}};
    r.height=10;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,3000,1e-5);
    r.shape="polyline"; r.points={{0,0,10},{100,0,10},{100,50,10}}; r.width=10;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,15000,1e-5);
}
TEST(PointModeling, TiltedReferenceAndLargeOriginPreserveDimensions)
{
    auto r=rectangle(); r.normal={0,1,0}; r.origin={1000000,2000000,3000000};
    r.points={{1000000,2000050,3000000},{1000100,2000050,3000000}}; r.toPlane=true;
    const auto model=buildModelByPoints(nullptr,r);
    EXPECT_NEAR(model->summary().volumeMm3,100000,1e-4);
    const auto profile=pointModelProfile(r); EXPECT_NEAR(profile.extrusion.y,-50,1e-9);
}
TEST(PointModeling, InvalidContoursAndDimensionsDoNotProduceModels)
{
    auto r=rectangle(); r.points[1]=r.points[0]; EXPECT_THROW((void)buildModelByPoints(nullptr,r),GeometryError);
    r=rectangle(); r.shape="polygon"; r.points={{0,0,50},{20,20,50},{0,20,50},{20,0,50}};
    EXPECT_THROW((void)buildModelByPoints(nullptr,r),GeometryError);
    r=rectangle(); r.shape="circle"; r.method="three"; r.points.push_back({200,0,50});
    EXPECT_THROW((void)buildModelByPoints(nullptr,r),GeometryError);
    r=rectangle(); r.height=0; EXPECT_THROW((void)buildModelByPoints(nullptr,r),GeometryError);
    r=rectangle(); std::stop_source stop; stop.request_stop();
    EXPECT_THROW((void)buildModelByPoints(nullptr,r,stop.get_token()),GeometryError);
}
TEST(PointModeling, ObstacleIncludesOriginalReferencePlaneAndHonorsSection)
{
    PointModeling r; r.shape="box"; r.points={{10,20,12},{40,20,30},{10,60,30},{40,60,25},{999,999,1}};
    r.offset=10;
    auto model=buildModelByPoints(nullptr,r);
    EXPECT_NEAR(model->summary().volumeMm3,30*40*30,1e-6);
    ASSERT_EQ(model->summary().obstacleParts.size(),1); EXPECT_TRUE(model->summary().obstacleParts.begin()->second.boundingBox);
    EXPECT_TRUE(model->summary().thicknessOverrides.empty());
    r.points={{10,20,-12},{40,20,-30},{10,60,-30},{40,60,-25}}; r.reversed=true; r.offset=-10;
    EXPECT_NEAR(buildModelByPoints(nullptr,r)->summary().volumeMm3,30*40*30,1e-6);
}
TEST(PointModeling, ManualCutSplitsOnlySelectedPartAndPreservesSource)
{
    auto source=buildModelByPoints(nullptr,rectangle());
    auto added=rectangle(); for (auto &p:added.points) p.y+=100;
    source=buildModelByPoints(source.get(),added);
    auto cut=rectangle(); cut.method="diagonal"; cut.points={{45,-20,60},{55,20,60}};
    cut.width=0; cut.height=70; cut.cut=true; cut.cutScope="manual"; cut.targets={source->summary().partIds.front()};
    const auto result=buildModelByPoints(source.get(),cut);
    EXPECT_EQ(result->summary().solidCount,3);
    EXPECT_NEAR(result->summary().volumeMm3,190000,1e-5);
    EXPECT_NEAR(source->summary().volumeMm3,200000,1e-5);
    cut.targets={"missing"}; EXPECT_THROW((void)buildModelByPoints(source.get(),cut),GeometryError);
}
TEST(PointModeling, FullRemovalAndNoIntersectionAreHandled)
{
    auto source=buildModelByPoints(nullptr,rectangle());
    auto cut=rectangle(); cut.cut=true; cut.cutScope="all"; cut.points={{-1,0,60},{101,0,60}}; cut.width=30; cut.height=70;
    EXPECT_EQ(buildModelByPoints(source.get(),cut)->summary().solidCount,0);
    for (auto &p:cut.points) p.y+=100;
    EXPECT_THROW((void)buildModelByPoints(source.get(),cut),GeometryError);
}
TEST(PointModeling, ServicePreviewCommitUndoRedoAndLateRecording)
{
    CommandService service; const auto r=rectangle(); const auto initial=service.project().revision;
    const auto id=service.submitPointModelingPreview(r,initial);
    ASSERT_EQ(wait(service,id).state,TaskState::Succeeded);
    const auto preview=service.takeEdgePreview(id); ASSERT_TRUE(preview); EXPECT_FALSE(service.project().model);
    auto done=wait(service,service.submitPointModeling(r,initial));
    ASSERT_EQ(done.state,TaskState::Succeeded) << done.message;
    EXPECT_TRUE(done.result.committed); ASSERT_TRUE(service.project().model);
    EXPECT_EQ(wait(service,service.submitPointModeling(r,initial)).state,TaskState::Failed);
    ASSERT_EQ(wait(service,service.submitLine("project.undo")).state,TaskState::Succeeded);
    EXPECT_FALSE(service.project().model);
    ASSERT_EQ(wait(service,service.submitLine("project.redo")).state,TaskState::Succeeded);
    const auto dir=std::filesystem::temp_directory_path()/std::filesystem::path("point-model-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    ASSERT_EQ(wait(service,service.submitStartRecording(dir/"late.py")).state,TaskState::Succeeded);
    ASSERT_EQ(wait(service,service.submitStopRecording()).state,TaskState::Succeeded);
    CommandService replay; done=wait(replay,replay.submitScriptFile(dir/"late.py"));
    ASSERT_EQ(done.state,TaskState::Succeeded) << done.message;
    EXPECT_NEAR(replay.project().model->volumeMm3,100000,1e-5);
    for (const auto *ext : {".step",".cwrm"})
    {
        const auto path=dir/(std::string("model")+ext);
        if (std::string(ext)==".step") (void)exportStep(*preview,path); else (void)exportCwrm(*preview,path,{},{});
        const auto loaded=loadCadModel(path); EXPECT_NEAR(loaded->summary().volumeMm3,100000,1e-4);
    }
    std::filesystem::remove_all(dir);
}
TEST(PointModeling, ServiceObstacleUsesFullCloudAndWorldOrigin)
{
    const auto path=std::filesystem::temp_directory_path()/std::filesystem::path("point-obstacle-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".pcd");
    std::ofstream file(path);
    file << "# .PCD v0.7\nVERSION 0.7\nFIELDS x y z\nSIZE 4 4 4\nTYPE F F F\nCOUNT 1 1 1\nWIDTH 4\nHEIGHT 1\nPOINTS 4\nDATA ascii\n10000 20000 30012\n10030 20000 30030\n10000 20040 30030\n10030 20040 30025\n"; file.close();
    CommandService service; auto done=wait(service,service.submitImport(path)); ASSERT_EQ(done.state,TaskState::Succeeded) << done.message;
    PointModeling r; r.shape="box"; r.origin={10000,20000,30000}; r.indices={0,1,2,3};
    done=wait(service,service.submitPointModeling(r,service.project().revision));
    ASSERT_EQ(done.state,TaskState::Succeeded) << done.message;
    EXPECT_NEAR(service.project().model->volumeMm3,36000,1e-4);
    EXPECT_EQ(service.project().model->obstacleParts.size(),1);
    r.indices.push_back(999); const auto model=service.project().model;
    EXPECT_EQ(wait(service,service.submitPointModeling(r,service.project().revision)).state,TaskState::Failed);
    EXPECT_EQ(service.project().model,model); std::filesystem::remove(path);
}

TEST(PointModeling, DefaultCutPreservesBaseWhileAllScopeRemovesIt)
{
    const auto source = buildBeam({}, {100, 40, 60, 4, 5, 3, {}});
    const auto originalVolume = source->summary().volumeMm3;
    auto cut = rectangle();
    cut.cut = true;
    cut.method = "diagonal";
    cut.width = 0;
    cut.height = 2000;
    cut.points = {{-1000, -1000, 1000}, {1000, 1000, 1000}};
    ASSERT_EQ(cut.cutScope, "no_base");
    const auto retained = buildModelByPoints(source.get(), cut);
    EXPECT_EQ(retained->summary().partIds, std::vector<std::string>{"web"});
    EXPECT_EQ(retained->summary().solidCount, 1u);
    EXPECT_NEAR(retained->summary().volumeMm3, 100 * 4 * 50, 1e-6);
    cut.cutScope = "all";
    const auto removed = buildModelByPoints(source.get(), cut);
    EXPECT_TRUE(removed->summary().partIds.empty());
    EXPECT_TRUE(removed->mesh().triangles.empty());
    EXPECT_NEAR(removed->summary().volumeMm3, 0, 1e-9);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, originalVolume);
    EXPECT_EQ(source->summary().partIds.size(), 3u);
}

TEST(PointModeling, ExtrudesToReferenceFromBothSidesOfTiltedPlane)
{
    const Vec3 origin{1e6, -2e6, 3e6};
    const Vec3 normal{0, 0.6, 0.8};
    for (const double distance : {-30.0, 30.0})
    {
        SCOPED_TRACE(distance);
        auto request = rectangle();
        request.toPlane = true;
        request.origin = origin;
        request.normal = normal;
        request.height = 123; // 自动至参考面时不使用手填高度。
        request.width = 12;
        request.points = {{origin.x, origin.y + normal.y * distance, origin.z + normal.z * distance},
                          {origin.x + 80, origin.y + normal.y * distance, origin.z + normal.z * distance}};
        const auto profile = pointModelProfile(request);
        EXPECT_NEAR(profile.height, 30, 1e-8);
        EXPECT_NEAR(profile.extrusion.y, -normal.y * distance, 1e-8);
        EXPECT_NEAR(profile.extrusion.z, -normal.z * distance, 1e-8);
        for (const auto &corner : profile.contour)
        {
            const auto endDistance = normal.y * (corner.y + profile.extrusion.y - origin.y) +
                                     normal.z * (corner.z + profile.extrusion.z - origin.z);
            EXPECT_NEAR(endDistance, 0, 1e-8);
        }
        EXPECT_NEAR(buildModelByPoints(nullptr, request)->summary().volumeMm3, 80 * 12 * 30, 1e-3);
    }
}

TEST(PointModeling, InvalidReferenceAndNonFiniteInputReturnActionableError)
{
    struct InvalidInput
    {
        const char *name;
        std::function<void(PointModeling &)> change;
    };
    const InvalidInput cases[]{
        {"zero normal", [](auto &r) { r.normal = {}; }},
        {"invalid axis", [](auto &r) { r.xAxis.x = std::numeric_limits<double>::quiet_NaN(); }},
        {"invalid origin", [](auto &r) { r.origin.y = std::numeric_limits<double>::infinity(); }},
        {"invalid point", [](auto &r) { r.points.front().z = std::numeric_limits<double>::quiet_NaN(); }},
        {"infinite dimension", [](auto &r) { r.length = std::numeric_limits<double>::infinity(); }},
        {"negative dimension", [](auto &r) { r.width = -1; }},
        {"on reference plane",
         [](auto &r)
         {
             r.toPlane = true;
             for (auto &p : r.points)
             {
                 p.z = 0;
             }
         }},
        {"polyline reversal", [](auto &r)
         {
             r.shape = "polyline";
             r.points = {{0, 0, 50}, {50, 0, 50}, {0, 0, 50}};
         }}};
    for (const auto &test : cases)
    {
        SCOPED_TRACE(test.name);
        auto request = rectangle();
        test.change(request);
        try
        {
            (void)buildModelByPoints(nullptr, request);
            FAIL() << "Invalid geometry input must not produce a model";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "invalid_point_model");
            EXPECT_GT(std::string(error.what()).size(), 0u);
        }
    }
}

TEST(PointModeling, CancellationAfterGeometryCalculationPreservesSource)
{
    const auto source = buildModelByPoints(nullptr, rectangle());
    const auto originalTriangles = source->mesh().triangles;
    const auto originalVolume = source->summary().volumeMm3;
    auto addition = rectangle();
    for (auto &point : addition.points)
    {
        point.y += 100;
    }
    std::stop_source cancellation;
    bool completedGeometry = false;
    try
    {
        (void)buildModelByPoints(source.get(), addition, cancellation.get_token(),
                                 [&](double fraction, std::string_view)
                                 {
                                     if (fraction == 1.0)
                                     {
                                         completedGeometry = true;
                                         cancellation.request_stop();
                                     }
                                 });
        FAIL() << "Cancellation before publishing the model must be honored";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "cancelled");
    }
    EXPECT_TRUE(completedGeometry);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, originalVolume);
    EXPECT_EQ(source->mesh().triangles, originalTriangles);
    EXPECT_EQ(source->summary().partIds.size(), 1u);
}
} // namespace
