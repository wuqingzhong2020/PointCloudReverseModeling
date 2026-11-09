/* 文件描述：验证交叉板切分、真实高度移动、底板约束、异常原子性和历史重放。 */
#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/PlateEditing.h"
#include "geometry/PlateThickness.h"
#include "workbench/ModelDisplayData.h"
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace
{
using namespace reverse;
auto a()
{
    return BRepPrimAPI_MakeBox(gp_Pnt(-200, -6, 12), 400, 12, 140).Shape();
}
auto b(double height = 140)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(-6, -180, 12), 12, 360, height).Shape();
}
auto base()
{
    return BRepPrimAPI_MakeBox(gp_Pnt(-240, -220, 0), 480, 440, 12).Shape();
}
auto model(std::initializer_list<TopoDS_Shape> shapes)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto &shape : shapes)
        builder.Add(compound, shape);
    return detail::modelFromShape(compound, "step", {}, {});
}
PlateEdit request(double h = 10)
{
    PlateEdit r;
    r.operation = "cross";
    r.target = "imported-part-1";
    r.parts = {"imported-part-2"};
    r.heightOffset = h;
    return r;
}
auto display(const Model &m)
{
    return workbench::makeModelDisplayParts(m.mesh(), m.summary(), {});
}
TaskSnapshot wait(CommandService &service, const std::string &id)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < end)
    {
        auto t = service.task(id);
        if (t.finished())
            return t;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("task timeout");
}
struct Files
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-cross-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Files()
    {
        std::filesystem::create_directory(dir);
    }
    ~Files()
    {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};
