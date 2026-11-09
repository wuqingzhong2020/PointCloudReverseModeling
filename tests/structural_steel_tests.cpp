/* 文件描述：验证槽钢/角钢的开口、截面、支撑高度、失败原子性、历史重放和交换；可显式生成验证文件。 */
#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/OtherModelRecognition.h"
#include "geometry/PlateThickness.h"
#include "geometry/StructuralSteel.h"
#include "workbench/ModelDisplayData.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace
{
using namespace reverse;
auto blank()
{
    return BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 32), 800, 60, 140).Shape();
}
auto base()
{
    return BRepPrimAPI_MakeBox(gp_Pnt(-60, -130, 0), 920, 320, 12).Shape();
}
auto assembly(const TopoDS_Shape &first, const TopoDS_Shape &second)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, first);
    builder.Add(compound, second);
    return compound;
}
auto model(const TopoDS_Shape &shape)
{
    return detail::modelFromShape(shape, "step", {}, {});
}
std::uint32_t faceIndex(const TopoDS_Shape &shape, const gp_Dir &direction)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(shape, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const auto face = TopoDS::Face(faces(i));
        const BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
            continue;
        auto normal = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
            normal.Reverse();
        if (normal.Dot(direction) > 1 - 1e-6)
            return i;
    }
    throw std::runtime_error("face missing");
}
StructuralSteel request(StructuralSteelKind kind = StructuralSteelKind::Channel)
{
    return {kind, "imported-part-1", faceIndex(blank(), gp_Dir(0, -1, 0)), 10, true, 100};
}
bool inside(const TopoDS_Shape &shape, double x, double y, double z)
{
    return BRepClass3d_SolidClassifier(shape, gp_Pnt(x, y, z), 1e-6).State() == TopAbs_IN;
}
struct Files
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-steel-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files()
    {
        std::filesystem::create_directory(dir);
    }
    ~Files()
    {
        std::error_code error;
        std::filesystem::remove_all(dir, error);
    }
};
TaskSnapshot wait(CommandService &service, const std::string &id)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < end)
    {
        const auto task = service.task(id);
        if (task.finished())
            return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("steel command timeout");
}
std::shared_ptr<CloudData> topScan()
{
    auto cloud = std::make_shared<CloudData>();
    for (int x = -60; x <= 860; x += 2)
    {
        for (int y = -130; y <= 190; y += 2)
        {
            const bool top = x >= 0 && x <= 800 && y >= 0 && y <= 60;
            const double noise = 0.02 * std::sin(x * 0.7 + y * 0.3);
            cloud->points.push_back({{double(x), double(y), (top ? 172.0 : 12.0) + noise}, 0, cloud->points.size()});
        }
    }
    cloud->sourcePointCount = cloud->points.size();
    cloud->boundsMinMm = {-60, -130, 11.98};
    cloud->boundsMaxMm = {860, 190, 172.02};
    cloud->fields = {"x", "y", "z"};
    return cloud;
}

