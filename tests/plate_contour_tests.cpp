/* 文件描述：检验轮廓等距外扩、厚度与内孔保持、旋转与凹形板、批量原子性及命令历史回放。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <numbers>
#include <thread>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
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
        ("rm-contour-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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

TEST(PlateContour, ExpandsAllOuterEdgesWithoutChangingThickness)
{
    const auto source = plate();
    const auto ids = source->summary().partIds;
    const auto ten = expandModelContours(*source, {ids, 10});
    const auto twenty = expandModelContours(*source, {ids, 20});
    near(display(*ten).front().boundsMinMm, {-10, 0, -10});
    near(display(*ten).front().boundsMaxMm, {210, 12, 110});
    near(display(*twenty).front().boundsMinMm, {-20, 0, -20});
    near(display(*twenty).front().boundsMaxMm, {220, 12, 120});
    EXPECT_NEAR(ten->summary().volumeMm3, 220 * 120 * 12, 1e-5);
    EXPECT_NEAR(twenty->summary().volumeMm3, 240 * 140 * 12, 1e-5);
    EXPECT_NEAR(inspectModelThickness(*twenty, ids).begin()->second.thicknessMm, 12, 1e-6);
    const auto again = expandModelContours(*ten, {ids, 20});
    EXPECT_NEAR(again->summary().volumeMm3, 260 * 160 * 12, 1e-5);
    near(display(*source).front().boundsMinMm, {0, 0, 0});
    near(display(*source).front().boundsMaxMm, {200, 12, 100});
    EXPECT_TRUE(source->summary().contourExpandedParts.empty());
    EXPECT_TRUE(ten->summary().thicknessOverrides.empty());
    EXPECT_EQ(again->summary().contourExpandedParts, ids);
}

TEST(PlateContour, PreservesClosedHoleAndStepRoundTrip)
{
    const auto box = BRepPrimAPI_MakeBox(200, 12, 100).Shape();
    const auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, -1, 50), gp_Dir(0, 1, 0)), 10, 14).Shape();
    const auto source = detail::modelFromShape(BRepAlgoAPI_Cut(box, cutter).Shape(), "step", {}, {});
    const auto result = expandModelContours(*source, {source->summary().partIds, 20});
    EXPECT_NEAR(result->summary().volumeMm3, (240 * 140 - std::numbers::pi * 100) * 12, 1e-4);
    Files files;
    (void)exportStep(*result, files.dir / "expanded.step");
    const auto reread = loadCadModel(files.dir / "expanded.step");
    EXPECT_NEAR(reread->summary().volumeMm3, result->summary().volumeMm3, 1e-3);
    EXPECT_NEAR(inspectModelThickness(*reread, reread->summary().partIds).begin()->second.thicknessMm, 12, 1e-6);
}

TEST(PlateContour, HandlesConcaveCircularAndRotatedPlates)
{
    BRepBuilderAPI_MakePolygon wire;
    for (const auto &p : {gp_Pnt(0, 0, 0), gp_Pnt(200, 0, 0), gp_Pnt(200, 0, 40), gp_Pnt(100, 0, 40),
                          gp_Pnt(100, 0, 100), gp_Pnt(0, 0, 100)})
    {
        wire.Add(p);
    }
    wire.Close();
    const auto shape = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0, 12, 0)).Shape();
    const auto source = detail::modelFromShape(shape, "step", {}, {});
    const auto result = expandModelContours(*source, {source->summary().partIds, 10});
    EXPECT_NEAR(result->summary().volumeMm3, (220 * 120 - 100 * 60) * 12, 1e-4);
    const auto round = detail::modelFromShape(BRepPrimAPI_MakeCylinder(50, 12).Shape(), "step", {}, {});
    const auto larger = expandModelContours(*round, {round->summary().partIds, 10});
    EXPECT_NEAR(larger->summary().volumeMm3, std::numbers::pi * 60 * 60 * 12, 1e-4);
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::X;
    rotation.angleDegrees = 37;
    rotation.centerMm = {123, 234, 345};
    const auto moved = transformModelParts(*source, source->summary().partIds, rotation);
    const auto expanded = expandModelContours(*moved, {moved->summary().partIds, 10});
    EXPECT_NEAR(expanded->summary().volumeMm3, result->summary().volumeMm3, 1e-4);
    EXPECT_NEAR(inspectModelThickness(*expanded, expanded->summary().partIds).begin()->second.thicknessMm, 12, 1e-6);
}

TEST(PlateContour, BatchPreservesOtherPartsAndThicknessEdits)
{
    const auto source = buildBeam({}, {1000, 200, 300, 8, 12, 10, {400}, false, false});
    const auto thick = changeModelThickness(*source, {{"rib-1-right"}, 20});
    const auto result = expandModelContours(*thick, {{"web", "rib-1-right", "web"}, 10});
    const auto thickness = inspectModelThickness(*result, {"web", "rib-1-right", "flange-top"});
    EXPECT_NEAR(thickness.at("web").thicknessMm, 8, 1e-6);
    EXPECT_NEAR(thickness.at("rib-1-right").thicknessMm, 20, 1e-6);
    EXPECT_NEAR(thickness.at("flange-top").thicknessMm, 12, 1e-6);
    EXPECT_EQ(result->summary().contourExpandedParts.size(), 2);
    const auto before = display(*source), after = display(*result);
    for (std::size_t i = 0; i < before.size(); ++i)
    {
        if (before[i].id == "flange-top" || before[i].id == "flange-bottom")
        {
            near(before[i].boundsMinMm, after[i].boundsMinMm);
            near(before[i].boundsMaxMm, after[i].boundsMaxMm);
        }
    }
    near(result->summary().thicknessOverrides.at("rib-1-right").centerMm, thickness.at("rib-1-right").centerMm);
    const auto removed = deleteModelParts(*result, {"web"});
    EXPECT_EQ(removed->summary().contourExpandedParts, std::vector<std::string>{"rib-1-right"});
}
TEST(PlateContour, RejectsUnsupportedBatchBadArgumentsAndCancellationWithoutChanges)
{
    BRep_Builder builder;
    TopoDS_Compound compound;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(200, 12, 100).Shape());
    builder.Add(compound, BRepPrimAPI_MakeSphere(gp_Pnt(300, 0, 0), 10).Shape());
    const auto source = detail::modelFromShape(compound, "step", {}, {});
    const auto volume = source->summary().volumeMm3;
    const auto triangles = source->mesh().triangles;
    EXPECT_THROW((void)expandModelContours(*source, {source->summary().partIds, 20}), GeometryError);
    const auto flat = plate();
    for (const double t : {0.0, -1.0, 1e7, std::numeric_limits<double>::quiet_NaN()})
    {
        EXPECT_THROW((void)expandModelContours(*flat, {flat->summary().partIds, t}), GeometryError);
    }
    EXPECT_THROW((void)expandModelContours(*flat, {{"missing"}, 20}), GeometryError);
    EXPECT_THROW((void)expandModelContours(*flat, {{}, 20}), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)expandModelContours(*flat, {flat->summary().partIds, 20}, stop.get_token()), GeometryError);
    EXPECT_EQ(source->mesh().triangles, triangles);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, volume);
    EXPECT_TRUE(source->summary().contourExpandedParts.empty());
}

TEST(PlateContourCommands, PreviewApplyHistoryRecordingAndReplay)
{
    Files files;
    (void)exportStep(*plate(), files.dir / "plate.step");
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(files.dir / "plate.step", LengthUnit::Millimeter)).state,
              TaskState::Succeeded);
    const auto before = service.project();
    const PlateContourExpansion request{before.model->partIds, 20};
    const auto inspection = wait(service, service.submitThicknessInspection(request.partIds, before.revision));
    ASSERT_EQ(inspection.state, TaskState::Succeeded) << inspection.message;
    EXPECT_NEAR(inspection.plateThicknesses.begin()->second.thicknessMm, 12, 1e-6);
    const auto preview = wait(service, service.submitContourExpansionPreview(request, before.revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    EXPECT_EQ(service.project().revision, before.revision);
    ASSERT_TRUE(service.takeEdgePreview(preview.id));
    ASSERT_EQ(wait(service, service.submitStartRecording(files.dir / "contour.py")).state, TaskState::Succeeded);
    const auto applied = wait(service, service.submitContourExpansion(request, before.revision));
    ASSERT_EQ(applied.state, TaskState::Succeeded) << applied.message;
    EXPECT_NEAR(service.project().model->volumeMm3, 403200, 1e-4);
    EXPECT_NE(applied.message.find("板件轮廓已外扩"), std::string::npos);
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_TRUE(service.project().model->contourExpandedParts.empty());
    EXPECT_NEAR(service.project().model->volumeMm3, 240000, 1e-4);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, 403200, 1e-4);
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream input(files.dir / "contour.py");
    const std::string script((std::istreambuf_iterator<char>(input)), {});
    EXPECT_NE(script.find("model.expand_contour"), std::string::npos);
    EXPECT_NE(script.find("--distance 20"), std::string::npos);
    EXPECT_EQ(script.find("ContourExpansionPreview"), std::string::npos);
    EXPECT_EQ(script.find("ThicknessInspection"), std::string::npos);
    // 中途开始录制也需要补写外扩轮廓以及撤销/重做。
    const auto late = wait(service, service.submitStartRecording(files.dir / "late.py"));
    ASSERT_EQ(late.state, TaskState::Succeeded) << late.message;
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService replay;
    const auto replayed = wait(replay, replay.submitScriptFile(files.dir / "late.py"));
    ASSERT_EQ(replayed.state, TaskState::Succeeded) << replayed.message;
    ASSERT_TRUE(replay.project().model);
    EXPECT_NEAR(replay.project().model->volumeMm3, 403200, 1e-4);
    const auto revision = service.project().revision;
    EXPECT_EQ(wait(service, service.submitContourExpansion(request, before.revision)).state, TaskState::Failed);
    EXPECT_EQ(
        wait(service, service.submitLine("model.expand_contour --parts '[\"imported-part-1\"]' --distance -1")).state,
        TaskState::Failed);
    EXPECT_EQ(service.project().revision, revision);
}
} // namespace
