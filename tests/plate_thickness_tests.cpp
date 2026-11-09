/* 文件描述：检验真实板厚、中面和孔轮廓保持、批量原子性、历史、录制回放与 STEP 交换。 */
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <thread>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <Geom_BezierCurve.hxx>
#include <NCollection_Array1.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <BRep_Builder.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>
#include <gtest/gtest.h>

#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"
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
void near(Vec3 a, Vec3 b, double tolerance = 1e-6)
{
    EXPECT_NEAR(a.x, b.x, tolerance);
    EXPECT_NEAR(a.y, b.y, tolerance);
    EXPECT_NEAR(a.z, b.z, tolerance);
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
    ASSERT_EQ(source->summary().importedThicknesses.size(), 1);
    const auto importedParts = display(*source);
    ASSERT_TRUE(importedParts[0].thicknessMm);
    EXPECT_NEAR(*importedParts[0].thicknessMm, 12, 1e-6);
    EXPECT_FALSE(importedParts[1].thicknessMm); // 球体不能用包围盒最短边当作板厚。
    const auto withoutPlate = deleteModelParts(*source, {"imported-part-1"});
    EXPECT_TRUE(withoutPlate->summary().importedThicknesses.empty());
    EXPECT_EQ(source->mesh().triangles, triangles);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, volume);
    EXPECT_TRUE(source->summary().thicknessOverrides.empty());
}

TEST(PlateThickness, SplineOutlineAndObliqueSidesKeepContoursAfterThicknessEditAndExchange)
{
    NCollection_Array1<gp_Pnt> poles(1, 4);
    poles(1) = gp_Pnt(0, 0, 100);
    poles(2) = gp_Pnt(70, 0, 112);
    poles(3) = gp_Pnt(130, 0, 87);
    poles(4) = gp_Pnt(200, 0, 100);
    BRepBuilderAPI_MakeWire wire;
    wire.Add(BRepBuilderAPI_MakeEdge(new Geom_BezierCurve(poles)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(poles(4), gp_Pnt(200, 0, 0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(200, 0, 0), gp_Pnt(0, 0, 0)).Edge());
    wire.Add(BRepBuilderAPI_MakeEdge(gp_Pnt(0, 0, 0), poles(1)).Edge());
    const auto prism = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(wire.Wire()).Face(), gp_Vec(0.12, 12, 0.07)).Shape();
    const auto hole = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, -1, 50), gp_Dir(0, 1, 0)), 7, 14).Shape();
    const auto source = detail::modelFromShape(BRepAlgoAPI_Cut(prism, hole).Shape(), "cwrm", {}, {});
    const auto id = source->summary().partIds.front();
    ASSERT_EQ(source->summary().importedThicknesses.size(), 1);
    const auto original = source->summary().importedThicknesses.at(id);
    EXPECT_NEAR(original.thicknessMm, 12, 1e-6);
    const auto edited = changeModelThickness(*source, {{id}, 19});
    ASSERT_EQ(edited->summary().importedThicknesses.size(), 1);
    const auto &info = edited->summary().importedThicknesses.at(id);
    EXPECT_NEAR(info.thicknessMm, 19, 1e-6);
    near(info.centerMm, original.centerMm, 1e-5);
    EXPECT_NEAR(edited->summary().volumeMm3, source->summary().volumeMm3 * 19 / 12, 1e-3);
    // 对每个原拓扑顶点检查法向缩放的位置：轮廓与孔均不随厚度在板面内漂移。
    ASSERT_EQ(source->mesh().keyPoints.size(), edited->mesh().keyPoints.size());
    for (const auto &point : source->mesh().keyPoints)
    {
        const Vec3 expected{point.positionMm.x, 6 + (point.positionMm.y - 6) * 19 / 12, point.positionMm.z};
        EXPECT_TRUE(std::any_of(edited->mesh().keyPoints.begin(), edited->mesh().keyPoints.end(), [&](const auto &candidate) {
            return std::hypot(candidate.positionMm.x - expected.x, candidate.positionMm.y - expected.y,
                              candidate.positionMm.z - expected.z) < 1e-5;
        }));
    }
    const auto restored = changeModelThickness(*edited, {{id}, 12});
    EXPECT_NEAR(restored->summary().volumeMm3, source->summary().volumeMm3, 1e-3);
    Files files;
    (void)exportStep(*edited, files.dir / "spline.step");
    (void)exportCwrm(*edited, files.dir / "spline.cwrm");
    for (const auto &name : {"spline.step", "spline.cwrm"})
    {
        SCOPED_TRACE(name);
        const auto imported = loadCadModel(files.dir / name);
        ASSERT_EQ(imported->summary().importedThicknesses.size(), 1);
        EXPECT_NEAR(imported->summary().importedThicknesses.begin()->second.thicknessMm, 19, 1e-5);
        const auto again = changeModelThickness(*imported, {imported->summary().partIds, 15});
        EXPECT_NEAR(again->summary().volumeMm3, source->summary().volumeMm3 * 15 / 12, 1e-2);
    }
}

TEST(PlateThickness, SlopingMainFaceDoesNotBecomeAnEndToEndThickness)
{
    BRepBuilderAPI_MakePolygon section;
    section.Add(gp_Pnt(0, 0, 0));
    section.Add(gp_Pnt(200, 0, 0));
    section.Add(gp_Pnt(200, 20, 0));
    section.Add(gp_Pnt(0, 12, 0));
    section.Close();
    const auto solid = BRepPrimAPI_MakePrism(BRepBuilderAPI_MakeFace(section.Wire()).Face(), gp_Vec(0, 0, 100)).Shape();
    const auto model = detail::modelFromShape(solid, "step", {}, {});
    EXPECT_TRUE(model->summary().importedThicknesses.empty());
    EXPECT_THROW((void)changeModelThickness(*model, {model->summary().partIds, 25}), GeometryError);
}

