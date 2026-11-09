/* 文件描述：封板双边、自动距离、连接修整、旋转孔洞、错误原子性和回放验证。 */
#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/PlateEditing.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateThickness.h"
#include "workbench/ModelDisplayData.h"
#include <BRepAlgoAPI_Common.hxx>
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
std::vector<TopoDS_Shape> shapes()
{
    return {BRepPrimAPI_MakeBox(gp_Pnt(0, -132, 20), 13, 262, 160).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(0, -130, 0), 1000, 258, 20).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(0, -150, -60), 1000, 18, 240).Shape(),
            BRepPrimAPI_MakeBox(gp_Pnt(0, 130, -60), 1000, 20, 240).Shape()};
}
auto model(const std::vector<TopoDS_Shape> &parts = shapes())
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    for (const auto &shape : parts)
        builder.Add(compound, shape);
    return detail::modelFromShape(compound, "step", {}, {});
}
PlateEdit request(int edge = 1, bool convex = false, double d = 15, bool automatic = true)
{
    PlateEdit r;
    r.operation = "cap";
    r.parts = {"imported-part-1"};
    r.capEdge = edge;
    r.capConvex = convex;
    r.capDistance = d;
    r.capAutomatic = automatic;
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
        ("rm-cap-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
TEST(EndCapPlate, DetectsBothEdgesAndUsesEachWingThickness)
{
    const auto info = inspectModelEndCap(*model(), "imported-part-1");
    EXPECT_EQ(info.web, "imported-part-2");
    EXPECT_EQ(info.edges[0].wing, "imported-part-3");
    EXPECT_EQ(info.edges[1].wing, "imported-part-4");
    EXPECT_NEAR(info.edges[0].automaticDistance, 13.5, 1e-6);
    EXPECT_NEAR(info.edges[1].automaticDistance, 15, 1e-6);
    EXPECT_NEAR(info.edges[0].position.y, -132, 1e-6);
    EXPECT_NEAR(info.edges[1].position.y, 130, 1e-6);
}
TEST(EndCapPlate, InsetRepairsSelectedConnectionAndPreservesOppositeEnd)
{
    const auto original = model();
    const auto result = editModelPlates(*original, request());
    ASSERT_TRUE(result.changed);
    ASSERT_EQ(result.model->summary().solidCount, 4);
    const auto parts = display(*result.model);
    EXPECT_NEAR(parts[0].boundsMinMm.y, -136.5, 1e-6);
    EXPECT_NEAR(parts[0].boundsMaxMm.y, 130, 1e-6);
    EXPECT_NEAR(parts[1].boundsMinMm.x, 13, 1e-6);
    EXPECT_NEAR(parts[1].boundsMinMm.y, -132, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.y, 128, 1e-6);
    EXPECT_NEAR(parts[2].boundsMinMm.x, 13, 1e-6);
    EXPECT_NEAR(parts[3].boundsMinMm.x, 0, 1e-6);
    EXPECT_NEAR(display(*original)[0].boundsMinMm.y, -132, 1e-6);
    EXPECT_TRUE(original->summary().editSources.empty());
    EXPECT_EQ(result.model->summary().partIds, original->summary().partIds);
    EXPECT_NEAR(inspectModelThickness(*result.model, {"imported-part-1"}).at("imported-part-1").thicknessMm, 13, 1e-6);
    EXPECT_FALSE(editModelPlates(*result.model, request()).changed);
}
TEST(EndCapPlate, IndependentEdgesConvexAndReapplyAreAbsolute)
{
    const auto first = editModelPlates(*model(), request(1, true, 50));
    const auto second = editModelPlates(*first.model, request(2));
    auto parts = display(*second.model);
    EXPECT_NEAR(parts[0].boundsMinMm.y, -200, 1e-6);
    EXPECT_NEAR(parts[0].boundsMaxMm.y, 135, 1e-6);
    EXPECT_NEAR(parts[1].boundsMaxMm.y, 130, 1e-6);
    EXPECT_NEAR(parts[3].boundsMinMm.x, 13, 1e-6);
    const auto change = editModelPlates(*second.model, request(1, false, 10, false));
    parts = display(*change.model);
    EXPECT_NEAR(parts[0].boundsMinMm.y, -140, 1e-6);
    EXPECT_NEAR(parts[0].boundsMaxMm.y, 135, 1e-6);
    const auto repeated = editModelPlates(*change.model, request(1, false, 10, false));
    EXPECT_FALSE(repeated.changed);
    const auto opposite = editModelPlates(*change.model, request(2, true, 40));
    EXPECT_NEAR(display(*opposite.model)[0].boundsMaxMm.y, 190, 1e-6);
}
TEST(EndCapPlate, OppositeLongitudinalEndAndYawKeepSemantics)
{
    auto parts = shapes();
    gp_Trsf move;
    move.SetTranslation(gp_Vec(987, 0, 0));
    parts[0] = BRepBuilderAPI_Transform(parts[0], move, true).Shape();
    const auto baseline = editModelPlates(*model(parts), request(2, true, 25));
    EXPECT_NEAR(display(*baseline.model)[1].boundsMaxMm.x, 987, 1e-6);
    EXPECT_NEAR(display(*baseline.model)[3].boundsMaxMm.x, 987, 1e-6);
    gp_Trsf yaw;
    yaw.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(0, 0, 1)), .47);
    for (auto &part : parts)
        part = BRepBuilderAPI_Transform(part, yaw, true).Shape();
    const auto rotated = editModelPlates(*model(parts), request(2, true, 25));
    EXPECT_NEAR(rotated.model->summary().volumeMm3, baseline.model->summary().volumeMm3, .001);
}
TEST(EndCapPlate, HoleSurvivesAndNearbyUnrelatedBodyIsUnchanged)
{
    auto parts = shapes();
    auto plain = editModelPlates(*model(parts), request(1, true, 50));
    const auto cylinder = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(-1, 0, 100), gp_Dir(1, 0, 0)), 20, 15).Shape();
    BRepAlgoAPI_Cut cut(parts[0], cylinder);
    ASSERT_TRUE(cut.IsDone());
    parts[0] = cut.Shape();
    parts.push_back(BRepPrimAPI_MakeBox(gp_Pnt(400, 0, 20), 40, 30, 25).Shape());
    const auto result = editModelPlates(*model(parts), request(1, true, 50));
    EXPECT_EQ(result.model->summary().solidCount, 5);
    EXPECT_NEAR(result.model->summary().volumeMm3,
                plain.model->summary().volumeMm3 - 3.141592653589793 * 400 * 13 + 40 * 30 * 25, .01);
    EXPECT_FALSE(result.model->summary().editSources.contains("imported-part-5"));
}
TEST(EndCapPlate, ArbitraryRigidPoseKeepsEdgesAndIdenticalGeometry)
{
    std::map<std::string, TopoDS_Shape> original;
    const auto parts = shapes();
    for (std::size_t i = 0; i < parts.size(); ++i)
        original["imported-part-" + std::to_string(i + 1)] = parts[i];
    const auto reference = detail::inspectEndCapShapes(original, "imported-part-1", {});
    for (const double angle : {0.0001, .71, 1.5707963267948966, 3.141592653589793})
    {
        SCOPED_TRACE(angle);
        gp_Trsf pose;
        pose.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(1, 2, -3)), angle);
        pose.SetTranslationPart(gp_Vec(12000, -24000, 7500));
        auto transformed = original;
        for (auto &[id, shape] : transformed)
            shape = BRepBuilderAPI_Transform(shape, pose, true).Shape();
        const auto info = detail::inspectEndCapShapes(transformed, "imported-part-1", {});
        EXPECT_EQ(info.web, reference.web);
        for (int edge = 1; edge <= 2; ++edge)
        {
            EXPECT_EQ(info.edges[edge - 1].wing, reference.edges[edge - 1].wing);
            const auto v = reference.edges[edge - 1].position;
            const auto expected = gp_Pnt(v.x, v.y, v.z).Transformed(pose);
            const auto actual = info.edges[edge - 1].position;
            EXPECT_LT(expected.Distance(gp_Pnt(actual.x, actual.y, actual.z)), 1e-5);
            for (const bool convex : {false, true})
            {
                const auto r = request(edge, convex, 50);
                const auto a = detail::optimizeEndCapShapes(original, r, {});
                const auto b = detail::optimizeEndCapShapes(transformed, r, {});
                ASSERT_EQ(a.replacements.size(), b.replacements.size());
                for (const auto &[id, shapes] : a.replacements)
                {
                    ASSERT_EQ(shapes.size(), 1);
                    const auto expectedShape = BRepBuilderAPI_Transform(shapes.front(), pose, true).Shape();
                    const auto &actualShape = b.replacements.at(id).front();
                    BRepAlgoAPI_Common common(expectedShape, actualShape);
                    ASSERT_TRUE(common.IsDone());
                    const double volume = detail::plateVolume(common.Shape());
                    EXPECT_NEAR(volume, detail::plateVolume(expectedShape), .01);
                    EXPECT_NEAR(volume, detail::plateVolume(actualShape), .01);
                }
            }
        }
    }
}
TEST(EndCapPlate, HBeamEndRibsWorkOnBothWebSidesAndBothBeamEnds)
{
    const BeamParameters parameters{1000, 200, 300, 20, 18, 13, {6.5, 500, 993.5}, false, true};
    for (const double angle : {0., .83})
    {
        gp_Trsf pose;
        pose.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(2, -3, 1)), angle);
        const auto vec = [&pose](gp_Dir d)
        {
            d.Transform(pose);
            return Vec3{d.X(), d.Y(), d.Z()};
        };
        const BeamFrame frame{{1030, -5020, 790}, vec(gp_Dir(1, 0, 0)), vec(gp_Dir(0, 1, 0)), vec(gp_Dir(0, 0, 1))};
        const auto source = buildBeam(frame, parameters);
        for (const auto *id : {"rib-1-left", "rib-1-right", "rib-3-left", "rib-3-right"})
        {
            SCOPED_TRACE(id);
            const auto info = inspectModelEndCap(*source, id);
            EXPECT_EQ(info.web, "web");
            EXPECT_EQ(info.edges[0].wing, "flange-bottom");
            EXPECT_EQ(info.edges[1].wing, "flange-top");
            auto current = source;
            for (int edge = 1; edge <= 2; ++edge)
            {
                auto r = request(edge, edge == 2, 50);
                r.parts = {id};
                const auto edit = editModelPlates(*current, r);
                ASSERT_TRUE(edit.changed);
                EXPECT_EQ(edit.model->summary().solidCount, source->summary().solidCount);
                EXPECT_NEAR(inspectModelThickness(*edit.model, {id}).at(id).thicknessMm, 13, 1e-5);
                EXPECT_FALSE(editModelPlates(*edit.model, r).changed);
                current = edit.model;
            }
        }
        EXPECT_THROW((void)inspectModelEndCap(*source, "rib-2-right"), GeometryError);
    }
}
TEST(EndCapPlate, RealScanEndRibs)
{
    const auto *input = std::getenv("RM_CAP_SCAN");
    if (!input)
        GTEST_SKIP() << "Set RM_CAP_SCAN to verify a real scan";
    const auto cloud = loadPointCloud(input, LengthUnit::Millimeter);
    const auto recognition = recognizePlates(*cloud, estimateBeam(*cloud));
    const auto source = buildRecognizedPlates(recognition);
    std::vector<std::string> ribs;
    for (const auto &id : source->summary().partIds)
        if (id.starts_with("rib-"))
            ribs.push_back(id);
    ASSERT_GE(ribs.size(), 2);
    for (const auto &id : {ribs.front(), ribs.back()})
    {
        SCOPED_TRACE(id);
        const auto info = inspectModelEndCap(*source, id);
        EXPECT_EQ(info.web, "web");
        for (int edge = 1; edge <= 2; ++edge)
        {
            auto r = request(edge, edge == 2, 50);
            r.parts = {id};
            const auto edited = editModelPlates(*source, r);
            EXPECT_TRUE(edited.changed);
            EXPECT_EQ(edited.model->summary().solidCount, source->summary().solidCount);
            EXPECT_NO_THROW((void)workbench::makeModelDisplayParts(
                edited.model->mesh(), edited.model->summary(), cloud->originMm));
        }
    }
}
TEST(EndCapPlate, RejectsInvalidSelectionParametersAndMissingWings)
{
    const auto source = model();
    for (int edge : {0, 3})
        EXPECT_THROW((void)editModelPlates(*source, request(edge)), GeometryError);
    for (double d : {0., -1., 101., std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()})
        EXPECT_THROW((void)editModelPlates(*source, request(1, false, d, false)), GeometryError);
    EXPECT_THROW((void)editModelPlates(*source, request(1, true, 201)), GeometryError);
    for (const auto &id : {"missing", "imported-part-2", "imported-part-3"})
        EXPECT_THROW((void)inspectModelEndCap(*source, id), GeometryError);
    auto parts = shapes();
    parts.pop_back();
    EXPECT_THROW((void)editModelPlates(*model(parts), request()), GeometryError);
    parts = shapes();
    gp_Trsf move;
    move.SetTranslation(gp_Vec(500, 0, 0));
    parts[0] = BRepBuilderAPI_Transform(parts[0], move, true).Shape();
    EXPECT_THROW((void)editModelPlates(*model(parts), request()), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)editModelPlates(*source, request(), stop.get_token()), GeometryError);
    EXPECT_TRUE(source->summary().editSources.empty());
}
TEST(EndCapPlate, PreviewApplyUndoReplayAndStepRoundTrip)
{
    Files files;
    const auto file = files.dir / "source.step";
    (void)exportStep(*model(), file);
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(file)).state, TaskState::Succeeded);
    const auto original = service.project();
    // 误选腹板在任务边界转成失败结果，模型保持不变，随后仍可识别真正的封板。
    const auto rejected = wait(service, service.submitEndCapInspection("imported-part-2", original.revision));
    EXPECT_EQ(rejected.state, TaskState::Failed);
    EXPECT_EQ(rejected.errorCode, "cap_invalid_connections");
    EXPECT_FALSE(rejected.result.committed);
    EXPECT_FALSE(rejected.endCap.has_value());
    EXPECT_EQ(service.project().revision, original.revision);
    EXPECT_EQ(service.project().model->volumeMm3, original.model->volumeMm3);
    const auto inspection = wait(service, service.submitEndCapInspection("imported-part-1", original.revision));
    ASSERT_EQ(inspection.state, TaskState::Succeeded) << inspection.message;
    ASSERT_TRUE(inspection.endCap);
    const auto id = service.submitPlateEditPreview(request(1, true, 50), original.revision);
    ASSERT_EQ(wait(service, id).state, TaskState::Succeeded);
    auto preview = service.takeEdgePreview(id);
    ASSERT_TRUE(preview);
    EXPECT_EQ(service.project().revision, original.revision);
    const auto applied = wait(service, service.submitPlateEdit(request(1, true, 50), original.revision));
    ASSERT_EQ(applied.state, TaskState::Succeeded) << applied.message;
    ASSERT_TRUE(applied.result.committed);
    EXPECT_NEAR(service.project().model->volumeMm3, preview->summary().volumeMm3, 1e-4);
    const auto macro = files.dir / "history.py";
    ASSERT_TRUE(wait(service, service.submitStartRecording(macro)).result.ok());
    ASSERT_TRUE(wait(service, service.submitStopRecording()).result.ok());
    ASSERT_TRUE(wait(service, service.submitLine("project.undo")).result.ok());
    EXPECT_NEAR(service.project().model->volumeMm3, original.model->volumeMm3, 1e-4);
    ASSERT_TRUE(wait(service, service.submitLine("project.redo")).result.ok());
    CommandService replay;
    const auto replayed = wait(replay, replay.submitScriptFile(macro));
    ASSERT_EQ(replayed.state, TaskState::Succeeded) << replayed.message;
    EXPECT_NEAR(replay.project().model->volumeMm3, preview->summary().volumeMm3, 1e-4);
    const auto roundtrip = files.dir / "roundtrip.step";
    (void)exportStep(*preview, roundtrip);
    EXPECT_NEAR(loadCadModel(roundtrip)->summary().volumeMm3, preview->summary().volumeMm3, 1e-3);
    const auto stale = wait(service, service.submitPlateEdit(request(), original.revision));
    EXPECT_EQ(stale.state, TaskState::Failed);
}
TEST(EndCapPlate, ExportValidationFixtures)
{
    const auto *directory = std::getenv("RM_CAP_SAMPLE_DIR");
    if (!directory)
        GTEST_SKIP() << "Set RM_CAP_SAMPLE_DIR to export fixtures";
    const std::filesystem::path dir(directory);
    const auto source = model();
    (void)exportStep(*source, dir / "end_cap_source.step");
    const auto inset = editModelPlates(*source, request());
    (void)exportStep(*inset.model, dir / "end_cap_misalign.step");
    const auto convex = editModelPlates(*source, request(1, true, 50));
    (void)exportStep(*convex.model, dir / "end_cap_convex.step");
    (void)convertModelFile(dir / "end_cap_source.step", dir / "end_cap_surface.pcd", 2);
}
TEST(EndCapPlate, ExportTiltedHBeamFixtures)
{
    const auto *directory = std::getenv("RM_CAP_POSE_SAMPLE_DIR");
    if (!directory)
        GTEST_SKIP() << "Set RM_CAP_POSE_SAMPLE_DIR to export tilted H-beam fixtures";
    const std::filesystem::path dir(directory);
    gp_Trsf pose;
    pose.SetRotation(gp_Ax1(gp_Pnt(), gp_Dir(1, 2, 3)), .63);
    const auto vec = [&pose](gp_Dir d)
    {
        d.Transform(pose);
        return Vec3{d.X(), d.Y(), d.Z()};
    };
    const BeamFrame frame{{100, -200, 300}, vec(gp_Dir(1, 0, 0)), vec(gp_Dir(0, 1, 0)), vec(gp_Dir(0, 0, 1))};
    const auto source = buildBeam(frame, {1000, 200, 300, 20, 18, 13, {6.5, 993.5}, false, true});
    (void)exportStep(*source, dir / "end_cap_tilted_hbeam.step");
    (void)convertModelFile(dir / "end_cap_tilted_hbeam.step", dir / "end_cap_tilted_hbeam_surface.pcd", 3);
}
} // namespace
