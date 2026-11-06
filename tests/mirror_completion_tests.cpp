/* 文件描述：验证底板中面镜像补全的实体合并、独立复制、板厚与孔洞、原子失败及录制历史。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax1.hxx>
#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelImport.h"
#include "workbench/ModelDisplayData.h"
namespace
{
using namespace reverse;
TopoDS_Shape assembly(bool hole = false)
{
    BRep_Builder builder;
    TopoDS_Compound shape;
    builder.MakeCompound(shape);
    builder.Add(shape, BRepPrimAPI_MakeBox(gp_Pnt(0, 0, 30), 200, 100, 10).Shape());  // 底板中面 Z=35
    builder.Add(shape, BRepPrimAPI_MakeBox(gp_Pnt(0, -12, 30), 200, 12, 80).Shape()); // 跨过中面的一侧翼板
    TopoDS_Shape rib = BRepPrimAPI_MakeBox(gp_Pnt(90, 0, 40), 8, 100, 70).Shape();
    if (hole)
    {
        auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(89, 50, 75), gp_Dir(1, 0, 0)), 10, 10).Shape();
        rib = BRepAlgoAPI_Cut(rib, cutter).Shape();
        // Boolean shape can be a compound containing a single solid; model import decomposes it.
    }
    builder.Add(shape, rib);
    return shape;
}
auto model(const TopoDS_Shape &s)
{
    return detail::modelFromShape(s, "step", {}, {});
}
MirrorCompletion request(std::vector<std::string> parts = {"imported-part-2"})
{
    return {std::move(parts), "imported-part-1"};
}
std::pair<Vec3, Vec3> box(const Model &m, const std::string &id)
{
    Vec3 low{1e12, 1e12, 1e12}, high{-1e12, -1e12, -1e12};
    const auto parts = workbench::makeModelDisplayParts(m.mesh(), m.summary(), {});
    const auto found = std::find_if(parts.begin(), parts.end(), [&](const auto &p) { return p.id == id; });
    if (found == parts.end())
        throw std::runtime_error("missing display part");
    // Read mesh through triangle indices so local per-part display conventions cannot mask errors.
    const auto index = std::find(m.mesh().partIds.begin(), m.mesh().partIds.end(), id) - m.mesh().partIds.begin();
    for (std::size_t i = 0; i < m.mesh().triangles.size(); ++i)
        if (m.mesh().trianglePartIndices[i] == index)
            for (auto v : m.mesh().triangles[i])
            {
                const auto p = m.mesh().vertices[v];
                low.x = std::min(low.x, p.x);
                low.y = std::min(low.y, p.y);
                low.z = std::min(low.z, p.z);
                high.x = std::max(high.x, p.x);
                high.y = std::max(high.y, p.y);
                high.z = std::max(high.z, p.z);
            }
    return {low, high};
}
TEST(MirrorCompletion, TouchingFlangeBecomesOnePlateAroundActualWebMidplane)
{
    const auto source = model(assembly());
    const auto result = completeModelMirror(*source, request());
    EXPECT_EQ(result->summary().solidCount, 3);
    const auto [low, high] = box(*result, "imported-part-2");
    EXPECT_NEAR(low.z, -40, 1e-5);
    EXPECT_NEAR(high.z, 110, 1e-5);
    EXPECT_NEAR(low.y, -12, 1e-5);
    EXPECT_NEAR(high.y, 0, 1e-5);
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, 200 * 12 * 70, 1e-4);
    EXPECT_NEAR(box(*source, "imported-part-2").first.z, 30, 1e-5);
    EXPECT_NEAR(box(*result, "imported-part-1").first.z, 30, 1e-5);
    EXPECT_EQ(result->summary().mirrorCompletedParts, std::vector<std::string>{"imported-part-2"});
    EXPECT_NEAR(inspectModelThickness(*result, {"imported-part-2"}).begin()->second.thicknessMm, 12, 1e-5);
}
TEST(MirrorCompletion, DetachedRibIsCopiedWithHoleAndThickness)
{
    const auto source = model(assembly(true));
    const auto result = completeModelMirror(*source, request({"imported-part-3"}));
    EXPECT_EQ(result->summary().solidCount, 4);
    ASSERT_EQ(result->summary().mirroredParts.size(), 1);
    const auto id = result->summary().mirroredParts.begin()->first;
    EXPECT_NEAR(box(*result, id).first.z, -40, 1e-5);
    EXPECT_NEAR(box(*result, id).second.z, 30, 1e-5);
    EXPECT_NEAR(inspectModelThickness(*result, {id}).begin()->second.thicknessMm, 8, 1e-5);
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, (100 * 70 - std::acos(-1) * 100) * 8, 1e-3);
}
TEST(MirrorCompletion, MultiplePartsAndDuplicateApplication)
{
    const auto source = model(assembly());
    const auto r = request({"imported-part-2", "imported-part-3"});
    const auto result = completeModelMirror(*source, r);
    EXPECT_EQ(result->summary().solidCount, 4);
    try
    {
        (void)completeModelMirror(*result, r);
        FAIL();
    }
    catch (const GeometryError &e)
    {
        EXPECT_EQ(e.code(), "already_symmetric");
    }
}
TEST(MirrorCompletion, ReferenceFollowsRotatedAndTranslatedGeometry)
{
    gp_Trsf rotate;
    rotate.SetRotation(gp_Ax1(gp_Pnt(0, 0, 0), gp_Dir(1, 1, 0)), 0.7);
    rotate.SetTranslationPart(gp_Vec(123, -37, 90));
    const auto source = model(BRepBuilderAPI_Transform(assembly(), rotate, true).Shape());
    const auto result = completeModelMirror(*source, request({"imported-part-2", "imported-part-3"}));
    EXPECT_EQ(result->summary().solidCount, 4);
    EXPECT_NEAR(result->summary().volumeMm3 - source->summary().volumeMm3, 200 * 12 * 70 + 8 * 100 * 70, 1e-3);
}
TEST(MirrorCompletion, ParametricRibKeepsThicknessLabelAndCanBeEdited)
{
    const auto source = buildBeam({}, {1000, 200, 300, 8, 12, 10, {400}, false, false});
    const auto result = completeModelMirror(*source, {{"rib-1-right"}, "web"});
    ASSERT_EQ(result->summary().mirroredParts.size(), 1);
    const auto id = result->summary().mirroredParts.begin()->first;
    const auto parts = workbench::makeModelDisplayParts(result->mesh(), result->summary(), {});
    const auto part = std::find_if(parts.begin(), parts.end(), [&](const auto &p) { return p.id == id; });
    ASSERT_NE(part, parts.end());
    ASSERT_TRUE(part->thicknessMm);
    EXPECT_NEAR(*part->thicknessMm, 10, 1e-6);
    const auto edited = changeModelThickness(*result, {{id}, 14});
    EXPECT_NEAR(inspectModelThickness(*edited, {id}).at(id).thicknessMm, 14, 1e-6);
}
TEST(MirrorCompletion, RecognizedRibKeepsMeasuredThicknessAfterMirroring)
{
    PlateRecognition recognition;
    recognition.plates = {{"web", {-100, -5, -60}, {100, 5, 60}, 1, 10.0, 0, 100, "top_width"},
                          {"rib-1-right", {-4, 5, -60}, {4, 50, 60}, 0, 8.0, 0, 100, "top_width"}};
    const auto source = buildRecognizedPlates(recognition);
    const auto result = completeModelMirror(*source, {{"rib-1-right"}, "web"});
    const auto parts = workbench::makeModelDisplayParts(result->mesh(), result->summary(), {});
    ASSERT_EQ(parts.size(), 3);
    ASSERT_TRUE(parts.back().thicknessMm);
    EXPECT_DOUBLE_EQ(*parts.back().thicknessMm, 8.0);
    EXPECT_FALSE(parts.back().thicknessUnresolved);
    EXPECT_NEAR(parts.back().boundsMaxMm.y, -5, 1e-6);
}
TEST(MirrorCompletion, InvalidSelectionAndCancellationLeaveSourceIntact)
{
    const auto source = model(assembly());
    const auto before = source->summary().volumeMm3;
    EXPECT_THROW((void)completeModelMirror(*source, request({})), GeometryError);
    EXPECT_THROW((void)completeModelMirror(*source, request({"unknown"})), GeometryError);
    EXPECT_THROW((void)completeModelMirror(*source, request({"imported-part-1"})), GeometryError);
    EXPECT_THROW((void)completeModelMirror(*source, {{"imported-part-2"}, "missing"}), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)completeModelMirror(*source, request(), stop.get_token()), GeometryError);
    EXPECT_EQ(source->summary().volumeMm3, before);
}
TEST(MirrorCompletion, PartialOverlapRejectsWholeBatch)
{
    BRep_Builder builder;
    auto s = assembly();
    builder.Add(s, BRepPrimAPI_MakeBox(gp_Pnt(95, 30, -30), 20, 20, 10).Shape());
    const auto source = model(s);
    EXPECT_THROW((void)completeModelMirror(*source, request({"imported-part-2", "imported-part-3"})), GeometryError);
    EXPECT_EQ(source->summary().solidCount, 4);
    EXPECT_NEAR(box(*source, "imported-part-2").first.z, 30, 1e-5);
}
struct MirrorFiles
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-mirror-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    MirrorFiles()
    {
        std::filesystem::create_directory(dir);
    }
    ~MirrorFiles()
    {
        std::error_code e;
        std::filesystem::remove_all(dir, e);
    }
};
TaskSnapshot mirrorWait(CommandService &service, const std::string &id)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (std::chrono::steady_clock::now() < until)
    {
        auto task = service.task(id);
        if (task.finished())
            return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("mirror task timeout");
}
TEST(MirrorCompletion, PreviewRecordingUndoRedoAndStepRoundTrip)
{
    MirrorFiles files;
    const auto input = files.dir / "input.step", script = files.dir / "mirror.py", output = files.dir / "output.step";
    const auto source = model(assembly());
    (void)exportStep(*source, input);
    CommandService service;
    ASSERT_EQ(mirrorWait(service, service.submitImport(input)).state, TaskState::Succeeded);
    const auto before = service.project();
    ASSERT_EQ(mirrorWait(service, service.submitStartRecording(script)).state, TaskState::Succeeded);
    const auto r = request({"imported-part-2", "imported-part-3"});
    const auto previewId = service.submitMirrorCompletionPreview(r, before.revision);
    ASSERT_EQ(mirrorWait(service, previewId).state, TaskState::Succeeded);
    ASSERT_TRUE(service.takeEdgePreview(previewId));
    EXPECT_EQ(service.project().revision, before.revision);
    ASSERT_EQ(mirrorWait(service, service.submitMirrorCompletion(r, before.revision)).state, TaskState::Succeeded);
    const auto after = service.project();
    EXPECT_EQ(after.model->solidCount, 4);
    ASSERT_EQ(mirrorWait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_EQ(service.project().model->solidCount, 3);
    ASSERT_EQ(mirrorWait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, after.model->volumeMm3, 1e-5);
    ASSERT_EQ(mirrorWait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream stream(script);
    std::string text((std::istreambuf_iterator<char>(stream)), {});
    EXPECT_NE(text.find("model.mirror_complete"), std::string::npos);
    EXPECT_NE(text.find("--plane-part imported-part-1"), std::string::npos);
    EXPECT_EQ(text.find("MirrorCompletionPreview"), std::string::npos);
    CommandService replay;
    ASSERT_EQ(mirrorWait(replay, replay.submitScriptFile(script)).state, TaskState::Succeeded);
    EXPECT_NEAR(replay.project().model->volumeMm3, after.model->volumeMm3, 1e-4);
    const auto lateScript = files.dir / "late-recording.py";
    ASSERT_EQ(mirrorWait(service, service.submitStartRecording(lateScript)).state, TaskState::Succeeded);
    ASSERT_EQ(mirrorWait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService lateReplay;
    ASSERT_EQ(mirrorWait(lateReplay, lateReplay.submitScriptFile(lateScript)).state, TaskState::Succeeded);
    EXPECT_NEAR(lateReplay.project().model->volumeMm3, after.model->volumeMm3, 1e-4);
    ASSERT_EQ(mirrorWait(service, service.submitExport(output, service.project().revision)).state,
              TaskState::Succeeded);
    const auto imported = loadCadModel(output);
    EXPECT_EQ(imported->summary().solidCount, 4);
    EXPECT_NEAR(imported->summary().volumeMm3, after.model->volumeMm3, 1e-3);
}
} // namespace