TEST(PlateThickness, OptionalLocalPlateSamplesSupportEveryPart)
{
    const auto sampleDirectory = std::getenv("RM_PLATE_SAMPLE_DIRECTORY");
    if (!sampleDirectory) GTEST_SKIP() << "Set RM_PLATE_SAMPLE_DIRECTORY to validate the local plate samples.";
    for (const auto &name : {u8"test1.step", u8"test2.cwrm", u8"test3.cwrm", u8"孔.step"})
    {
        const auto path = std::filesystem::path(sampleDirectory) / std::filesystem::path(name);
        SCOPED_TRACE(path.string());
        const auto model = loadCadModel(path);
        ASSERT_EQ(model->summary().importedThicknesses.size(), model->summary().solidCount);
        for (const auto &[id, info] : model->summary().importedThicknesses)
        {
            SCOPED_TRACE(id);
            const auto edited = changeModelThickness(*model, {{id}, info.thicknessMm + 1});
            ASSERT_EQ(edited->summary().importedThicknesses.size(), model->summary().solidCount);
            EXPECT_NEAR(edited->summary().importedThicknesses.at(id).thicknessMm, info.thicknessMm + 1, 1e-4);
            const auto restored = changeModelThickness(*edited, {{id}, info.thicknessMm});
            EXPECT_NEAR(restored->summary().volumeMm3, model->summary().volumeMm3, 1e-2);
        }
    }
}

TEST(PlateThicknessCommands, ImportedStepAndCwrmLabelsSupportEditUndoRedoAndRoundtrip)
{
    Files files;
    const auto cutter = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(100, -1, 50), gp_Dir(0, 1, 0)), 10, 14).Shape();
    auto source = detail::modelFromShape(BRepAlgoAPI_Cut(BRepPrimAPI_MakeBox(200, 12, 100).Shape(), cutter).Shape(), "step", {}, {});
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::Z;
    rotation.angleDegrees = 31;
    source = transformModelParts(*source, source->summary().partIds, rotation);
    ModelTransform move;
    move.offsetMm = {5000, -700, 300};
    source = transformModelParts(*source, source->summary().partIds, move);
    const auto expected = inspectModelThickness(*source, source->summary().partIds).begin()->second;
    for (const auto &extension : {".step", ".cwrm"})
    {
        SCOPED_TRACE(extension);
        const auto input = files.dir / (std::string("input") + extension);
        if (std::string_view(extension) == ".step") (void)exportStep(*source, input);
        else (void)exportCwrm(*source, input);
        CommandService service;
        ASSERT_EQ(wait(service, service.submitImport(input)).state, TaskState::Succeeded);
        const auto before = service.project();
        const auto id = before.model->partIds.front();
        ASSERT_EQ(before.model->importedThicknesses.size(), 1);
        EXPECT_TRUE(before.model->thicknessOverrides.empty());
        const auto &detected = before.model->importedThicknesses.at(id);
        EXPECT_NEAR(detected.thicknessMm, 12, 1e-6);
        near(detected.centerMm, expected.centerMm);
        const Vec3 origin{4900, -600, 200};
        auto parts = workbench::makeModelDisplayParts(*before.mesh, *before.model, origin);
        ASSERT_TRUE(parts[0].thicknessMm);
        EXPECT_NEAR(*parts[0].thicknessMm, 12, 1e-6);
        for (const auto &face : parts[0].labelFaces)
        {
            const Vec3 offset{face.position.x + origin.x - detected.centerMm.x,
                              face.position.y + origin.y - detected.centerMm.y,
                              face.position.z + origin.z - detected.centerMm.z};
            EXPECT_NEAR(std::hypot(offset.x, offset.y, offset.z), 6, 1e-6);
            EXPECT_NEAR(offset.x * face.normal.x + offset.y * face.normal.y + offset.z * face.normal.z, 6, 1e-6);
        }
        ASSERT_EQ(wait(service, service.submitThickness({{id}, 18.5}, before.revision)).state, TaskState::Succeeded);
        auto edited = service.project();
        EXPECT_NEAR(edited.model->volumeMm3, before.model->volumeMm3 * 18.5 / 12, 1e-4);
        EXPECT_NEAR(edited.model->importedThicknesses.at(id).thicknessMm, 18.5, 1e-6);
        parts = workbench::makeModelDisplayParts(*edited.mesh, *edited.model, {});
        EXPECT_NEAR(parts[0].thicknessMm.value_or(0), 18.5, 1e-6);
        ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
        EXPECT_NEAR(service.project().model->importedThicknesses.at(id).thicknessMm, 12, 1e-6);
        EXPECT_TRUE(service.project().model->thicknessOverrides.empty());
        ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
        EXPECT_NEAR(service.project().model->importedThicknesses.at(id).thicknessMm, 18.5, 1e-6);
        const auto output = files.dir / (std::string("edited") + extension);
        const auto task = std::string_view(extension) == ".step" ? service.submitExport(output, service.project().revision)
                                                               : service.submitExportCwrm(output, service.project().revision);
        ASSERT_EQ(wait(service, task).state, TaskState::Succeeded);
        const auto reread = loadCadModel(output);
        EXPECT_NEAR(reread->summary().importedThicknesses.begin()->second.thicknessMm, 18.5, 1e-6);
        EXPECT_NEAR(reread->summary().volumeMm3, edited.model->volumeMm3, 1e-3);
        EXPECT_TRUE(reread->summary().thicknessOverrides.empty());
    }
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
