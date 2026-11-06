/* 文件描述：检验真实板厚、中面和孔轮廓保持、批量原子性、历史、录制回放与 STEP 交换。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelImport.h"
#include "workbench/ModelDisplayData.h"

namespace
{
using namespace reverse;
auto plate()
{
    return detail::modelFromShape(BRepPrimAPI_MakeBox(200, 12, 100).Shape(), "step", {}, {});
}
auto display(const Model &model)
{
    return workbench::makeModelDisplayParts(model.mesh(), model.summary(), {});
}
void near(Vec3 a, Vec3 b)
{
    EXPECT_NEAR(a.x, b.x, 1e-6);
    EXPECT_NEAR(a.y, b.y, 1e-6);
    EXPECT_NEAR(a.z, b.z, 1e-6);
}
struct Files
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-thickness-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < deadline)
    {
        const auto task = service.task(id);
        if (task.finished())
        {
            return task;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("thickness task timed out");
}

TEST(PlateThickness, ChangesGeometryAboutMiddlePlaneAndCanChangeAgain)
{
    const auto source = plate();
    const auto id = source->summary().partIds.front();
    const auto edited = changeModelThickness(*source, {{id}, 20});
    const auto part = display(*edited).front();
    near(part.boundsMinMm, {0, -4, 0});
    near(part.boundsMaxMm, {200, 16, 100});
    ASSERT_TRUE(part.thicknessMm);
    EXPECT_DOUBLE_EQ(*part.thicknessMm, 20);
    EXPECT_FALSE(part.thicknessEstimated);
    EXPECT_NEAR(edited->summary().volumeMm3, 400000, 1e-6);
    EXPECT_NEAR(inspectModelThickness(*edited, {id}).at(id).thicknessMm, 20, 1e-6);
    const auto thinner = changeModelThickness(*edited, {{id}, 6});
    near(display(*thinner).front().boundsMinMm, {0, 3, 0});
    near(display(*thinner).front().boundsMaxMm, {200, 9, 100});
    near(display(*source).front().boundsMinMm, {0, 0, 0});
    near(display(*source).front().boundsMaxMm, {200, 12, 100});
    EXPECT_TRUE(source->summary().thicknessOverrides.empty());
}

TEST(PlateThickness, PreservesThroughHoleAndRoundedOutline)
{
    const auto box = BRepPrimAPI_MakeBox(200, 12, 100).Shape();
    const auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, -1, 50), gp_Dir(0, 1, 0)), 10, 14).Shape();
    const auto holed = detail::modelFromShape(BRepAlgoAPI_Cut(box, cutter).Shape(), "step", {}, {});
    const auto id = holed->summary().partIds.front();
    std::uint32_t corner = 0;
    for (const auto &edge : holed->mesh().edges)
    {
        const auto a = edge.pointsMm.front(), b = edge.pointsMm.back();
        if (std::abs(a.x) < 1e-6 && std::abs(b.x) < 1e-6 && std::abs(a.z) < 1e-6 && std::abs(b.z) < 1e-6 &&
            std::abs(a.y - b.y) > 11.9)
        {
            corner = edge.topologyIndex;
            break;
        }
    }
    ASSERT_NE(corner, 0);
    const auto rounded = treatModelEdges(*holed, {{{id, {corner}}}, EdgeTreatmentKind::Fillet, 20});
    const auto edited = changeModelThickness(*rounded, {{id}, 25});
    EXPECT_NEAR(edited->summary().volumeMm3, rounded->summary().volumeMm3 * 25 / 12, 1e-4);
    EXPECT_LT(edited->summary().volumeMm3, 200 * 100 * 25);
    const auto restored = changeModelThickness(*edited, {{id}, 12});
    EXPECT_NEAR(restored->summary().volumeMm3, rounded->summary().volumeMm3, 1e-4);
    Files files;
    (void)exportStep(*edited, files.dir / "thick.step");
    const auto reread = loadCadModel(files.dir / "thick.step");
    EXPECT_NEAR(reread->summary().volumeMm3, edited->summary().volumeMm3, 1e-3);
    EXPECT_NEAR(inspectModelThickness(*reread, reread->summary().partIds).begin()->second.thicknessMm, 25, 1e-6);
}

TEST(PlateThickness, BatchChangesKeepUnselectedPartsAndTransformLabels)
{
    const auto source = buildBeam({}, {1000, 200, 300, 8, 12, 10, {400}, false, false});
    const auto edited = changeModelThickness(*source, {{"web", "rib-1-right"}, 20});
    const auto values = inspectModelThickness(*edited, {"web", "rib-1-right", "flange-top"});
    EXPECT_NEAR(values.at("web").thicknessMm, 20, 1e-6);
    EXPECT_NEAR(values.at("rib-1-right").thicknessMm, 20, 1e-6);
    EXPECT_NEAR(values.at("flange-top").thicknessMm, 12, 1e-6);
    ModelTransform rotate;
    rotate.kind = ModelTransformKind::Rotate;
    rotate.axis = RotationAxis::X;
    rotate.angleDegrees = 37;
    rotate.centerMm = {10, 20, 30};
    const auto rotated = transformModelParts(*edited, {"web"}, rotate);
    const auto &info = rotated->summary().thicknessOverrides.at("web");
    near(info.centerMm, transformPoint(modelTransformFrame(rotate), values.at("web").centerMm));
    near(info.normal, transformVector(modelTransformFrame(rotate), values.at("web").normal));
    const auto resized = changeModelThickness(*rotated, {{"web"}, 16});
    EXPECT_NEAR(inspectModelThickness(*resized, {"web"}).at("web").thicknessMm, 16, 1e-6);
    const auto parts = display(*resized);
    const auto &web = *std::find_if(parts.begin(), parts.end(), [](const auto &p) { return p.id == "web"; });
    EXPECT_NEAR(std::hypot(web.labelFaces[0].position.x - web.labelFaces[1].position.x,
                           web.labelFaces[0].position.y - web.labelFaces[1].position.y,
                           web.labelFaces[0].position.z - web.labelFaces[1].position.z),
                16, 1e-6);
    const auto removed = deleteModelParts(*resized, {"web"});
    EXPECT_FALSE(removed->summary().thicknessOverrides.contains("web"));
    EXPECT_TRUE(removed->summary().thicknessOverrides.contains("rib-1-right"));
}

TEST(PlateThickness, RejectsUnsupportedBatchBadArgumentsAndCancellationWithoutChanges)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(200, 12, 100).Shape());
    builder.Add(compound, BRepPrimAPI_MakeSphere(gp_Pnt(300, 0, 0), 10).Shape());
    const auto source = detail::modelFromShape(compound, "step", {}, {});
    const auto volume = source->summary().volumeMm3;
    const auto triangles = source->mesh().triangles;
    EXPECT_THROW((void)changeModelThickness(*source, {source->summary().partIds, 20}), GeometryError);
    const auto flat = plate();
    for (const double t : {0.0, -1.0, 1e7, std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_THROW((void)changeModelThickness(*flat, {flat->summary().partIds, t}), GeometryError);
    }
    EXPECT_THROW((void)changeModelThickness(*flat, {{"missing"}, 20}), GeometryError);
    EXPECT_THROW((void)changeModelThickness(*flat, {{}, 20}), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)changeModelThickness(*flat, {flat->summary().partIds, 20}, stop.get_token()), GeometryError);
    EXPECT_EQ(source->mesh().triangles, triangles);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, volume);
    EXPECT_TRUE(source->summary().thicknessOverrides.empty());
}

TEST(PlateThicknessCommands, PreviewApplyHistoryRecordingAndReplay)
{
    Files files;
    (void)exportStep(*plate(), files.dir / "plate.step");
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(files.dir / "plate.step")).state,
              TaskState::Succeeded);
    const auto before = service.project();
    const PlateThicknessChange request{before.model->partIds, 20};
    const auto inspection = wait(service, service.submitThicknessInspection(request.partIds, before.revision));
    ASSERT_EQ(inspection.state, TaskState::Succeeded) << inspection.message;
    EXPECT_NEAR(inspection.plateThicknesses.begin()->second.thicknessMm, 12, 1e-6);
    const auto preview = wait(service, service.submitThicknessPreview(request, before.revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    EXPECT_EQ(service.project().revision, before.revision);
    ASSERT_TRUE(service.takeEdgePreview(preview.id));
    ASSERT_EQ(wait(service, service.submitStartRecording(files.dir / "thickness.py")).state, TaskState::Succeeded);
    const auto applied = wait(service, service.submitThickness(request, before.revision));
    ASSERT_EQ(applied.state, TaskState::Succeeded) << applied.message;
    EXPECT_NEAR(service.project().model->volumeMm3, 400000, 1e-4);
    EXPECT_NE(applied.message.find("板厚已修改"), std::string::npos);
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_TRUE(service.project().model->thicknessOverrides.empty());
    EXPECT_NEAR(service.project().model->volumeMm3, 240000, 1e-4);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, 400000, 1e-4);
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream input(files.dir / "thickness.py");
    const std::string script((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(script.find("model.set_thickness"), std::string::npos);
    EXPECT_NE(script.find("--thickness 20"), std::string::npos);
    EXPECT_EQ(script.find("ThicknessPreview"), std::string::npos);
    EXPECT_EQ(script.find("ThicknessInspection"), std::string::npos);
    // 中途开始录制也需要补写修改板厚以及撤销/重做。
    const auto late = wait(service, service.submitStartRecording(files.dir / "late.py"));
    ASSERT_EQ(late.state, TaskState::Succeeded) << late.message;
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService replay;
    const auto replayed = wait(replay, replay.submitScriptFile(files.dir / "late.py"));
    ASSERT_EQ(replayed.state, TaskState::Succeeded) << replayed.message;
    ASSERT_TRUE(replay.project().model);
    EXPECT_NEAR(replay.project().model->volumeMm3, 400000, 1e-4);
    const auto revision = service.project().revision;
    EXPECT_EQ(wait(service, service.submitThickness(request, before.revision)).state, TaskState::Failed);
    EXPECT_EQ(
        wait(service, service.submitLine("model.set_thickness --parts '[\"imported-part-1\"]' --thickness -1")).state,
        TaskState::Failed);
    EXPECT_EQ(service.project().revision, revision);
}
} // namespace
