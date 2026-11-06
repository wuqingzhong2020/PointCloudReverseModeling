/* 文件描述：检验开孔尺寸、位置、板厚、失败原子性、交换及命令历史/录制重放。 */
#include "commands/CommandService.h"
#include "geometry/Geometry.h"
#include "geometry/ModelImport.h"
#include "geometry/PlateHoles.h"
#include <BRepAlgoAPI_Common.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <GProp_GProps.hxx>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>
#include <numbers>
#include <thread>
namespace
{
using namespace reverse;
std::shared_ptr<const Model> plate()
{
    return detail::modelFromShape(BRepPrimAPI_MakeBox(200, 12, 100).Shape(), "step", {}, {});
}
PlateHole requestFor(const Model &model)
{
    PlateHole request;
    request.partId = model.summary().partIds[0];
    double longest = 0;
    for (const auto &base : inspectModelHoleBases(model, request.partId))
    {
        const double length = std::hypot(base.firstMm.x - base.secondMm.x, base.firstMm.y - base.secondMm.y,
                                         base.firstMm.z - base.secondMm.z);
        if (std::abs(base.firstMm.z) < 1e-6 && std::abs(base.secondMm.z) < 1e-6 && length > longest)
        {
            request.bottomEdge = base.edge;
            longest = length;
        }
    }
    if (!request.bottomEdge)
        throw std::runtime_error("fixture missing bottom edge");
    return request;
}
void thicknessAndSolids(const Model &model)
{
    double minimum = 1e9, maximum = -1e9;
    for (auto p : model.mesh().vertices)
    {
        minimum = std::min(minimum, p.y);
        maximum = std::max(maximum, p.y);
    }
    EXPECT_NEAR(maximum - minimum, 12, 1e-6);
    EXPECT_EQ(model.summary().solidCount, 1);
}
struct Files
{
    std::filesystem::path dir =
        std::filesystem::temp_directory_path() /
        ("rm-holes-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
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
        auto task = service.task(id);
        if (task.finished())
            return task;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("hole command timeout");
}
} // namespace
TEST(PlateHoles, RoundWeldCutsConcaveQuarterCirclesAndPreservesThickness)
{
    const auto source = plate();
    auto request = requestFor(*source);
    const auto result = addModelHole(*source, request);
    EXPECT_NEAR(source->summary().volumeMm3 - result->summary().volumeMm3, std::numbers::pi * 20 * 20 / 2 * 12, 1e-5);
    thicknessAndSolids(*result);
    EXPECT_TRUE(source->summary().holeEditedParts.empty());
    EXPECT_EQ(result->summary().holeEditedParts, std::vector<std::string>{request.partId});
    request.second = false;
    const auto single = addModelHole(*source, request);
    EXPECT_NEAR(source->summary().volumeMm3 - single->summary().volumeMm3, std::numbers::pi * 20 * 20 / 4 * 12, 1e-5);
}
TEST(PlateHoles, PreviewToolsMatchActualCutForAllHoleShapes)
{
    const auto source = plate();
    auto request = requestFor(*source);
    const auto bases = inspectModelHoleBases(*source, request.partId);
    const auto base = *std::find_if(bases.begin(), bases.end(),
        [&](const auto &candidate) { return candidate.edge == request.bottomEdge; });
    const auto solid = BRepPrimAPI_MakeBox(200, 12, 100).Shape();
    for (const auto kind : {PlateHoleKind::Weld, PlateHoleKind::Bottom})
    {
        request.kind = kind;
        for (const auto shape : {PlateHoleShape::Circle,
             kind == PlateHoleKind::Weld ? PlateHoleShape::Triangle : PlateHoleShape::Rectangle})
        {
            request.shape = shape;
            request.reference = HoleReference::Middle;
            request.offsetMm = 30;
            const auto tools = detail::plateHoleTools(base, request);
            ASSERT_EQ(tools.size(), kind == PlateHoleKind::Weld ? 2u : 1u);
            double removedVolume = 0;
            for (const auto &tool : tools)
            {
                ASSERT_TRUE(BRepCheck_Analyzer(tool).IsValid());
                BRepAlgoAPI_Common overlap(solid, tool);
                ASSERT_TRUE(overlap.IsDone());
                GProp_GProps properties;
                BRepGProp::VolumeProperties(overlap.Shape(), properties);
                removedVolume += properties.Mass();
            }
            const auto applied = addModelHole(*source, request);
            EXPECT_NEAR(removedVolume, source->summary().volumeMm3 - applied->summary().volumeMm3, 1e-5);
            thicknessAndSolids(*applied);
        }
    }
    request = requestFor(*source);
    request.first = false;
    const auto single = detail::plateHoleTools(base, request);
    ASSERT_EQ(single.size(), 1u);
    GProp_GProps properties;
    BRepGProp::VolumeProperties(single.front(), properties);
    const auto center = properties.CentreOfMass();
    EXPECT_NEAR(center.X(), base.secondMm.x + base.thicknessDirection.x * base.thicknessMm / 2, 1e-6);
    EXPECT_NEAR(center.Y(), base.secondMm.y + base.thicknessDirection.y * base.thicknessMm / 2, 1e-6);
    EXPECT_NEAR(center.Z(), base.secondMm.z + base.thicknessDirection.z * base.thicknessMm / 2, 1e-6);
    EXPECT_TRUE(source->summary().holeEditedParts.empty());
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, 200 * 12 * 100);
}
TEST(PlateHoles, TriangularWeldUsesLegSize)
{
    const auto source = plate();
    auto request = requestFor(*source);
    request.shape = PlateHoleShape::Triangle;
    const auto result = addModelHole(*source, request);
    EXPECT_NEAR(source->summary().volumeMm3 - result->summary().volumeMm3, 20 * 20 * 12, 1e-5);
    thicknessAndSolids(*result);
}
TEST(PlateHoles, BottomCircleAndRectangleUseRadiusWidthAndDepth)
{
    const auto source = plate();
    auto request = requestFor(*source);
    request.kind = PlateHoleKind::Bottom;
    request.sizeMm = 15;
    const auto circle = addModelHole(*source, request);
    EXPECT_NEAR(source->summary().volumeMm3 - circle->summary().volumeMm3, std::numbers::pi * 15 * 15 / 2 * 12, 1e-5);
    request.shape = PlateHoleShape::Rectangle;
    const auto rectangle = addModelHole(*source, request);
    EXPECT_NEAR(source->summary().volumeMm3 - rectangle->summary().volumeMm3, 30 * 15 * 12, 1e-5);
    thicknessAndSolids(*circle);
    thicknessAndSolids(*rectangle);
}
TEST(PlateHoles, PositiveEndpointOffsetsPointInwardAndMidpointOffsetsAreSigned)
{
    const auto source = plate();
    auto request = requestFor(*source);
    request.kind = PlateHoleKind::Bottom;
    const auto middle = addModelHole(*source, request);
    for (const auto reference : {HoleReference::Start, HoleReference::End})
    {
        request.reference = reference;
        request.offsetMm = 100;
        EXPECT_NEAR(addModelHole(*source, request)->summary().volumeMm3, middle->summary().volumeMm3, 1e-5);
        request.offsetMm = -1;
        EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    }
    request.reference = HoleReference::Middle;
    request.offsetMm = -40;
    EXPECT_NEAR(addModelHole(*source, request)->summary().volumeMm3, middle->summary().volumeMm3, 1e-5);
}
TEST(PlateHoles, ConsecutiveHolesRetainEarlierCutsAndExport)
{
    Files files;
    const auto source = plate();
    auto request = requestFor(*source);
    const auto first = addModelHole(*source, request);
    request = requestFor(*first);
    request.kind = PlateHoleKind::Bottom;
    request.sizeMm = 15;
    const auto second = addModelHole(*first, request);
    EXPECT_NEAR(first->summary().volumeMm3 - second->summary().volumeMm3, std::numbers::pi * 15 * 15 / 2 * 12, 1e-5);
    (void)exportStep(*second, files.dir / "holes.step");
    const auto restored = loadCadModel(files.dir / "holes.step");
    EXPECT_NEAR(restored->summary().volumeMm3, second->summary().volumeMm3, 1e-4);
    thicknessAndSolids(*restored);
}
TEST(PlateHoles, InvalidInputsAndCancelledCutLeaveSourceUnchanged)
{
    const auto source = plate();
    const auto original = requestFor(*source);
    const auto mesh = source->mesh().triangles;
    for (const double size : {0.0, -1.0, 100.0, 100000.0, std::numeric_limits<double>::quiet_NaN()})
    {
        auto request = original;
        request.sizeMm = size;
        EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    }
    auto request = original;
    request.bottomEdge = 99999;
    EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    request = original;
    request.first = request.second = false;
    EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    request = original;
    request.shape = PlateHoleShape::Rectangle;
    EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    request = original;
    request.partId = "missing";
    EXPECT_THROW((void)addModelHole(*source, request), GeometryError);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW((void)addModelHole(*source, original, stop.get_token()), GeometryError);
    EXPECT_EQ(source->mesh().triangles, mesh);
    EXPECT_DOUBLE_EQ(source->summary().volumeMm3, 200 * 12 * 100);
}
TEST(PlateHoles, ExplicitBottomEdgeWorksAfterRigidRotation)
{
    const auto source = plate();
    auto request = requestFor(*source);
    const auto before = addModelHole(*source, request);
    ModelTransform rotation;
    rotation.kind = ModelTransformKind::Rotate;
    rotation.axis = RotationAxis::X;
    rotation.angleDegrees = 63;
    const auto rotated = transformModelParts(*source, {request.partId}, rotation);
    const auto after = addModelHole(*rotated, request);
    EXPECT_NEAR(after->summary().volumeMm3, before->summary().volumeMm3, 1e-5);
}
TEST(PlateHoleCommands, PreviewIsTransientApplyRecordsUndoRedoAndReplay)
{
    Files files;
    const auto source = plate();
    (void)exportStep(*source, files.dir / "plate.step");
    CommandService service;
    ASSERT_EQ(wait(service, service.submitImport(files.dir / "plate.step")).state,
              TaskState::Succeeded);
    const auto before = service.project();
    const auto inspect = wait(service, service.submitHoleInspection(before.model->partIds[0], before.revision));
    ASSERT_EQ(inspect.state, TaskState::Succeeded);
    EXPECT_EQ(inspect.holeBases.size(), 4);
    // STEP 导入可能重排边号，针对导入后的实体识别测试底边。
    const auto imported = loadCadModel(files.dir / "plate.step");
    auto request = requestFor(*imported);
    const auto preview = wait(service, service.submitHolePreview(request, before.revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    EXPECT_EQ(service.project().revision, before.revision);
    ASSERT_TRUE(service.takeEdgePreview(preview.id));
    ASSERT_EQ(wait(service, service.submitStartRecording(files.dir / "holes.py")).state, TaskState::Succeeded);
    const auto applied = wait(service, service.submitHole(request, before.revision));
    ASSERT_EQ(applied.state, TaskState::Succeeded) << applied.message;
    EXPECT_NE(applied.message.find("孔已应用"), std::string::npos);
    const double editedVolume = service.project().model->volumeMm3;
    ASSERT_EQ(wait(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, before.model->volumeMm3, 1e-5);
    ASSERT_EQ(wait(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, editedVolume, 1e-5);
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    std::ifstream recording(files.dir / "holes.py");
    const std::string contents((std::istreambuf_iterator<char>(recording)), {});
    EXPECT_NE(contents.find("model.add_hole"), std::string::npos);
    EXPECT_NE(contents.find("--edge"), std::string::npos);
    EXPECT_EQ(contents.find("HolePreview"), std::string::npos);
    // 在开孔和撤销/重做之后开始新录制，也必须完整补写当前会话。
    const auto lateRecording = wait(service, service.submitStartRecording(files.dir / "late-holes.py"));
    ASSERT_EQ(lateRecording.state, TaskState::Succeeded) << lateRecording.message;
    ASSERT_EQ(wait(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService lateReplay;
    const auto lateResult = wait(lateReplay, lateReplay.submitScriptFile(files.dir / "late-holes.py"));
    ASSERT_EQ(lateResult.state, TaskState::Succeeded) << lateResult.message;
    ASSERT_TRUE(lateReplay.project().model);
    EXPECT_NEAR(lateReplay.project().model->volumeMm3, editedVolume, 1e-5);
    ASSERT_EQ(wait(service, service.submitImport(files.dir / "plate.step")).state,
              TaskState::Succeeded);
    ASSERT_EQ(wait(service, service.submitScriptFile(files.dir / "holes.py")).state, TaskState::Succeeded);
    EXPECT_NEAR(service.project().model->volumeMm3, editedVolume, 1e-5);
    const auto revision = service.project().revision;
    EXPECT_EQ(wait(service, service.submitHole(request, before.revision)).state, TaskState::Failed);
    EXPECT_EQ(service.project().revision, revision);
}
