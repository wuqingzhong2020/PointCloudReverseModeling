/* 文件描述：验证残余扫描障碍物的闭合包络、噪点/实体内点排除、编辑历史和双格式导出。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <thread>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include "geometry/ModelTransform.h"
#include <gtest/gtest.h>
#include "commands/CommandService.h"
#include "commands/Json.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/ObstacleReconstruction.h"
#include "workbench/ModelDisplayData.h"

namespace
{
using namespace reverse;
void add(CloudData &cloud, Vec3 p) { cloud.points.push_back({p, 0, cloud.points.size()}); }
void boxScan(CloudData &cloud, Vec3 low, Vec3 high, double step = 2)
{
    const double lo[]{low.x, low.y, low.z}, hi[]{high.x, high.y, high.z};
    for (int normal = 0; normal < 3; ++normal)
    {
        const int u = (normal + 1) % 3, v = (normal + 2) % 3;
        for (int side = 0; side < 2; ++side)
            for (double a = lo[u]; a <= hi[u] + 1e-8; a += step)
                for (double b = lo[v]; b <= hi[v] + 1e-8; b += step)
                {
                    double p[3]; p[normal] = side ? hi[normal] : lo[normal]; p[u] = a; p[v] = b;
                    add(cloud, {p[0], p[1], p[2]});
                }
    }
}
struct Files
{
    std::filesystem::path directory = std::filesystem::temp_directory_path() /
        ("obstacle-reconstruction-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files() { std::filesystem::create_directory(directory); }
    ~Files() { std::error_code error; std::filesystem::remove_all(directory, error); }
};
TaskSnapshot finish(CommandService &service, const std::string &id)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto task = service.task(id);
        if (task.finished()) return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    (void)service.cancel(id);
    throw std::runtime_error("Obstacle reconstruction timed out");
}

TEST(ObstacleReconstruction, ResidualClustersEncloseAllObservedPointsAndIgnoreNoiseAndSolidInterior)
{
    CloudData cloud;
    cloud.originMm = {1e6, -2e6, 3e6};
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(
        gp_Pnt(cloud.originMm.x, cloud.originMm.y, cloud.originMm.z), 120, 20, 60).Shape(), "step", {}, {});
    boxScan(cloud, {0, 0, 0}, {120, 20, 60});
    // 连续板面的轻微翘曲残差不能成为独立障碍物。
    boxScan(cloud, {30, 0, 60}, {50, 20, 63});
    boxScan(cloud, {20, 5, 20}, {40, 15, 40}); // 实体内部扫描点不应重复建模。
    const auto obstacleStart = cloud.points.size();
    boxScan(cloud, {-30, 40, 10}, {-10, 60, 30});
    boxScan(cloud, {150, 40, 0}, {174, 68, 0}); // 平面扫描亦须生成闭合包络。
    const auto obstacleEnd = cloud.points.size();
    for (int i = 0; i < 12; ++i) add(cloud, {800.0 + 25 * i, -500.0, 300.0});
    const auto shapes = detail::reconstructObstacleShapes(cloud, source->mesh(), {true}, 1, .05, {}, {});
    ASSERT_EQ(shapes.size(), 2);
    for (const auto &obstacle : shapes)
    {
        EXPECT_EQ(obstacle.shape.ShapeType(), TopAbs_SOLID);
        EXPECT_TRUE(BRepCheck_Analyzer(obstacle.shape).IsValid());
        EXPECT_GE(obstacle.info.supportPoints, 8);
        EXPECT_GT(obstacle.info.paddingMm, 0);
    }
    for (std::size_t i = obstacleStart; i < obstacleEnd; ++i)
    {
        const auto p = cloud.points[i].position;
        bool enclosed = false;
        for (const auto &obstacle : shapes)
        {
            BRepClass3d_SolidClassifier classify(obstacle.shape,
                gp_Pnt(p.x + cloud.originMm.x, p.y + cloud.originMm.y, p.z + cloud.originMm.z), 1e-6);
            enclosed |= classify.State() == TopAbs_IN || classify.State() == TopAbs_ON;
        }
        ASSERT_TRUE(enclosed) << i;
    }
    cloud.points.resize(obstacleStart);
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud, source->mesh(), {true}, 1, .05, {}, {}).empty());
    std::stop_source stop; stop.request_stop();
    EXPECT_THROW((void)detail::reconstructObstacleShapes(cloud, source->mesh(), {true}, 1, .05, stop.get_token(), {}), GeometryError);
}

TEST(ObstacleReconstruction, DistanceFilterUsesEachPointAndKeepsTheRestOfTheSameObject)
{
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(120,20,60).Shape(),"step",{},{});
    CloudData cloud;
    // 0.5 / 1 / 1.25 mm 三层在同一体素中，必须逐点过滤，不能删除整格或整个簇。
    for (double x : {.5,1.,1.25,3.5,5.5,7.5,9.5,11.5})
        for (double y : {5.,9.,13.}) for (double z : {10.,14.,18.})
            add(cloud,{120+x,y,z});
    EXPECT_DOUBLE_EQ(PlateBuildOptions{}.obstacleExclusionDistanceMm,1);
    const auto unfiltered = detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1,.05,{},{},nullptr,0);
    ASSERT_EQ(unfiltered.size(),1);
    EXPECT_EQ(unfiltered[0].info.supportPoints,72);
    const auto filtered = detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1,.05,{},{});
    ASSERT_EQ(filtered.size(),1);
    EXPECT_EQ(filtered[0].info.supportPoints,54); // 边界 1 mm 包含在排除带内，1.25 mm 保留。
    const auto wider = detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1,.05,{},{},nullptr,2);
    ASSERT_EQ(wider.size(),1);
    EXPECT_EQ(wider[0].info.supportPoints,45);
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1,.05,{},{},nullptr,20).empty());
    EXPECT_EQ(cloud.points.size(),72); // 原始点云不改变。
    CloudData thinObject;
    for (double y : {5.,9.,13.}) for (double z : {10.,14.,18.}) add(thinObject,{121.25,y,z});
    const auto thin=detail::reconstructObstacleShapes(thinObject,source->mesh(),{true},1,.05,{},{});
    ASSERT_EQ(thin.size(),1); // 独立的 1.25 mm 外小平面不能被旧的自动表面容差吞掉。
    EXPECT_EQ(thin[0].info.supportPoints,9);
    EXPECT_TRUE(detail::reconstructObstacleShapes(thinObject,source->mesh(),{true},1,.05,{},{},nullptr,2).empty());
    const double s=std::sqrt(.5);
    const BeamFrame frame{{1e6,-2e6,3e6},{s,s,0},{0,0,1},{s,-s,0}};
    auto rotatedMesh=source->mesh();
    auto rotated=cloud;
    rotated.originMm=frame.originMm;
    for (auto &v : rotatedMesh.vertices) v=transformPoint(frame,v);
    for (auto &p : rotated.points)
    {
        const auto v=transformPoint(frame,p.position);
        p.position={v.x-frame.originMm.x,v.y-frame.originMm.y,v.z-frame.originMm.z};
    }
    const auto moved=detail::reconstructObstacleShapes(rotated,rotatedMesh,{true},1,.05,{},{},nullptr,2);
    ASSERT_EQ(moved.size(),1);
    EXPECT_EQ(moved[0].info.supportPoints,45);
    for (double d : {-1.,1001.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW((void)detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1,.05,{},{},nullptr,d),GeometryError);
}

TEST(ObstacleReconstruction, MetadataDisplayDeletionAndExchangeKeepObstacleSeparateFromWorkpiece)
{
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(120, 20, 60).Shape(), "step", {}, {});
    CloudData cloud;
    boxScan(cloud, {120, 5, 5}, {140, 15, 25}); // 包络与工件相接，仍不参加工件镜像选择。
    PlateRecognition recognition; recognition.scanSpacingMm = 1; recognition.surfaceNoiseMm = .05;
    const auto completed = reconstructModelObstacles(*source, cloud, recognition);
    ASSERT_EQ(completed->summary().obstacleParts.size(), 1);
    const auto id = completed->summary().obstacleParts.begin()->first;
    EXPECT_EQ(completed->summary().solidCount, 2);
    EXPECT_EQ(source->summary().solidCount, 1);
    EXPECT_TRUE(source->summary().obstacleParts.empty());
    EXPECT_EQ(connectedModelParts(*completed, "imported-part-1"), std::vector<std::string>{"imported-part-1"});
    EXPECT_THROW((void)connectedModelParts(*completed, id), GeometryError);
    EXPECT_THROW((void)completeModelMirror(*completed, {{id}, "imported-part-1"}), GeometryError);
    const auto display = workbench::makeModelDisplayParts(completed->mesh(), completed->summary(), {});
    EXPECT_FALSE(display.back().thicknessMm);
    EXPECT_FALSE(display.back().thicknessUnresolved);
    // 有完整工件扫描时，仍保留与其接触的外部物体。
    auto touchingScan = cloud;
    boxScan(touchingScan, {0, 0, 0}, {120, 20, 60});
    const auto touchingModel = reconstructModelObstacles(*source, touchingScan, recognition);
    EXPECT_EQ(touchingModel->summary().obstacleParts.size(), 1);
    ModelTransform move; move.offsetMm = {50, 20, 30};
    const auto moved = transformModelParts(*completed, {id}, move);
    EXPECT_EQ(moved->summary().obstacleParts.at(id).supportPoints, completed->summary().obstacleParts.at(id).supportPoints);
    const auto removed = deleteModelParts(*completed, {id});
    EXPECT_TRUE(removed->summary().obstacleParts.empty());
    EXPECT_EQ(removed->summary().solidCount, 1);
    Files files;
    (void)exportStep(*completed, files.directory / "complete.step");
    (void)exportCwrm(*completed, files.directory / "complete.cwrm");
    for (const auto &name : {"complete.step", "complete.cwrm"})
    {
        const auto loaded = loadCadModel(files.directory / name);
        EXPECT_EQ(loaded->summary().solidCount, 2);
        EXPECT_NEAR(loaded->summary().volumeMm3, completed->summary().volumeMm3, 1e-3);
    }
}

TEST(ObstacleReconstruction, TrimmedPlateEndIsNotAnObstacleButIndependentAndTouchingObjectsRemain)
{
    CloudData cloud;
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{});
    boxScan(cloud,{-14,0,0},{200,80,10});
    PlateRecognition recognition;
    RecognizedPlate plate; plate.id = "web"; plate.normalAxis = 2;
    plate.minimumMm = {-10,0,0}; plate.maximumMm = {210,80,10}; plate.thicknessMm = 10;
    recognition.plates.push_back(plate);
    const auto mesh = source->mesh();
    ASSERT_FALSE(detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{}).empty());
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{},&recognition).empty());
    // 相同板面附近的小独立物体不能因符合板件误差带而消失。
    boxScan(cloud,{208,20,12},{210,26,14},1);
    // 与工件相接、明显超出板面高度的夹具仍应保留。
    boxScan(cloud,{80,20,10},{100,40,35});
    const auto shapes = detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{},&recognition);
    ASSERT_EQ(shapes.size(),2);
    for (const auto &p : {gp_Pnt(209,23,13),gp_Pnt(90,30,34)})
    {
        bool enclosed = false;
        for (const auto &obstacle : shapes)
        {
            BRepClass3d_SolidClassifier classify(obstacle.shape,p,1e-6);
            enclosed |= classify.State() == TopAbs_IN || classify.State() == TopAbs_ON;
        }
        EXPECT_TRUE(enclosed);
    }
}

TEST(ObstacleReconstruction, PlateResidualFilterUsesObservedPartFrameAndCloudOrigin)
{
    CloudData cloud;
    cloud.originMm = {1e6,-2e6,3e6};
    boxScan(cloud,{-14,0,0},{200,80,10});
    const BeamFrame frame{{1e6,-2e6,3e6},{0,1,0},{0,0,1},{1,0,0}};
    auto mesh = detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{})->mesh();
    for (auto &p : mesh.vertices) p = transformPoint(frame,p);
    for (auto &p : cloud.points)
    {
        const auto v = transformPoint(frame,p.position);
        p.position = {v.x-cloud.originMm.x,v.y-cloud.originMm.y,v.z-cloud.originMm.z};
    }
    PlateRecognition recognition;
    RecognizedPlate plate; plate.id = "rotated"; plate.normalAxis = 2;
    plate.minimumMm = {-10,0,0}; plate.maximumMm = {200,80,10}; plate.thicknessMm = 10;
    recognition.plates.push_back(plate);
    recognition.plateFrames.emplace_back("rotated",frame);
    ASSERT_FALSE(detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{}).empty());
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{},&recognition).empty());
    // 异形板的外包范围不等于板面，不能借此吞掉凹缺口中的对象。
    recognition.plates.front().outline.push_back({{0,0,0},{1,0,0},{}});
    EXPECT_FALSE(detail::reconstructObstacleShapes(cloud,mesh,{true},1,.05,{},{},&recognition).empty());
}

TEST(ObstacleReconstruction, DisconnectedScanStripsAtPlateEndAreNotIndependentObstacles)
{
    for (bool rotated : {false,true})
    {
        CloudData cloud;
        boxScan(cloud,{0,0,0},{200,80,10});
        for (const auto start : {20,52})
            for (int y=start; y<start+16; ++y)
                for (int sample=0; sample<5; ++sample)
                    add(cloud,{199.5+.3*std::sin(sample+y),double(y),20+.3*std::cos(sample-y)});
        auto mesh = detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{})->mesh();
        PlateRecognition recognition;
        RecognizedPlate plate; plate.id = "plate"; plate.normalAxis = 2;
        plate.minimumMm = {0,0,0}; plate.maximumMm = {200,80,10}; plate.thicknessMm = 10;
        recognition.plates.push_back(plate);
        if (rotated)
        {
            const double s = std::sqrt(.5);
            const BeamFrame frame{{1e6,-2e6,3e6},{s,s,0},{0,0,1},{s,-s,0}};
            cloud.originMm = frame.originMm;
            for (auto &p : mesh.vertices) p = transformPoint(frame,p);
            for (auto &p : cloud.points)
            {
                const auto v = transformPoint(frame,p.position);
                p.position = {v.x-cloud.originMm.x,v.y-cloud.originMm.y,v.z-cloud.originMm.z};
            }
            recognition.plateFrames.emplace_back(plate.id,frame);
        }
        // 不提供板件证据时，这两条扫描片段会被当成独立簇。
        ASSERT_EQ(detail::reconstructObstacleShapes(cloud,mesh,{true},1.46,1.52,{},{}).size(),2);
        EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,mesh,{true},1.46,1.52,{},{},&recognition).empty());
    }
}

TEST(ObstacleReconstruction, SparseEndStripAccountsForScanPitchWithoutDiscardingNearbyObjects)
{
    const auto source=detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{});
    CloudData cloud;
    // 条带跨越几个采样列，点间距和面噪声共同决定其横向误差。
    for (const auto p : std::vector<Vec3>{{202.62,20,20},{202.65,29.06,20.04},{202.60,19.01,20.11},
              {198.34,20.01,20.08},{200.13,29.06,20.09},{202.66,25.08,20.46},
              {200.14,19.01,20.15},{197.09,19.02,20.21}}) add(cloud,p);
    PlateRecognition recognition;
    RecognizedPlate plate; plate.id="plate"; plate.normalAxis=2;
    plate.minimumMm={0,0,0};plate.maximumMm={200,80,10};plate.thicknessMm=10;
    recognition.plates.push_back(plate);
    ASSERT_EQ(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{}).size(),1);
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{},&recognition).empty());
    boxScan(cloud,{180,50,20},{190,64,30},1);
    const auto kept=detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{},&recognition);
    ASSERT_EQ(kept.size(),1);
    EXPECT_GT(kept[0].info.supportPoints,8);
}

TEST(ObstacleReconstruction, ObliqueVisibleSideStripUsesMeasuredDirection)
{
    const auto source=detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{});
    CloudData cloud;
    for (int i=0;i<=60;++i)
        add(cloud,{200+.4*std::sin(i),10.0+i,20+std::tan(7.0*3.141592653589793/180.0)*(i-30)+.25*std::cos(i)});
    PlateRecognition recognition;
    RecognizedPlate plate;plate.id="plate";plate.normalAxis=2;
    plate.minimumMm={0,0,0};plate.maximumMm={200,80,10};plate.thicknessMm=10;
    recognition.plates.push_back(plate);
    ASSERT_EQ(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{}).size(),1);
    EXPECT_TRUE(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{},&recognition).empty());
    // 明显斜穿板边的线状物体不属于小角度斜视残差。
    for (auto &p : cloud.points) p.position.z=35+std::tan(.5)*(p.position.y-40);
    EXPECT_FALSE(detail::reconstructObstacleShapes(cloud,source->mesh(),{true},2.05,1.61,{},{},&recognition).empty());
}

TEST(ObstacleReconstruction, DetachedPlateEdgeFilterPreservesSmallFacesVolumesAndSeparateLines)
{
    CloudData cloud;
    boxScan(cloud,{0,0,0},{200,80,10});
    // 同样靠近板边、但有二维扫描宽度的小平面；点数不足以作为统一删除阈值。
    boxScan(cloud,{-4,20,21},{4,32,21},1);
    // 有可测厚度的小实体也保留，即使横向尺寸较小。
    boxScan(cloud,{198,52,21},{202,64,29},1);
    // 不贴合板件端边的一维扫描物体仍保留。
    for (int y=20; y<=40; ++y) add(cloud,{100,double(y),21});
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(200,80,10).Shape(),"step",{},{});
    PlateRecognition recognition;
    RecognizedPlate plate; plate.id = "plate"; plate.normalAxis = 2;
    plate.minimumMm = {0,0,0}; plate.maximumMm = {200,80,10}; plate.thicknessMm = 10;
    recognition.plates.push_back(plate);
    const auto shapes = detail::reconstructObstacleShapes(cloud,source->mesh(),{true},1.46,1.52,{},{},&recognition);
    ASSERT_EQ(shapes.size(),3);
    for (const auto &p : {gp_Pnt(0,26,21),gp_Pnt(200,58,25),gp_Pnt(100,30,21)})
    {
        bool enclosed = false;
        for (const auto &obstacle : shapes)
        {
            BRepClass3d_SolidClassifier classify(obstacle.shape,p,1e-6);
            enclosed |= classify.State() == TopAbs_IN || classify.State() == TopAbs_ON;
        }
        EXPECT_TRUE(enclosed);
    }
}

TEST(ObstacleReconstruction, HBeamCommandRetainsExternalObjectWithHistoryAndRecording)
{
    CloudData cloud;
    for (int x = -500; x <= 500; x += 5)
    {
        for (int y = -100; y <= 100; y += 5) add(cloud, {double(x), double(y), .01 * std::sin(x + y)});
        for (int width = 0; width <= 10; ++width)
        {
            add(cloud, {double(x), -100 + width * .8, 100});
            add(cloud, {double(x), 88 + width * 1.2, 100});
        }
    }
    boxScan(cloud, {-30, 170, 15}, {-10, 190, 35});
    cloud.sourcePointCount = cloud.points.size();
    Files files;
    const auto input = files.directory / "beam-obstacle.pcd", script = files.directory / "rebuild.py";
    (void)exportPointCloud(cloud, input);
    CommandService service;
    ASSERT_EQ(finish(service, service.submitImport(input)).state, TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitStartRecording(script)).state, TaskState::Succeeded);
    PlateBuildOptions options;
    options.obstacleExclusionDistanceMm=3.25;
    const auto task = finish(service, service.submitRecognizedReconstruct(options, service.project().revision));
    ASSERT_EQ(task.state, TaskState::Succeeded) << task.message;
    const auto completed = service.project();
    ASSERT_TRUE(completed.model);
    ASSERT_EQ(completed.model->obstacleParts.size(), 1);
    EXPECT_EQ(completed.model->plates.size(), 3);
    EXPECT_EQ(completed.model->solidCount, 4);
    EXPECT_EQ(toJson(completed)["model"]["obstacles"].size(), 1);
    EXPECT_EQ(toJson(completed)["model"]["recognitionOptions"]["obstacleExclusionDistanceMm"],3.25);
    EXPECT_EQ(workbench::makeModelDisplayParts(*completed.mesh, *completed.model, {}).size(), 4);
    ASSERT_EQ(finish(service, service.submitStopRecording()).state, TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_FALSE(service.project().model);
    ASSERT_EQ(finish(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model, completed.model);
    EXPECT_DOUBLE_EQ(service.project().committedRecognitionOptions->obstacleExclusionDistanceMm,3.25);
    CommandService replay;
    ASSERT_EQ(finish(replay, replay.submitScriptFile(script)).state, TaskState::Succeeded);
    EXPECT_EQ(toJson(replay.project())["model"]["obstacles"], toJson(completed)["model"]["obstacles"]);
    EXPECT_DOUBLE_EQ(replay.project().model->recognitionOptions.obstacleExclusionDistanceMm,3.25);
    EXPECT_NEAR(replay.project().model->volumeMm3, completed.model->volumeMm3, 1e-5);
}

std::vector<Vec3> partVertices(const MeshData &mesh, const std::string &id)
{
    const auto found = std::find(mesh.partIds.begin(), mesh.partIds.end(), id);
    std::vector<Vec3> points;
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
        if (mesh.trianglePartIndices[i] == std::distance(mesh.partIds.begin(), found))
            for (const auto vertex : mesh.triangles[i]) points.push_back(mesh.vertices[vertex]);
    return points;
}
void expectSamePart(const MeshData &a, const MeshData &b, const std::string &id)
{
    const auto pa = partVertices(a, id), pb = partVertices(b, id);
    ASSERT_EQ(pa.size(), pb.size());
    for (std::size_t i = 0; i < pa.size(); ++i)
    {
        EXPECT_DOUBLE_EQ(pa[i].x, pb[i].x);
        EXPECT_DOUBLE_EQ(pa[i].y, pb[i].y);
        EXPECT_DOUBLE_EQ(pa[i].z, pb[i].z);
    }
}
void expectOccupancyBox(const MeshData &before, const MeshData &after, const ModelSummary &summary, const std::string &id)
{
    const auto frame = modelPartFrame(summary, id);
    const auto points = partVertices(after, id);
    ASSERT_EQ(points.size(), 36); // 六个矩形面，各两个三角形。
    Vec3 low{1e100, 1e100, 1e100}, high{-1e100, -1e100, -1e100};
    for (const auto &p : points)
    {
        const auto q = inverseTransformPoint(frame, p);
        low = {std::min(low.x, q.x), std::min(low.y, q.y), std::min(low.z, q.z)};
        high = {std::max(high.x, q.x), std::max(high.y, q.y), std::max(high.z, q.z)};
    }
    for (const auto &p : partVertices(before, id))
    {
        const auto q = inverseTransformPoint(frame, p);
        EXPECT_GE(q.x, low.x - 1e-6); EXPECT_LE(q.x, high.x + 1e-6);
        EXPECT_GE(q.y, low.y - 1e-6); EXPECT_LE(q.y, high.y + 1e-6);
        EXPECT_GE(q.z, low.z - 1e-6); EXPECT_LE(q.z, high.z + 1e-6);
    }
    EXPECT_TRUE(summary.obstacleParts.at(id).boundingBox);
    EXPECT_FALSE(summary.thicknessOverrides.contains(id));
    for (const auto &part : workbench::makeModelDisplayParts(after, summary, {}))
        if (part.id == id) EXPECT_FALSE(part.thicknessMm);
}
TEST(ObstacleReconstruction, LocalBoxUsesCurrentGeometryAfterMovingWithoutPointCloud)
{
    BRep_Builder builder; TopoDS_Compound compound; builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeCylinder(10, 30).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(100, 0, 0), 10, 10, 10).Shape());
    const auto original = detail::modelFromShape(compound, "step", {}, {});
    ModelTransform turn; turn.kind = ModelTransformKind::Rotate; turn.axis = RotationAxis::Y; turn.angleDegrees = 37;
    auto moved = transformModelParts(*original, {"imported-part-1"}, turn);
    ModelTransform move; move.offsetMm = {1e6, -2e6, 3e6};
    moved = transformModelParts(*moved, {"imported-part-1"}, move);
    LocalReconstruction box{{"imported-part-1"}}; box.type = "box";
    box.cornerMm = 0; box.toleranceMm = 0; // 与盒体无关的轮廓参数不应阻止执行。
    const auto completed = reconstructLocalParts(*moved, {}, box);
    expectOccupancyBox(moved->mesh(), completed->mesh(), completed->summary(), box.partIds.front());
    expectSamePart(moved->mesh(), completed->mesh(), "imported-part-2");
    EXPECT_NEAR(completed->summary().volumeMm3, 20 * 20 * 30 + 1000, 1e-3);
    EXPECT_TRUE(moved->summary().obstacleParts.empty());
    const auto again = reconstructLocalParts(*completed, {}, box);
    EXPECT_NEAR(again->summary().volumeMm3, completed->summary().volumeMm3, 1e-3);
    EXPECT_THROW((void)connectedModelParts(*completed, box.partIds.front()), GeometryError);
    EXPECT_THROW((void)reconstructLocalParts(*completed, {}, {{box.partIds.front()}}), GeometryError);
    std::stop_source cancel; cancel.request_stop();
    EXPECT_THROW((void)reconstructLocalParts(*moved, {}, box, cancel.get_token()), GeometryError);
}
TEST(ObstacleReconstruction, LocalBoxPreviewHistoryRecordingAndExchangeWithoutPointCloud)
{
    Files files;
    const auto original = detail::modelFromShape(BRepPrimAPI_MakeCylinder(10, 30).Shape(), "step", {}, {});
    const auto input = files.directory / "cylinder.step", script = files.directory / "box.py";
    (void)exportStep(*original, input);
    CommandService service;
    ASSERT_EQ(finish(service, service.submitImport(input)).state, TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitStartRecording(script)).state, TaskState::Succeeded);
    const auto before = service.project();
    LocalReconstruction box{{"imported-part-1"}}; box.type = "box";
    const auto previewId = service.submitLocalReconstructionPreview(box, before.revision);
    const auto previewTask = finish(service, previewId);
    ASSERT_EQ(previewTask.state, TaskState::Succeeded) << previewTask.message;
    const auto preview = service.takeEdgePreview(previewId); ASSERT_TRUE(preview);
    EXPECT_EQ(service.project().model, before.model);
    ASSERT_EQ(finish(service, service.submitLocalReconstruction(box, before.revision)).state, TaskState::Succeeded);
    const auto after = service.project();
    expectOccupancyBox(*before.mesh, *after.mesh, *after.model, box.partIds.front());
    EXPECT_NEAR(preview->summary().volumeMm3, after.model->volumeMm3, 1e-6);
    ASSERT_EQ(finish(service, service.submitStopRecording()).state, TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model, before.model);
    ASSERT_EQ(finish(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model, after.model);
    CommandService replay;
    const auto replayTask = finish(replay, replay.submitScriptFile(script));
    ASSERT_EQ(replayTask.state, TaskState::Succeeded) << replayTask.message;
    EXPECT_EQ(toJson(replay.project())["model"]["obstacles"], toJson(after)["model"]["obstacles"]);
    for (const auto &ext : {".step", ".cwrm"})
    {
        const auto output = files.directory / (std::string("boxed") + ext);
        const auto task = finish(service, ext == std::string(".step") ? service.submitExport(output, service.project().revision)
                                                                    : service.submitExportCwrm(output, service.project().revision));
        ASSERT_EQ(task.state, TaskState::Succeeded) << task.message;
        const auto loaded = loadCadModel(output);
        EXPECT_EQ(loaded->summary().solidCount, 1);
        EXPECT_NEAR(loaded->summary().volumeMm3, after.model->volumeMm3, 1e-5);
    }
}
TEST(ObstacleReconstruction, LocalBoxConvertsNativeObstacleWithoutPlateSource)
{
    const auto source = detail::modelFromShape(BRepPrimAPI_MakeBox(120, 20, 60).Shape(), "step", {}, {});
    CloudData cloud; cloud.originMm = {1e6, -2e6, 3e6};
    boxScan(cloud, {150, 40, 0}, {174, 68, 20});
    for (auto &p : cloud.points) p.position.x += .4 * p.position.y;
    PlateRecognition recognition; recognition.scanSpacingMm = 1; recognition.surfaceNoiseMm = .05;
    auto model = reconstructModelObstacles(*source, cloud, recognition);
    model = associateModelCloud(*model, cloud);
    ASSERT_EQ(model->summary().obstacleParts.size(), 1);
    const auto id = model->summary().obstacleParts.begin()->first;
    EXPECT_EQ(std::count(model->summary().reconstructableParts.begin(), model->summary().reconstructableParts.end(), id), 0);
    LocalReconstruction box{{id}}; box.type = "box";
    const auto completed = reconstructLocalParts(*model, {}, box);
    expectOccupancyBox(model->mesh(), completed->mesh(), completed->summary(), id);
    expectSamePart(model->mesh(), completed->mesh(), "imported-part-1");
    EXPECT_EQ(completed->summary().obstacleParts.at(id).supportPoints, model->summary().obstacleParts.at(id).supportPoints);
    EXPECT_GT(completed->summary().volumeMm3, model->summary().volumeMm3);
    EXPECT_FALSE(model->summary().obstacleParts.at(id).boundingBox);
}

TEST(ObstacleReconstruction, OptionalLocalScanRetainsObstacleGeometry)
{
    const auto input = std::getenv("RM_OBSTACLE_SAMPLE_FILE");
    if (!input) GTEST_SKIP() << "Set RM_OBSTACLE_SAMPLE_FILE to validate a local scan.";
    CommandService service;
    ASSERT_EQ(finish(service, service.submitImport(std::filesystem::path(input))).state, TaskState::Succeeded);
    const auto task = finish(service, service.submitRecognizedReconstruct(false, service.project().revision));
    ASSERT_EQ(task.state, TaskState::Succeeded) << task.message;
    auto snapshot = service.project();
    ASSERT_TRUE(snapshot.model);
    const auto filename=std::filesystem::path(input).filename();
    if (filename=="reverse_modeling_result-20260922-08-17-37.pcd")
    {
        EXPECT_TRUE(snapshot.model->obstacleParts.empty());
        EXPECT_EQ(snapshot.model->solidCount,7);
        EXPECT_DOUBLE_EQ(snapshot.model->recognitionOptions.obstacleExclusionDistanceMm,1);
    }
    else EXPECT_FALSE(snapshot.model->obstacleParts.empty());
    // 用户验收样例：两端的四个扫描残差均不应成为障碍物，只保留梁外的两个物体。
    if (std::filesystem::path(input).filename() == "reverse_modeling_result-20260922-08-17-37-1.pcd")
    {
        EXPECT_EQ(snapshot.model->solidCount,9);
        EXPECT_EQ(snapshot.model->plates.size(),7);
        std::vector<std::uint64_t> supports;
        for (const auto &[id,info] : snapshot.model->obstacleParts) supports.push_back(info.supportPoints);
        std::sort(supports.begin(),supports.end());
        EXPECT_EQ(supports,(std::vector<std::uint64_t>{1519,1919}));
    }
    if (const auto selected = std::getenv("RM_OBSTACLE_BOX_PART"))
    {
        ASSERT_TRUE(snapshot.model->obstacleParts.contains(selected));
        LocalReconstruction box{{selected}}; box.type = "box";
        const auto previewId = service.submitLocalReconstructionPreview(box, snapshot.revision);
        const auto previewTask = finish(service, previewId);
        ASSERT_EQ(previewTask.state, TaskState::Succeeded) << previewTask.message;
        const auto preview = service.takeEdgePreview(previewId); ASSERT_TRUE(preview);
        EXPECT_EQ(service.project().model, snapshot.model);
        const auto applyTask = finish(service, service.submitLocalReconstruction(box, snapshot.revision));
        ASSERT_EQ(applyTask.state, TaskState::Succeeded) << applyTask.message;
        const auto after = service.project();
        expectOccupancyBox(*snapshot.mesh, *after.mesh, *after.model, selected);
        for (const auto &id : snapshot.model->partIds)
            if (id != selected) expectSamePart(*snapshot.mesh, *after.mesh, id);
        EXPECT_EQ(snapshot.model->solidCount, after.model->solidCount);
        ASSERT_EQ(finish(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
        EXPECT_EQ(service.project().model, snapshot.model);
        ASSERT_EQ(finish(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
        EXPECT_EQ(service.project().model, after.model);
        snapshot = service.project(); // 撤销/重做后需使用最新工程版本导出。
    }
    if (const auto output = std::getenv("RM_OBSTACLE_ACCEPTANCE_DIR"))
    {
        const auto directory = std::filesystem::path(output);
        std::filesystem::create_directories(directory);
        std::ofstream(directory / "snapshot.json") << toJson(snapshot).dump(2);
        nlohmann::json geometry = nlohmann::json::array();
        for (const auto &part : workbench::makeModelDisplayParts(*snapshot.mesh, *snapshot.model, {}))
        {
            nlohmann::json vertices = nlohmann::json::array();
            for (const auto &v : part.vertices) vertices.push_back({v.x, v.y, v.z});
            geometry.push_back({{"id", part.id}, {"vertices", vertices}, {"triangles", part.triangles}});
        }
        std::ofstream(directory / "geometry.json") << geometry.dump();
        const auto stepExport = finish(service, service.submitExport(directory / "with-obstacles.step", snapshot.revision));
        ASSERT_EQ(stepExport.state, TaskState::Succeeded) << stepExport.errorCode << ": " << stepExport.message;
        const auto cwrmExport = finish(service, service.submitExportCwrm(directory / "with-obstacles.cwrm", snapshot.revision));
        ASSERT_EQ(cwrmExport.state, TaskState::Succeeded) << cwrmExport.errorCode << ": " << cwrmExport.message;
    }
}
} // namespace