TEST(CrossPlate, SplitsOnlyBWithStableSourcesThicknessAndUnchangedA)
{
    const auto original = model({a(), b(), base()});
    const auto result = editModelPlates(*original, request(0));
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(result.model->summary().solidCount, 4);
    const auto parts = display(*result.model);
    EXPECT_EQ(parts[0].id, "imported-part-1");
    EXPECT_EQ(parts[3].id, "imported-part-3");
    EXPECT_NEAR(parts[0].boundsMaxMm.z, 152, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.y, -6, 1e-6);
    EXPECT_NEAR(parts[2].boundsMinMm.y, 6, 1e-6);
    EXPECT_NEAR(parts[1].boundsMinMm.z, 12, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.z, 152, 1e-6);
    EXPECT_NEAR(result.model->summary().volumeMm3, original->summary().volumeMm3 - 12 * 12 * 140, 1e-4);
    EXPECT_EQ(original->summary().solidCount, 3);
    EXPECT_TRUE(original->summary().editSources.empty());
    for (const auto &id : {parts[1].id, parts[2].id})
    {
        EXPECT_EQ(result.model->summary().editSources.at(id), std::vector<std::string>{"imported-part-2"});
        EXPECT_NEAR(inspectModelThickness(*result.model, {id}).at(id).thicknessMm, 12, 1e-6);
    }
}
TEST(CrossPlate, HeightMovesSegmentsAndDoesNotJustTrimTheirTops)
{
    const auto source = model({a(), b()});
    const auto result = editModelPlates(*source, request());
    const auto parts = display(*result.model);
    EXPECT_NEAR(parts[1].boundsMinMm.z, 2, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.z, 142, 1e-6);
    EXPECT_NEAR(result.model->summary().volumeMm3, source->summary().volumeMm3 - 12 * 12 * 140, 1e-4);
    EXPECT_NEAR(parts[0].boundsMaxMm.z, 152, 1e-6);
}
TEST(CrossPlate, SupportExtendsRaisedSegmentsAndClipsBelowWebBottom)
{
    const auto raised = editModelPlates(*model({a(), b(110), base()}), request());
    auto parts = display(*raised.model);
    EXPECT_NEAR(parts[1].boundsMaxMm.z, 142, 1e-6);
    EXPECT_NEAR(parts[1].boundsMinMm.z, 12, 1e-6);
    const auto lowered = editModelPlates(*model({a(), b(160), base()}), request());
    parts = display(*lowered.model);
    EXPECT_NEAR(parts[1].boundsMaxMm.z, 142, 1e-6);
    EXPECT_NEAR(parts[1].boundsMinMm.z, 0, 1e-6);
}
TEST(CrossPlate, RotationAndExistingHolesSurviveSplitting)
{
    const auto hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-20, -90, 80), gp_Dir(1, 0, 0)), 15, 40).Shape();
    BRepAlgoAPI_Cut cut(b(), hole);
    ASSERT_TRUE(cut.IsDone());
    const auto source = model({a(), cut.Shape()});
    const auto baseline = editModelPlates(*source, request(0));
    EXPECT_NEAR(baseline.model->summary().volumeMm3, source->summary().volumeMm3 - 12 * 12 * 140, 1e-3);
    gp_Trsf yaw;
    yaw.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), .47);
    const auto rotated = model(
        {BRepBuilderAPI_Transform(a(), yaw, true).Shape(), BRepBuilderAPI_Transform(cut.Shape(), yaw, true).Shape()});
    const auto result = editModelPlates(*rotated, request(0));
    EXPECT_EQ(result.model->summary().solidCount, 3);
    EXPECT_NEAR(result.model->summary().volumeMm3, baseline.model->summary().volumeMm3, 1e-3);
}
TEST(CrossPlate, RejectsInvalidSelectionParametersGeometryAndCancellation)
{
    const auto source = model({a(), b(), base()});
    for (double h : {-1.0, 10.1, std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW((void)editModelPlates(*source, request(h)), GeometryError);
    auto r = request();
    r.target = r.parts.front();
    EXPECT_THROW((void)editModelPlates(*source, r), GeometryError);
    r.target = "missing";
    EXPECT_THROW((void)editModelPlates(*source, r), GeometryError);
    r = request();
    r.parts.clear();
    EXPECT_THROW((void)editModelPlates(*source, r), GeometryError);
    r = request();
    r.target = "imported-part-3";
    EXPECT_THROW((void)editModelPlates(*source, r), GeometryError);
    const auto far = model({a(), BRepPrimAPI_MakeBox(gp_Pnt(1000, -180, 12), 12, 360, 140).Shape()});
    EXPECT_THROW((void)editModelPlates(*far, request()), GeometryError);
    const auto thin = model({a(), BRepPrimAPI_MakeBox(gp_Pnt(-4, -180, 12), 8, 360, 140).Shape()});
    EXPECT_THROW((void)editModelPlates(*thin, request()), GeometryError);
    std::stop_source cancelled;
    cancelled.request_stop();
    EXPECT_THROW((void)editModelPlates(*source, request(), cancelled.get_token()), GeometryError);
    EXPECT_EQ(source->summary().solidCount, 3);
}
TEST(CrossPlate, PreviewCommitHistoryReplayAndStepRoundTrip)
{
    Files files;
    const auto input = files.dir / "source.step";
    const auto source = model({a(), b(), base()});
    (void)exportStep(*source, input);
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(input)).state, TaskState::Succeeded);
    const auto revision = service.project().revision;
    const auto preview = wait(service, service.submitPlateEditPreview(request(), revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    const auto result = service.takeEdgePreview(preview.id);
    ASSERT_TRUE(result);
    EXPECT_EQ(service.project().revision, revision);
    const auto committed = wait(service, service.submitPlateEdit(request(), revision));
    ASSERT_EQ(committed.state, TaskState::Succeeded) << committed.message;
    EXPECT_NEAR(service.project().model->volumeMm3, result->summary().volumeMm3, 1e-4);
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 3);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 4);
    EXPECT_EQ(wait(service, service.submitPlateEdit(request(), revision)).state, TaskState::Failed);
    const auto late = files.dir / "late.py";
    ASSERT_EQ(wait(service, service.submitStartRecording(late)).state, TaskState::Succeeded);
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService replay;
    const auto replayed = wait(replay, replay.submitScriptFile(late));
    ASSERT_EQ(replayed.state, TaskState::Succeeded) << replayed.message;
    EXPECT_NEAR(replay.project().model->volumeMm3, result->summary().volumeMm3, 1e-4);
    const auto output = files.dir / "result.step";
    (void)exportStep(*result, output);
    const auto loaded = loadCadModel(output);
    EXPECT_EQ(loaded->summary().solidCount, 4);
    EXPECT_NEAR(loaded->summary().volumeMm3, result->summary().volumeMm3, 1e-3);
}
TEST(CrossPlate, ExportValidationFixtures)
{
    const auto *directory = std::getenv("RM_CROSS_SAMPLE_DIR");
    if (!directory)
        GTEST_SKIP() << "Set RM_CROSS_SAMPLE_DIR to export fixtures";
    const std::filesystem::path dir(directory);
    const auto source = model({a(), b(), base()});
    (void)exportStep(*source, dir / "cross_plate_source.step");
    const auto optimized = editModelPlates(*source, request());
    (void)exportStep(*optimized.model, dir / "cross_plate_optimized.step");
    (void)convertModelFile(dir / "cross_plate_source.step", dir / "cross_plate_surface.pcd", 2);
}
} // namespace