TEST(StructuralSteel, ChannelAndAngleUseSelectedOpeningAndPreserveTopFlange)
{
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        const auto s = detail::structuralSteelSection(blank(), request(kind), {});
        const auto result = detail::buildSteelSection(s);
        EXPECT_TRUE(inside(result, 400, 55, 100));
        EXPECT_TRUE(inside(result, 400, 15, 167));
        EXPECT_FALSE(inside(result, 400, 15, 100));
        EXPECT_EQ(inside(result, 400, 15, 37), kind == StructuralSteelKind::Channel);
        auto opposite = request(kind);
        opposite.faceIndex = faceIndex(blank(), gp_Dir(0, 1, 0));
        const auto reverse = detail::buildSteelSection(detail::structuralSteelSection(blank(), opposite, {}));
        EXPECT_TRUE(inside(reverse, 400, 5, 100));
        EXPECT_FALSE(inside(reverse, 400, 55, 100));
        const auto m = model(result);
        const double area = kind == StructuralSteelKind::Channel ? 2 * 60 * 10 + 120 * 10 : 60 * 10 + 130 * 10;
        EXPECT_NEAR(m->summary().volumeMm3, 800 * area, 1e-4);
        EXPECT_NEAR(inspectModelThickness(*m, {"imported-part-1"}).at("imported-part-1").thicknessMm, 10, 1e-6);
    }
}
TEST(StructuralSteel, AutoSupportHeightCustomHeightAndStablePartIds)
{
    const auto source = model(assembly(blank(), base()));
    const auto before = workbench::makeModelDisplayParts(source->mesh(), source->summary(), {});
    auto r = request();
    const auto result = setModelStructuralSteel(*source, r);
    const auto parts = workbench::makeModelDisplayParts(result->mesh(), result->summary(), {});
    EXPECT_EQ(result->summary().partIds, source->summary().partIds);
    EXPECT_EQ(result->summary().solidCount, 2);
    EXPECT_NEAR(parts[0].boundsMinMm.z, 12, 1e-6);
    EXPECT_NEAR(parts[0].boundsMaxMm.z, 172, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.z, before[1].boundsMaxMm.z, 1e-6);
    EXPECT_NEAR(result->summary().volumeMm3, 800 * (1200 + 1400) + 920 * 320 * 12, 1e-4);
    EXPECT_EQ(source->summary().thicknessOverrides.size(), 0);
    r.autoHeight = false;
    r.heightMm = 100;
    const auto custom = setModelStructuralSteel(*source, r);
    const auto changed = workbench::makeModelDisplayParts(custom->mesh(), custom->summary(), {});
    EXPECT_NEAR(changed[0].boundsMinMm.z, 12, 1e-6);
    EXPECT_NEAR(changed[0].boundsMaxMm.z, 112, 1e-6);
}
TEST(StructuralSteel, IgnoresDistantAboveAndNonoverlappingSupports)
{
    for (const auto support : {BRepPrimAPI_MakeBox(gp_Pnt(-60, -130, -80), 920, 320, 12).Shape(),
                               BRepPrimAPI_MakeBox(gp_Pnt(-60, -130, 200), 920, 320, 12).Shape(),
                               BRepPrimAPI_MakeBox(gp_Pnt(1000, 0, 0), 920, 320, 12).Shape()})
    {
        const auto s = detail::structuralSteelSection(blank(), request(), {support});
        EXPECT_NEAR(s.origin.Z(), 32, 1e-6);
        EXPECT_NEAR(s.height, 140, 1e-6);
    }
}
TEST(StructuralSteel, RejectsWrongFacesCurvesDimensionsAndCancellation)
{
    const auto source = model(blank());
    for (const auto value : {0.0, -1.0, 51.0, std::numeric_limits<double>::quiet_NaN()})
    {
        auto r = request();
        r.thicknessMm = value;
        EXPECT_THROW((void)setModelStructuralSteel(*source, r), GeometryError);
    }
    for (const auto index : {0u, 999u, faceIndex(blank(), gp_Dir(0, 0, 1))})
    {
        auto r = request();
        r.faceIndex = index;
        EXPECT_THROW((void)setModelStructuralSteel(*source, r), GeometryError);
    }
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        auto r = request(kind);
        r.autoHeight = false;
        r.heightMm = kind == StructuralSteelKind::Channel ? 20 : 10;
        EXPECT_THROW((void)setModelStructuralSteel(*source, r), GeometryError);
    }
    const auto cylinder = model(BRepPrimAPI_MakeCylinder(40, 140).Shape());
    EXPECT_THROW((void)setModelStructuralSteel(*cylinder, request()), GeometryError);
    std::stop_source stopped;
    stopped.request_stop();
    EXPECT_THROW((void)setModelStructuralSteel(*source, request(), stopped.get_token()), GeometryError);
    EXPECT_NEAR(source->summary().volumeMm3, 800 * 60 * 140, 1e-4);
}
TEST(StructuralSteel, YawAndTranslationPreserveSectionAndWallThicknessEditing)
{
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        gp_Trsf move;
        move.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), 0.37);
        move.SetTranslationPart(gp_Vec(2300, -1400, 800));
        const auto rotated = BRepBuilderAPI_Transform(blank(), move, true).Shape();
        auto r = request(kind);
        r.faceIndex = faceIndex(rotated, gp_Dir(0, -1, 0).Transformed(move));
        const auto source = model(rotated);
        const auto steel = setModelStructuralSteel(*source, r);
        EXPECT_NEAR(inspectModelThickness(*steel, {r.partId}).at(r.partId).thicknessMm, 10, 1e-6);
        const auto thick = changeModelThickness(*steel, {{r.partId}, 12});
        const double area = kind == StructuralSteelKind::Channel ? 2 * 60 * 12 + 116 * 12 : 60 * 12 + 128 * 12;
        EXPECT_NEAR(thick->summary().volumeMm3, 800 * area, 1e-3);
        EXPECT_NEAR(inspectModelThickness(*thick, {r.partId}).at(r.partId).thicknessMm, 12, 1e-6);
        EXPECT_NEAR(steel->summary().thicknessOverrides.at(r.partId).thicknessMm, 10, 1e-6);
    }
}
TEST(StructuralSteel, PreviewHistoryRecordingReplayAndStepCwrmRoundTrip)
{
    Files files;
    const auto file = files.dir / "source.step";
    (void)exportStep(*model(assembly(blank(), base())), file);
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        const auto suffix = kind == StructuralSteelKind::Channel ? "channel" : "angle";
        CommandService service;
        ASSERT_EQ(wait(service, service.submitImport(file)).state, TaskState::Succeeded);
        const auto revision = service.project().revision;
        const auto r = request(kind);
        const auto preview = wait(service, service.submitStructuralSteelPreview(r, revision));
        ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
        const auto previewModel = service.takeEdgePreview(preview.id);
        ASSERT_TRUE(previewModel);
        EXPECT_EQ(service.project().revision, revision);
        const auto script = files.dir / (std::string(suffix) + ".py");
        ASSERT_EQ(wait(service, service.submitStartRecording(script)).state, TaskState::Succeeded);
        const auto applied = wait(service, service.submitStructuralSteel(r, revision));
        ASSERT_EQ(applied.state, TaskState::Succeeded) << applied.message;
        EXPECT_NEAR(service.project().model->volumeMm3, previewModel->summary().volumeMm3, 1e-4);
        ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
        EXPECT_NEAR(service.project().model->volumeMm3, 800 * 60 * 140 + 920 * 320 * 12, 1e-4);
        ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
        EXPECT_EQ(wait(service, service.submitStructuralSteel(r, revision)).state, TaskState::Failed);
        ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
        const auto lateScript = files.dir / (std::string(suffix) + "-late.py");
        ASSERT_EQ(wait(service, service.submitStartRecording(lateScript)).state, TaskState::Succeeded);
        ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
        CommandService replay;
        const auto done = wait(replay, replay.submitScriptFile(lateScript));
        ASSERT_EQ(done.state, TaskState::Succeeded) << done.message;
        EXPECT_NEAR(replay.project().model->volumeMm3, previewModel->summary().volumeMm3, 1e-4);
        for (const auto extension : {".step", ".cwrm"})
        {
            const auto path = files.dir / (std::string(suffix) + extension);
            if (std::string_view(extension) == ".step")
                (void)exportStep(*previewModel, path);
            else
                (void)exportCwrm(*previewModel, path);
            const auto loaded = loadCadModel(path);
            EXPECT_EQ(loaded->summary().solidCount, 2);
            EXPECT_NEAR(loaded->summary().volumeMm3, previewModel->summary().volumeMm3, 1e-3);
            EXPECT_NEAR(inspectModelThickness(*loaded, {r.partId}).at(r.partId).thicknessMm, 10, 1e-5);
        }
    }
}
TEST(StructuralSteel, SmallTiltPreservesTrueSectionAndRejectsExcessiveTilt)
{
    for (double degrees : {2.0, 7.0})
    {
        gp_Trsf tilt;
        tilt.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(1,0,0)), degrees*std::acos(-1.0)/180.0);
        const auto rotated = BRepBuilderAPI_Transform(blank(), tilt, true).Shape();
        const auto support = BRepBuilderAPI_Transform(base(), tilt, true).Shape();
        auto r = request();
        r.faceIndex = faceIndex(rotated, gp_Dir(0,-1,0).Transformed(tilt));
        if (degrees > 5)
        {
            EXPECT_THROW((void)detail::structuralSteelSection(rotated,r,{support}), GeometryError);
        }
        else
        {
            const auto section = detail::structuralSteelSection(rotated,r,{support});
            EXPECT_NEAR(section.height,160,1e-6);
            EXPECT_NEAR(section.origin.Transformed(tilt.Inverted()).Z(),12,1e-6);
            const auto steel = model(detail::buildSteelSection(section));
            EXPECT_NEAR(steel->summary().volumeMm3,800*2600,1e-4);
        }
    }
}
TEST(StructuralSteel, TopScanCanBeRebuiltAndConvertedToBothSections)
{
    const auto cloud = topScan();
    const auto recognition = recognizeOtherModel(*cloud);
    ASSERT_EQ(recognition.plates.size(), 2);
    PlateBuildOptions options;
    options.type = "other";
    auto source = buildRecognizedPlates(recognition, options);
    source = associateModelCloud(*source, *cloud);
    LocalReconstruction local;
    local.partIds = {recognition.plates.back().id};
    // 俯视识别已根据底板连接关系恢复立板高度，不能用仅有顶面的点再次拟合高度。
    const auto &mesh = source->mesh();
    std::uint32_t opening = 0;
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
    {
        if (mesh.partIds.at(mesh.trianglePartIndices.at(i)) != local.partIds.front())
            continue;
        const auto &triangle = mesh.triangles[i];
        const auto &a = mesh.vertices[triangle[0]], &b = mesh.vertices[triangle[1]], &c = mesh.vertices[triangle[2]];
        const auto normal = gp_Vec(b.x - a.x, b.y - a.y, b.z - a.z).Crossed(gp_Vec(c.x - a.x, c.y - a.y, c.z - a.z));
        if (normal.Magnitude() > 1e-6 && normal.Normalized().Y() < -0.999)
        {
            opening = mesh.triangleFaceIndices[i] + 1;
            break;
        }
    }
    ASSERT_NE(opening, 0);
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        StructuralSteel r{kind, local.partIds.front(), opening, 10, true, 100};
        const auto converted = setModelStructuralSteel(*source, r);
        EXPECT_EQ(converted->summary().solidCount, 2);
        EXPECT_NEAR(inspectModelThickness(*converted, {r.partId}).at(r.partId).thicknessMm, 10, 1e-5);
    }
}
TEST(StructuralSteel, ExportValidationFixtures)
{
    const auto directory = std::getenv("RM_STEEL_SAMPLE_DIR");
    if (!directory)
        GTEST_SKIP() << "Set RM_STEEL_SAMPLE_DIR to generate STEP/PCD validation files.";
    const auto folder = std::filesystem::path(reinterpret_cast<const char8_t *>(directory));
    std::filesystem::create_directories(folder);
    const auto source = model(assembly(blank(), base()));
    (void)exportStep(*source, folder / "structural_steel_source.step");
    (void)exportPointCloud(*topScan(), folder / "structural_steel_topscan.pcd");
    for (const auto kind : {StructuralSteelKind::Channel, StructuralSteelKind::Angle})
    {
        const auto name = kind == StructuralSteelKind::Channel ? "structural_steel_channel" : "structural_steel_angle";
        const auto steel = setModelStructuralSteel(*source, request(kind));
        (void)exportStep(*steel, folder / (std::string(name) + ".step"));
        const auto points = sampleModelSurface(*steel, 2);
        (void)exportPointCloud(*points, folder / (std::string(name) + "_surface.pcd"));
        const auto loaded = loadCadModel(folder / (std::string(name) + ".step"));
        EXPECT_NEAR(loaded->summary().volumeMm3, steel->summary().volumeMm3, 1e-3);
    }
}
} // namespace
