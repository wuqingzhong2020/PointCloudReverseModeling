/* 文件描述：验证新增贴板/底板的真实位置、尺寸和独立性，以及适用性、历史、录制和导出。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopExp.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelImport.h"
#include "workbench/ModelDisplayData.h"
namespace
{
using namespace reverse;
auto shape()
{
    return BRepPrimAPI_MakeBox(200, 12, 100).Shape();
}
auto model(const TopoDS_Shape &s)
{
    return detail::modelFromShape(s, "step", {}, {});
}
std::uint32_t faceIndex(const TopoDS_Shape &s, gp_Dir direction)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    for (int i = 1; i <= faces.Extent(); ++i)
    {
        const auto face = TopoDS::Face(faces(i));
        BRepAdaptor_Surface surface(face);
        if (surface.GetType() != GeomAbs_Plane)
        {
            continue;
        }
        auto n = surface.Plane().Axis().Direction();
        if (face.Orientation() == TopAbs_REVERSED)
        {
            n.Reverse();
        }
        if (n.Dot(direction) > 1 - 1e-6)
        {
            return i;
        }
    }
    throw std::runtime_error("missing face");
}
auto displays(const Model &m)
{
    return workbench::makeModelDisplayParts(m.mesh(), m.summary(), {});
}
void near(Vec3 a, Vec3 b)
{
    EXPECT_NEAR(a.x, b.x, 1e-5);
    EXPECT_NEAR(a.y, b.y, 1e-5);
    EXPECT_NEAR(a.z, b.z, 1e-5);
}
PlateGeneration cover(const TopoDS_Shape &s)
{
    return {PlateGenerationKind::Cover, "imported-part-1", faceIndex(s, gp_Dir(0, -1, 0)), 30, 10};
}
PlateGeneration base()
{
    return {PlateGenerationKind::Base, "imported-part-1", 0, 20, 10};
}
PlateGeneration pairBase()
{
    auto r = base();
    r.baseMode = BasePlateMode::VerticalPair;
    r.secondPartId = "imported-part-2";
    return r;
}
TopoDS_Shape pairShape(const TopoDS_Shape &a = shape(),
                      const TopoDS_Shape &b = BRepPrimAPI_MakeBox(gp_Pnt(0, 80, 0), 200, 12, 100).Shape())
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, a);
    builder.Add(compound, b);
    return compound;
}
struct Files
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-generation-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files()
    {
        std::filesystem::create_directory(dir);
    }
    ~Files()
    {
        std::error_code e;
        std::filesystem::remove_all(dir, e);
    }
};
TaskSnapshot wait(CommandService &service, const std::string &id)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(45);
    while (std::chrono::steady_clock::now() < end)
    {
        auto t = service.task(id);
        if (t.finished())
        {
            return t;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("generation task timeout");
}
TEST(PlateGeneration, CoverUsesSelectedMainFaceAndLeavesSourceIntact)
{
    const auto s = shape();
    const auto source = model(s);
    const auto result = generateModelPlate(*source, cover(s));
    const auto parts = displays(*result);
    ASSERT_EQ(parts.size(), 2);
    near(parts[0].boundsMinMm, {0, 0, 0});
    near(parts[0].boundsMaxMm, {200, 12, 100});
    near(parts[1].boundsMinMm, {-30, -10, -30});
    near(parts[1].boundsMaxMm, {230, 0, 130});
    EXPECT_EQ(result->summary().solidCount, 2);
    EXPECT_NEAR(result->summary().volumeMm3, 240000 + 260 * 160 * 10, 1e-5);
    EXPECT_EQ(result->summary().generatedPlates.at(parts[1].id).sourcePartId, "imported-part-1");
    ASSERT_TRUE(parts[1].thicknessMm);
    EXPECT_DOUBLE_EQ(*parts[1].thicknessMm, 10);
    EXPECT_EQ(source->summary().solidCount, 1);
    EXPECT_TRUE(source->summary().generatedPlates.empty());
}
TEST(PlateGeneration, CoverOppositeFaceReverseAndInset)
{
    const auto s = shape();
    const auto source = model(s);
    auto r = cover(s);
    r.faceIndex = faceIndex(s, gp_Dir(0, 1, 0));
    auto result = generateModelPlate(*source, r);
    near(displays(*result).back().boundsMinMm, {-30, 12, -30});
    near(displays(*result).back().boundsMaxMm, {230, 22, 130});
    r.reversed = true;
    r.offsetMm = -5;
    result = generateModelPlate(*source, r);
    near(displays(*result).back().boundsMinMm, {5, 2, 5});
    near(displays(*result).back().boundsMaxMm, {195, 12, 95});
    r.offsetMm = 0;
    EXPECT_NO_THROW((void)generateModelPlate(*source, r));
    r.offsetMm = -100;
    EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
}
TEST(PlateGeneration, CoverDoesNotTransferClosedHoles)
{
    const auto s =
        BRepAlgoAPI_Cut(
            shape(), BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, -1, 50), gp_Dir(0, 1, 0)), 10, 14).Shape())
            .Shape();
    const auto source = model(s);
    const auto result = generateModelPlate(*source, cover(s));
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, 260 * 160 * 10, 1e-4);
}
TEST(PlateGeneration, BaseUsesLowestSupportFaceAndExtendsDown)
{
    const auto source = model(BRepPrimAPI_MakeBox(200, 100, 12).Shape());
    const auto result = generateModelPlate(*source, base());
    const auto parts = displays(*result);
    near(parts.back().boundsMinMm, {-20, -20, -10});
    near(parts.back().boundsMaxMm, {220, 120, 0});
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, 240 * 140 * 10, 1e-5);
    near(parts.front().boundsMaxMm, {200, 100, 12});
}
TEST(PlateGeneration, NearHorizontalAndVerticalBoardsProduceOneWorldHorizontalBase)
{
    for (const bool vertical : {false, true})
    {
        SCOPED_TRACE(vertical);
        for (const double degrees : {-5.0, -0.18, 0.0, 0.18, 5.0})
        {
            SCOPED_TRACE(degrees);
            const double angle = degrees * std::acos(-1.0) / 180;
            const double width = vertical ? 92 : 100;
            gp_Trsf rotation;
            rotation.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0)), angle);
            rotation.SetTranslationPart(gp_Vec(-6000, 1200, -440));
            const auto source = model(BRepBuilderAPI_Transform(
                vertical ? pairShape() : BRepPrimAPI_MakeBox(200, 100, 12).Shape(), rotation, true).Shape());
            const auto original = displays(*source).front();
            const auto result = generateModelPlate(*source, vertical ? pairBase() : base());
            const auto parts = displays(*result);
            ASSERT_EQ(parts.size(), vertical ? 3u : 2u); // 两块竖板也只产生一个新实体。
            EXPECT_EQ(result->summary().solidCount, vertical ? 3 : 2);
            const double lowestZ = -440 + std::min(0.0, width * std::sin(angle));
            near(parts.back().boundsMinMm, {-6020, 1180, lowestZ - 10});
            near(parts.back().boundsMaxMm, {-5780, 1220 + width * std::cos(angle), lowestZ});
            EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3,
                        240 * (width * std::cos(angle) + 40) * 10, 1e-4);
            near(parts.front().boundsMinMm, original.boundsMinMm);
            near(parts.front().boundsMaxMm, original.boundsMaxMm);
            EXPECT_LE(parts.back().boundsMaxMm.z, original.boundsMinMm.z + 1e-5);
            const auto info = inspectModelThickness(*result, {parts.back().id}).at(parts.back().id);
            EXPECT_NEAR(info.thicknessMm, 10, 1e-6);
            EXPECT_NEAR(std::abs(info.normal.z), 1, 1e-6);
        }
    }
}
TEST(PlateGeneration, VerticalPairBridgesGapAndPreservesBothSources)
{
    const auto source = model(pairShape());
    const auto before = displays(*source);
    const auto result = generateModelPlate(*source, pairBase());
    const auto parts = displays(*result);
    ASSERT_EQ(parts.size(), 3u);
    near(parts.back().boundsMinMm, {-20, -20, -10});
    near(parts.back().boundsMaxMm, {220, 112, 0});
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, 240 * 132 * 10, 1e-5);
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        EXPECT_EQ(parts[i].id, before[i].id);
        near(parts[i].boundsMinMm, before[i].boundsMinMm);
        near(parts[i].boundsMaxMm, before[i].boundsMaxMm);
    }
    const auto info = result->summary().generatedPlates.at(parts.back().id);
    EXPECT_EQ(info.sourcePartId, "imported-part-1");
    EXPECT_EQ(info.secondSourcePartId, "imported-part-2");
    EXPECT_EQ(info.baseMode, BasePlateMode::VerticalPair);
    auto swapped = pairBase();
    std::swap(swapped.partId, swapped.secondPartId);
    const auto again = generateModelPlate(*source, swapped);
    EXPECT_NEAR(again->summary().volumeMm3, result->summary().volumeMm3, 1e-5);
    near(displays(*again).back().boundsMinMm, parts.back().boundsMinMm);
    near(displays(*again).back().boundsMaxMm, parts.back().boundsMaxMm);
    EXPECT_EQ(source->summary().solidCount, 2);
    EXPECT_TRUE(source->summary().generatedPlates.empty());
}
TEST(PlateGeneration, ExplicitModeRejectsMissingWrongOrDuplicateSources)
{
    const auto source = model(pairShape());
    for (const auto second : {"", "imported-part-1", "missing"})
    {
        auto r = pairBase();
        r.secondPartId = second;
        EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    }
    EXPECT_THROW((void)generateModelPlate(*source, base()), GeometryError); // 竖板不能冒充水平板。
    auto wrong = pairBase();
    wrong.baseMode = BasePlateMode::Horizontal;
    EXPECT_THROW((void)generateModelPlate(*source, wrong), GeometryError);
    const auto mixed = model(pairShape(BRepPrimAPI_MakeBox(200, 100, 12).Shape()));
    EXPECT_THROW((void)generateModelPlate(*mixed, pairBase()), GeometryError);
    EXPECT_EQ(source->summary().solidCount, 2);
}
TEST(PlateGeneration, VerticalPairRejectsCrossingOverlappingOrDisconnectedSupports)
{
    const auto overlapping = BRepPrimAPI_MakeBox(gp_Pnt(0, 6, 0), 200, 12, 100).Shape();
    const auto disconnected = BRepPrimAPI_MakeBox(gp_Pnt(220, 80, 0), 200, 12, 100).Shape();
    gp_Trsf turn;
    turn.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), 10 * std::acos(-1.0) / 180);
    turn.SetTranslationPart(gp_Vec(0, 80, 0));
    const auto crossed = BRepBuilderAPI_Transform(shape(), turn, true).Shape();
    for (const auto &s : {overlapping, disconnected, crossed})
    {
        const auto source = model(pairShape(shape(), s));
        try
        {
            (void)generateModelPlate(*source, pairBase());
            FAIL() << "invalid pair was accepted";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "unsuitable_plate_pair");
        }
        EXPECT_EQ(source->summary().solidCount, 2);
    }
}
TEST(PlateGeneration, VerticalPairUsesLowestOfDifferentBottomHeights)
{
    const auto high = BRepPrimAPI_MakeBox(gp_Pnt(20, 80, 5), 200, 12, 100).Shape();
    const auto source = model(pairShape(shape(), high));
    const auto result = generateModelPlate(*source, pairBase());
    const auto parts = displays(*result);
    EXPECT_NEAR(parts.back().boundsMinMm.z, -10, 1e-5);
    EXPECT_NEAR(parts.back().boundsMaxMm.z, 0, 1e-5);
    near(parts[1].boundsMinMm, {20, 80, 5}); // 较高源板保持原位，不会被拉到新板上。
    const auto info = inspectModelThickness(*result, {parts.back().id}).at(parts.back().id);
    EXPECT_NEAR(info.thicknessMm, 10, 1e-6);
    EXPECT_NEAR(std::abs(info.normal.z), 1, 1e-6);
}
TEST(PlateGeneration, OutsideFiveDegreesAndSlopedBottomReportActualAngle)
{
    for (const bool vertical : {false, true})
    {
        for (const double degrees : {-30.0, -5.001, 5.001, 30.0})
        {
            SCOPED_TRACE(degrees);
            gp_Trsf rotation;
            rotation.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(1, 0, 0)), degrees * std::acos(-1.0) / 180);
            const auto source = model(BRepBuilderAPI_Transform(
                vertical ? pairShape() : BRepPrimAPI_MakeBox(200, 100, 12).Shape(), rotation, true).Shape());
            try
            {
                (void)generateModelPlate(*source, vertical ? pairBase() : base());
                FAIL() << "outside the horizontal/vertical tolerance";
            }
            catch (const GeometryError &error)
            {
                EXPECT_EQ(error.code(), "unsuitable_base_plate");
                EXPECT_NE(std::string(error.what()).find("5°"), std::string::npos);
                EXPECT_NE(std::string(error.what()).find("旋转"), std::string::npos);
            }
            EXPECT_EQ(source->summary().solidCount, vertical ? 2 : 1);
        }
    }
    // 主面仍竖直，但底部边线沿长度方向倾斜 10°，应明确报告底面而非板面的问题。
    gp_Trsf rotation;
    rotation.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 1, 0)), 10 * std::acos(-1.0) / 180);
    try
    {
        (void)generateModelPlate(*model(BRepBuilderAPI_Transform(pairShape(), rotation, true).Shape()), pairBase());
        FAIL() << "sloped bottom must obey the same tolerance";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "unsuitable_bottom_face");
        EXPECT_NE(std::string(error.what()).find("10.000°"), std::string::npos);
    }
}
TEST(PlateGeneration, BaseKeepsTrueConcaveAndCircularFootprints)
{
    const auto source = model(BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(200, 100, 12).Shape(),
        BRepPrimAPI_MakeBox(gp_Pnt(140, 60, -1), 70, 50, 14).Shape()).Shape());
    const auto result = generateModelPlate(*source, base());
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3,
                (200 * 100 - 60 * 40 + 600 * 20 + 4 * 20 * 20) * 10, 1e-4);
    const auto round = model(BRepPrimAPI_MakeCylinder(50, 12).Shape());
    const auto roundBase = generateModelPlate(*round, base());
    EXPECT_NEAR(roundBase->summary().volumeMm3 - round->summary().volumeMm3,
                std::acos(-1.0) * 70 * 70 * 10, 1e-3);
}
TEST(PlateGeneration, WorldDownCannotBeOverriddenByScriptDirection)
{
    const auto source = model(BRepPrimAPI_MakeBox(200, 100, 12).Shape());
    auto request = base();
    for (const auto direction : {Vec3{0, 0, -1}, Vec3{0, 1, 1}, Vec3{0, 1, 0}})
    {
        request.up = direction;
        try
        {
            (void)generateModelPlate(*source, request);
            FAIL() << "base plate direction is fixed to world -Z";
        }
        catch (const GeometryError &error)
        {
            EXPECT_EQ(error.code(), "invalid_up");
            EXPECT_NE(std::string(error.what()).find("世界 -Z"), std::string::npos);
        }
    }
}
TEST(PlateGeneration, RejectsWrongFaceCurvedOrNotchedSupport)
{
    auto s = shape();
    auto source = model(s);
    auto r = cover(s);
    r.faceIndex = faceIndex(s, gp_Dir(1, 0, 0));
    EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    r.faceIndex = 999;
    EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    EXPECT_THROW((void)generateModelPlate(*model(BRepPrimAPI_MakeSphere(50).Shape()), base()), GeometryError);
    const auto notch = BRepPrimAPI_MakeBox(gp_Pnt(80, -1, -1), 40, 14, 30).Shape();
    EXPECT_THROW((void)generateModelPlate(*model(pairShape(BRepAlgoAPI_Cut(s, notch).Shape())), pairBase()), GeometryError);
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> faces;
    TopExp::MapShapes(s, TopAbs_FACE, faces);
    EXPECT_THROW((void)generateModelPlate(*model(faces(1)), base()), GeometryError);
}
TEST(PlateGeneration, RejectsInvalidParametersAndCancellationWithoutChangingSource)
{
    const auto source = model(BRepPrimAPI_MakeBox(200, 100, 12).Shape());
    const double originalVolume = source->summary().volumeMm3;
    auto r = base();
    for (double d : {0.0, -1.0, 1001.0, std::numeric_limits<double>::quiet_NaN()})
    {
        r.offsetMm = d;
        EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    }
    r = base();
    r.thicknessMm = 51;
    EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    r = base();
    r.offsetMm = 4;
    r.thicknessMm = 50;
    EXPECT_THROW((void)generateModelPlate(*model(BRepPrimAPI_MakeBox(20, 20, 2).Shape()), r), GeometryError);
    r = base();
    r.partId = "missing";
    EXPECT_THROW((void)generateModelPlate(*source, r), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)generateModelPlate(*source, base(), stop.get_token()), GeometryError);
    EXPECT_EQ(source->summary().solidCount, 1);
    EXPECT_EQ(source->summary().volumeMm3, originalVolume);
    EXPECT_NEAR(originalVolume, 240000, 1e-6);
}
TEST(PlateGeneration, StableIdsDeletionThicknessEditingAndStepRoundTrip)
{
    Files files;
    const auto s = BRepPrimAPI_MakeBox(200, 100, 12).Shape();
    const auto source = model(s);
    const auto first = generateModelPlate(*source, base());
    const auto second = generateModelPlate(*first, {PlateGenerationKind::Cover, "imported-part-1",
                                                  faceIndex(s, gp_Dir(0, 0, -1)), 30, 10});
    EXPECT_EQ(second->summary().partIds.back(), "generated-cover-2");
    const auto removed = deleteModelParts(*second, {"generated-base-1"});
    EXPECT_EQ(removed->summary().generatedPlates.size(), 1);
    const auto third = generateModelPlate(*removed, base());
    EXPECT_EQ(third->summary().partIds.back(), "generated-base-3");
    const auto resized = changeModelThickness(*third, {{"generated-base-3"}, 15});
    EXPECT_DOUBLE_EQ(*displays(*resized).back().thicknessMm, 15);
    const auto file = files.dir / std::filesystem::path(u8"附加板.step");
    EXPECT_EQ(exportStep(*resized, file).solidCount, 3);
    const auto loaded = loadCadModel(file);
    EXPECT_EQ(loaded->summary().solidCount, 3);
    EXPECT_NEAR(loaded->summary().volumeMm3, resized->summary().volumeMm3, 1e-3);
}
TEST(PlateGeneration, RecognizedModelsCanDisplayNewPlateAndOriginalThicknesses)
{
    PlateRecognition recognition;
    recognition.frame = {};
    RecognizedPlate p;
    p.id = "web";
    p.minimumMm = {0, 0, 0};
    p.maximumMm = {200, 100, 12};
    p.normalAxis = 2;
    p.thicknessMm = 12;
    recognition.plates.push_back(p);
    const auto source = buildRecognizedPlates(recognition);
    auto r = base();
    r.partId = "web";
    const auto result = generateModelPlate(*source, r);
    const auto parts = displays(*result);
    ASSERT_EQ(parts.size(), 2);
    EXPECT_DOUBLE_EQ(*parts[0].thicknessMm, 12);
    EXPECT_DOUBLE_EQ(*parts[1].thicknessMm, 10);
}
TEST(PlateGeneration, PreviewCommitHistoryAndRecordedReplay)
{
    Files files;
    const auto s = BRepPrimAPI_MakeBox(200, 100, 12).Shape();
    const PlateGeneration coverRequest{PlateGenerationKind::Cover, "imported-part-1",
                                       faceIndex(s, gp_Dir(0, 0, -1)), 30, 10};
    const auto source = model(s);
    const auto file = files.dir / "source.step";
    (void)exportStep(*source, file);
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(file)).state, TaskState::Succeeded);
    const auto initial = service.project();
    const auto p = wait(service, service.submitPlateGenerationPreview(base(), initial.revision));
    ASSERT_EQ(p.state, TaskState::Succeeded) << p.message;
    EXPECT_TRUE(service.takeEdgePreview(p.id));
    EXPECT_EQ(service.project().revision, initial.revision);
    ASSERT_EQ(wait(service, service.submitStartRecording(files.dir / "record.py")).state,
              TaskState::Succeeded);
    auto done = wait(service, service.submitPlateGeneration(base(), initial.revision));
    ASSERT_EQ(done.state, TaskState::Succeeded) << done.message;
    EXPECT_EQ(service.project().model->solidCount, 2);
    const auto volume = service.project().model->volumeMm3;
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 1);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->partIds.back(), "generated-base-1");
    EXPECT_EQ(wait(service, service.submitPlateGeneration(base(), initial.revision)).state,
              TaskState::Failed);
    EXPECT_EQ(
        wait(service, service.submitLine("model.generate_cover_plate --part imported-part-1 --face 999"))
            .state,
        TaskState::Failed);
    EXPECT_EQ(service.project().model->solidCount, 2);
    done = wait(service, service.submitPlateGeneration(coverRequest, service.project().revision));
    ASSERT_EQ(done.state, TaskState::Succeeded) << done.message;
    EXPECT_EQ(service.project().model->solidCount, 3);
    const auto finalVolume = service.project().model->volumeMm3;
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    const auto late = wait(service, service.submitStartRecording(files.dir / "late.py"));
    ASSERT_EQ(late.state, TaskState::Succeeded) << late.message;
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream in(files.dir / "late.py");
    const std::string lines((std::istreambuf_iterator<char>(in)), {});
    EXPECT_NE(lines.find("model.generate_base_plate"), std::string::npos);
    EXPECT_NE(lines.find("--mode horizontal"), std::string::npos);
    EXPECT_NE(lines.find("--up"), std::string::npos);
    EXPECT_NE(lines.find("model.generate_cover_plate"), std::string::npos);
    EXPECT_NE(lines.find("--face"), std::string::npos);
    EXPECT_EQ(lines.find("PlateGenerationPreview"), std::string::npos);
    EXPECT_EQ(lines.find("--face 999"), std::string::npos);
    CommandService replay;
    const auto played = wait(replay, replay.submitScriptFile(files.dir / "late.py"));
    ASSERT_EQ(played.state, TaskState::Succeeded) << played.message;
    EXPECT_EQ(replay.project().model->solidCount, 3);
    EXPECT_NEAR(replay.project().model->volumeMm3, finalVolume, 1e-4);
    EXPECT_GT(finalVolume, volume);
}

TEST(PlateGeneration, InvalidDirectionReportsActionableReasonToConsoleWithoutChangingModel)
{
    Files files;
    const auto source = model(BRepPrimAPI_MakeBox(200, 100, 12).Shape());
    const auto path = files.dir / "source.step";
    (void)exportStep(*source, path);
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(path)).state, TaskState::Succeeded);
    (void)service.takeUiCommandEvents();
    const auto initial = service.project();
    auto request = base();
    request.up = {0, 0, 0};
    const auto task = wait(service, service.submitPlateGenerationPreview(request, initial.revision));
    ASSERT_EQ(task.state, TaskState::Failed);
    EXPECT_EQ(task.errorCode, "invalid_up");
    EXPECT_NE(task.message.find("--up"), std::string::npos);
    const auto events = service.takeUiCommandEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events.front().message, "生成下底板预览（部件 imported-part-1）失败：" + task.message + "\n");
    EXPECT_EQ(service.project().revision, initial.revision);
    EXPECT_EQ(service.project().model, initial.model);
    EXPECT_FALSE(service.takeEdgePreview(task.id));

    // 修正方向后正常生成预览，不残留上一条失败，也不伪装成已应用的命令。
    const auto success = wait(service, service.submitPlateGenerationPreview(base(), initial.revision));
    ASSERT_EQ(success.state, TaskState::Succeeded) << success.message;
    EXPECT_TRUE(service.takeEdgePreview(success.id));
    EXPECT_TRUE(service.takeUiCommandEvents().empty());
    EXPECT_EQ(service.project().model, initial.model);
}
TEST(PlateGeneration, PairPreviewRecordingHistoryAndStepRoundTrip)
{
    Files files;
    const auto sourcePath = files.dir / "two-boards.step";
    (void)exportStep(*model(pairShape()), sourcePath);
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(sourcePath)).state, TaskState::Succeeded);
    const auto revision = service.project().revision;
    auto preview = wait(service, service.submitPlateGenerationPreview(pairBase(), revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    EXPECT_TRUE(service.takeEdgePreview(preview.id));
    EXPECT_EQ(service.project().revision, revision);
    ASSERT_EQ(wait(service, service.submitStartRecording(files.dir / "pair.py")).state, TaskState::Succeeded);
    const auto done = wait(service, service.submitPlateGeneration(pairBase(), revision));
    ASSERT_EQ(done.state, TaskState::Succeeded) << done.message;
    EXPECT_EQ(service.project().model->solidCount, 3);
    const auto volume = service.project().model->volumeMm3;
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 2);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 3);
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream in(files.dir / "pair.py");
    const std::string script((std::istreambuf_iterator<char>(in)), {});
    EXPECT_NE(script.find("--mode vertical_pair"), std::string::npos);
    EXPECT_NE(script.find("--second-part imported-part-2"), std::string::npos);
    CommandService replay;
    const auto played = wait(replay, replay.submitScriptFile(files.dir / "pair.py"));
    ASSERT_EQ(played.state, TaskState::Succeeded) << played.message;
    EXPECT_EQ(replay.project().model->solidCount, 3);
    EXPECT_NEAR(replay.project().model->volumeMm3, volume, 1e-4);
    const auto info = replay.project().model->generatedPlates.at("generated-base-1");
    EXPECT_EQ(info.secondSourcePartId, "imported-part-2");
    EXPECT_EQ(info.baseMode, BasePlateMode::VerticalPair);
    const auto step = files.dir / "result.step";
    const auto exported = wait(replay, replay.submitLine("model.export_step \"" + step.generic_string() + "\""));
    ASSERT_EQ(exported.state, TaskState::Succeeded) << exported.message;
    const auto loaded = loadCadModel(step);
    EXPECT_EQ(loaded->summary().solidCount, 3);
    EXPECT_NEAR(loaded->summary().volumeMm3, volume, 1e-3);
}
} // namespace
