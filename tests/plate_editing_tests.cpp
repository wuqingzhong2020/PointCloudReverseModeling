/* 文件描述：多板编辑的几何边界、原子提交、撤销录制和实际孔洞样本验证。 */
#include "commands/CommandService.h"
#include "geometry/ModelImport.h"
#include "geometry/PlateEditing.h"
#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Builder.hxx>
#include <GProp_GProps.hxx>
#include <STEPControl_Reader.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <numbers>
#include <thread>
namespace
{
using namespace reverse;
TopoDS_Shape box(double x, double y, double z, double a, double b, double c)
{
    return BRepPrimAPI_MakeBox(gp_Pnt(x, y, z), a, b, c).Shape();
}
TopoDS_Shape perforated()
{
    return detail::plateSolids(
               BRepAlgoAPI_Cut(box(0, 0, 0, 100, 60, 6),
                               BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(30, 30, -1), gp_Dir(0, 0, 1)), 8, 8).Shape())
                   .Shape())
        .front();
}
std::shared_ptr<const Model> model(std::initializer_list<TopoDS_Shape> shapes)
{
    BRep_Builder b;
    TopoDS_Compound c;
    b.MakeCompound(c);
    for (const auto &s : shapes)
        b.Add(c, s);
    return detail::modelFromShape(c, "step", {}, {});
}
unsigned xFace(const TopoDS_Shape &s, double x)
{
    unsigned i = 0;
    for (TopExp_Explorer it(s, TopAbs_FACE); it.More(); it.Next())
    {
        ++i;
        BRepAdaptor_Surface a(TopoDS::Face(it.Current()));
        if (a.GetType() == GeomAbs_Plane && std::abs(a.Plane().Axis().Direction().X()) > .999 &&
            std::abs(a.Plane().Location().X() - x) < 1e-5)
            return i;
    }
    return 0;
}
TaskSnapshot done(CommandService &service, const std::string &id)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    while (std::chrono::steady_clock::now() < end)
    {
        auto t = service.task(id);
        if (t.finished())
            return t;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("plate edit timeout");
}
} // namespace
TEST(PlateEditing, ExtendChecksWholeContactAndProtectsTarget)
{
    const auto a = box(0, 0, 0, 100, 40, 6), b = box(110, -10, -10, 6, 60, 30);
    PlateEdit r;
    r.operation = "extend";
    r.parts = {"a"};
    r.target = "b";
    r.contacts = {{"a", xFace(b, 110)}};
    auto result = detail::editPlateShapes({{"a", a}, {"b", b}}, r, {});
    ASSERT_EQ(result.replacements.size(), 1u);
    EXPECT_NEAR(detail::plateVolume(result.replacements.at("a")[0]), 110 * 40 * 6, 1e-5);
    EXPECT_NEAR(detail::plateVolume(a), 100 * 40 * 6, 1e-6);
    const auto partial = box(110, 20, -10, 6, 30, 30);
    r.contacts = {{"a", xFace(partial, 110)}};
    EXPECT_THROW(detail::editPlateShapes({{"a", a}, {"b", partial}}, r, {}), GeometryError);
    r.maxDistance = 5;
    r.contacts = {{"a", xFace(b, 110)}};
    EXPECT_THROW(detail::editPlateShapes({{"a", a}, {"b", b}}, r, {}), GeometryError);
}
TEST(PlateEditing, TrimUsesExplicitSideAndSplitKeepsBothSides)
{
    const auto a = box(0, 0, 0, 100, 40, 6), b = box(45, -10, -10, 10, 60, 30);
    PlateEdit r;
    r.operation = "trim";
    r.parts = {"a"};
    r.target = "b";
    r.contacts = {{"a", xFace(b, 45)}};
    r.cuts = {{"a", {}, {1, 0, 0}, false}};
    const auto result = detail::editPlateShapes({{"a", a}, {"b", b}}, r, {});
    const double volume = detail::plateVolume(result.replacements.at("a")[0]);
    EXPECT_TRUE(std::abs(volume - 45 * 40 * 6) < 1e-5 || std::abs(volume - 55 * 40 * 6) < 1e-5);
    r.operation = "split";
    r.contacts.clear();
    r.cuts.clear();
    auto split = detail::editPlateShapes({{"a", a}, {"b", b}}, r, {});
    ASSERT_EQ(split.replacements.at("a").size(), 2u);
    for (const auto &p : split.replacements.at("a"))
        EXPECT_NEAR(detail::plateVolume(p), 45 * 40 * 6, 1e-5);
    const auto far = box(45, 100, -10, 10, 60, 30);
    EXPECT_THROW(detail::editPlateShapes({{"a", a}, {"b", far}}, r, {}), GeometryError);
}
TEST(PlateEditing, MergeUnifiesThicknessAndUsesOriginalTarget)
{
    auto a = box(0, 0, 0, 100, 40, 4), b = box(102, 0, -1, 80, 40, 6);
    PlateEdit r;
    r.operation = "merge";
    r.parts = {"a"};
    r.target = "b";
    auto result = detail::editPlateShapes({{"a", a}, {"b", b}}, r, {});
    ASSERT_TRUE(result.replacements.at("a").empty());
    EXPECT_NEAR(detail::plateVolume(result.replacements.at("b")[0]), 182 * 40 * 6, 1e-5);
    r.maxGap = 1;
    EXPECT_THROW(detail::editPlateShapes({{"a", a}, {"b", b}}, r, {}), GeometryError);
}
TEST(PlateEditing, RepairPreservesBoundaryAndHonorsAreaLimit)
{
    const auto s = perforated();
    PlateEdit r;
    r.operation = "repair";
    r.parts = {"a"};
    r.maxHoleArea = 100;
    EXPECT_TRUE(detail::editPlateShapes({{"a", s}}, r, {}).replacements.empty());
    r.maxHoleArea = 0;
    auto result = detail::editPlateShapes({{"a", s}}, r, {});
    EXPECT_NEAR(detail::plateVolume(result.replacements.at("a")[0]), 100 * 60 * 6, 1e-4);
    EXPECT_NEAR(detail::plateVolume(s), (100 * 60 - std::numbers::pi * 64) * 6, 1e-4);
}
TEST(PlateEditing, RepairBoundaryIsExplicitAndDoesNotCutConvexCorners)
{
    const auto plate = box(0, 0, 0, 100, 60, 6);
    const auto notch = detail::plateSolids(BRepAlgoAPI_Cut(plate, box(40, -1, -1, 10, 11, 8)).Shape()).front();
    PlateEdit r;
    r.operation = "repair";
    r.parts = {"a"};
    EXPECT_TRUE(detail::editPlateShapes({{"a", notch}}, r, {}).replacements.empty());
    r.boundary = true;
    const auto result = detail::editPlateShapes({{"a", notch}}, r, {});
    ASSERT_EQ(result.replacements.size(), 1u);
    EXPECT_NEAR(detail::plateVolume(result.replacements.at("a")[0]), 36000, 1e-4);
    EXPECT_TRUE(detail::editPlateShapes({{"a", plate}}, r, {}).replacements.empty());
}
TEST(PlateEditing, CropConvertsExactRectangleToIndependentWorldPlane)
{
    PlateEdit r;
    r.operation = "crop";
    r.parts = {"a", "untouched"};
    CloudSelectionRect rectangle;
    rectangle.viewProjection = {.01, 0, 0, 0, 0, .01, 0, 0, 0, 0, .01, 0, 0, 0, 0, 1};
    rectangle.minX = -.1;
    rectangle.maxX = .12345;
    rectangle.minY = -.1;
    rectangle.maxY = .5;
    r.rectangle = rectangle;
    const std::map<std::string, TopoDS_Shape> shapes{{"a", box(0, 0, 0, 100, 40, 6)},
                                                     {"untouched", box(200, 0, 0, 100, 40, 6)}};
    const auto result = detail::editPlateShapes(shapes, r, {});
    ASSERT_EQ(r.cuts.size(), 1u);
    EXPECT_FALSE(r.rectangle);
    EXPECT_NEAR(detail::plateVolume(result.replacements.at("a")[0]), (100 - 12.345) * 40 * 6, 1e-5);
    EXPECT_FALSE(result.replacements.contains("untouched"));
    auto replay = detail::editPlateShapes(shapes, r, {});
    EXPECT_NEAR(detail::plateVolume(replay.replacements.at("a")[0]),
                detail::plateVolume(result.replacements.at("a")[0]), 1e-6);
    rectangle.minX = .3;
    rectangle.maxX = .6;
    r.rectangle = rectangle;
    EXPECT_THROW(detail::editPlateShapes(shapes, r, {}), GeometryError);
    rectangle.minX = -.1;
    rectangle.maxX = 1.1;
    r.rectangle = rectangle;
    EXPECT_THROW(detail::editPlateShapes(shapes, r, {}), GeometryError);
}
TEST(PlateEditing, BatchFailureAndCancellationLeaveSnapshotUnchanged)
{
    const auto m =
        model({perforated(),
               BRepAlgoAPI_Cut(box(200, 0, 0, 100, 60, 6),
                               BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(230, 30, 3), gp_Dir(0, 0, 1)), 8, 8).Shape())
                   .Shape()});
    const auto before = m->summary().volumeMm3;
    PlateEdit r;
    r.operation = "repair";
    r.parts = m->summary().partIds;
    EXPECT_THROW(editModelPlates(*m, r), GeometryError);
    EXPECT_EQ(m->summary().volumeMm3, before);
    r.parts.resize(1);
    std::stop_source stop;
    stop.request_stop();
    EXPECT_THROW(editModelPlates(*m, r, stop.get_token()), GeometryError);
}
TEST(PlateEditing, StableSplitIdsAndUnselectedGeometry)
{
    const auto m = model({box(0, 0, 0, 100, 40, 6), box(45, -10, -10, 10, 60, 30), box(200, 0, 0, 20, 20, 2)});
    PlateEdit r;
    r.operation = "split";
    r.parts = {m->summary().partIds[0]};
    r.target = m->summary().partIds[1];
    const auto a = editModelPlates(*m, r), b = editModelPlates(*m, r);
    EXPECT_EQ(a.model->summary().partIds, b.model->summary().partIds);
    EXPECT_EQ(a.model->summary().solidCount, 4u);
    EXPECT_EQ(a.model->summary().partIds.back(), m->summary().partIds.back());
    EXPECT_EQ(m->summary().solidCount, 3u);
}
TEST(PlateEditing, PreviewApplyUndoLateRecordingReplayAndStaleRevision)
{
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("rm-plate-edit-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(folder);
    const auto source = model({perforated(), box(200, 0, 0, 100, 40, 6)});
    (void)exportStep(*source, folder / "source.step");
    CommandService service;
    ASSERT_EQ(done(service, service.submitImport(folder / "source.step")).state,
              TaskState::Succeeded);
    const auto before = service.project();
    PlateEdit r;
    r.operation = "repair";
    r.parts = {before.model->partIds[0]};
    (void)service.takeUiCommandEvents();
    const auto review = done(service, service.submitPlateReview(before.model->partIds, r, before.revision));
    ASSERT_EQ(review.state, TaskState::Succeeded) << review.message;
    const auto events = service.takeUiCommandEvents();
    std::string diagnostics;
    for (const auto &event : events)
    {
        EXPECT_EQ(event.kind, UiCommandEventKind::Output);
        diagnostics += event.message;
    }
    EXPECT_NE(diagnostics.find(before.model->partIds[0]), std::string::npos);
    EXPECT_NE(diagnostics.find(before.model->partIds[1]), std::string::npos);
    auto preview = done(service, service.submitPlateEditPreview(r, before.revision));
    ASSERT_EQ(preview.state, TaskState::Succeeded) << preview.message;
    EXPECT_EQ(service.project().revision, before.revision);
    ASSERT_TRUE(service.takeEdgePreview(preview.id));
    const auto apply = done(service, service.submitPlateEdit(r, before.revision));
    ASSERT_EQ(apply.state, TaskState::Succeeded) << apply.message;
    const auto volume = service.project().model->volumeMm3;
    EXPECT_GT(volume, before.model->volumeMm3);
    EXPECT_EQ(done(service, service.submitPlateEdit(r, before.revision)).state, TaskState::Failed);
    ASSERT_EQ(done(service, service.submitLine("project.undo")).state, TaskState::Succeeded);
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, before.model->volumeMm3);
    ASSERT_EQ(done(service, service.submitLine("project.redo")).state, TaskState::Succeeded);
    EXPECT_DOUBLE_EQ(service.project().model->volumeMm3, volume);
    ASSERT_EQ(done(service, service.submitStartRecording(folder / "late.py")).state, TaskState::Succeeded);
    ASSERT_EQ(done(service, service.submitStopRecording()).state, TaskState::Succeeded);
    CommandService replay;
    auto replayed = done(replay, replay.submitScriptFile(folder / "late.py"));
    ASSERT_EQ(replayed.state, TaskState::Succeeded) << replayed.message;
    EXPECT_NEAR(replay.project().model->volumeMm3, volume, 1e-5);
    std::filesystem::remove_all(folder);
}
TEST(PlateEditingSamples, RealHoleStepRepairExportAndPcdImport)
{
    const auto directory = std::getenv("RM_PLATE_SAMPLE_DIR");
    if (!directory)
        GTEST_SKIP() << "Set RM_PLATE_SAMPLE_DIR to test supplied fixtures";
    const auto folder = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(directory)));
    const auto source = loadCadModel(folder / std::filesystem::path(L"孔.step"));
    ASSERT_GT(source->summary().solidCount, 0u);
    std::size_t changed = 0;
    double gain = 0;
    for (const auto &id : source->summary().partIds)
    {
        PlateEdit r;
        r.operation = "repair";
        r.parts = {id};
        r.boundary = true;
        r.notchWidth = 200;
        r.notchDepth = 100;
        try
        {
            const auto result = editModelPlates(*source, r);
            if (result.changed)
            {
                ++changed;
                gain += result.model->summary().volumeMm3 - source->summary().volumeMm3;
                const auto path =
                    std::filesystem::temp_directory_path() /
                    ("rm-repair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                     ".step");
                const auto exported = exportStep(*result.model, path);
                EXPECT_TRUE(exported.valid);
                EXPECT_EQ(loadCadModel(path)->summary().solidCount, source->summary().solidCount);
                std::filesystem::remove(path);
            }
        }
        catch (const GeometryError &e)
        {
            ADD_FAILURE() << id << ": " << e.what();
        }
    }
    // 样本名称含“孔”，但 STEP 中所有板面均只有外环；保留它的阶梯/端部外形，不能把外形误填平。
    EXPECT_EQ(changed, 0u);
    EXPECT_DOUBLE_EQ(gain, 0);
    // 在导入样本的真实板件副本上打贯穿孔，再修复并验证 STEP 往返；不改动用户样本。
    const auto fixtureCopy =
        std::filesystem::temp_directory_path() /
        ("rm-plate-fixture-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    std::filesystem::copy_file(folder / std::filesystem::path(L"孔.step"), fixtureCopy);
    STEPControl_Reader reader;
    ASSERT_EQ(reader.ReadFile(fixtureCopy.string().c_str()), IFSelect_RetDone);
    std::filesystem::remove(fixtureCopy);
    reader.TransferRoots();
    const auto solids = detail::plateSolids(reader.OneShape());
    ASSERT_GT(solids.size(), 1u);
    const auto &plate = solids[1];
    const auto info = detail::inspectRepairPlateThickness(plate);
    const gp_Dir normal(info.normal.x, info.normal.y, info.normal.z);
    const gp_Pnt middle(info.centerMm.x, info.centerMm.y, info.centerMm.z);
    const auto cutter =
        BRepPrimAPI_MakeCylinder(gp_Ax2(middle.Translated(gp_Vec(normal) * (-info.thicknessMm - 1)), normal), 10,
                                 info.thicknessMm * 2 + 2)
            .Shape();
    const auto drilled = detail::plateSolids(BRepAlgoAPI_Cut(plate, cutter).Shape());
    ASSERT_EQ(drilled.size(), 1u);
    ASSERT_LT(detail::plateVolume(drilled.front()), detail::plateVolume(plate));
    PlateEdit repair;
    repair.operation = "repair";
    repair.parts = {"fixture"};
    std::string report;
    const auto healed = detail::repairPlate(drilled.front(), repair, report);
    EXPECT_NEAR(detail::plateVolume(healed), detail::plateVolume(plate), 1e-3);
    const auto repairedModel = model({healed});
    const auto path =
        std::filesystem::temp_directory_path() /
        ("rm-real-repair-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".step");
    ASSERT_TRUE(exportStep(*repairedModel, path).valid);
    EXPECT_NEAR(loadCadModel(path)->summary().volumeMm3, repairedModel->summary().volumeMm3, .01);
    std::filesystem::remove(path);
    CommandService service;
    const auto imported =
        done(service, service.submitImport(folder / std::filesystem::path(L"孔.pcd")));
    ASSERT_EQ(imported.state, TaskState::Succeeded) << imported.message;
    ASSERT_TRUE(service.project().cloud);
    EXPECT_GT(service.project().cloud->points.size(), 0u);
}

TEST(PlateEditing, BatchWeldUsesIndependentEdgesAndIsAtomic)
{
    const auto source = model({box(0, 0, 0, 200, 12, 100), box(300, 0, 0, 200, 12, 100), box(600, 0, 0, 200, 12, 100)});
    PlateEdit r;
    r.operation = "holes";
    r.parts = {source->summary().partIds[0], source->summary().partIds[1]};
    for (const auto &id : r.parts)
    {
        PlateHole h;
        h.partId = id;
        h.sizeMm = 20;
        for (const auto &b : inspectModelHoleBases(*source, id))
            if (std::abs(b.firstMm.z) < 1e-6 && std::abs(b.secondMm.z) < 1e-6)
                h.bottomEdge = b.edge;
        ASSERT_NE(h.bottomEdge, 0u);
        r.holes.push_back(h);
    }
    const auto result = editModelPlates(*source, r);
    EXPECT_EQ(result.model->summary().solidCount, 3u);
    EXPECT_NEAR(source->summary().volumeMm3 - result.model->summary().volumeMm3, std::numbers::pi * 20 * 20 * 12, 1e-4);
    r.holes[1].bottomEdge = 999;
    EXPECT_THROW(editModelPlates(*source, r), GeometryError);
    EXPECT_NEAR(source->summary().volumeMm3, 3 * 200 * 12 * 100, 1e-4);
}
