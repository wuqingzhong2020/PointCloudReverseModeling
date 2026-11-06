/* 文件描述：验证基准面退化检查、工程/显示坐标一致性、剖视偏移反向及精确平面拾取。 */
#include <limits>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "commands/CommandService.h"
#include "commands/ProjectHistory.h"
#include "commands/SectionPlane.h"
#include "geometry/ModelTransform.h"
#include "geometry/ReferencePlane.h"
#include "workbench/ReferencePlanePicking.h"

using namespace reverse;

TEST(SectionPlane, PositioningUsesSectionThenReconstructedBottomOtherwiseTop)
{
    ProjectSnapshot project;
    project.referencePlane = std::make_shared<const ReferencePlane>(
        makeReferencePlane({Vec3{0, 0, 20}, {10, 0, 20}, {0, 10, 20}}));
    ASSERT_TRUE(positioningPlane(project));
    EXPECT_EQ(positioningView(project), PositioningView::Top);
    EXPECT_DOUBLE_EQ(positioningPlane(project)->pointsMm[0].z, 0); // 不使用自定义基准面。
    project.section = {true, 5, false, SectionPlaneSource::Reference};
    ASSERT_TRUE(positioningPlane(project));
    EXPECT_DOUBLE_EQ(positioningPlane(project)->normal.z, 1);
    project.recognition.emplace();
    RecognizedPlate web;
    web.id = "web"; web.normalAxis = 1;
    web.minimumMm = {0, 10, -20}; web.maximumMm = {100, 14, 20};
    project.recognition->plates.push_back(web);
    project.section.source = SectionPlaneSource::Bottom;
    const auto bottom = positioningPlane(project);
    ASSERT_TRUE(bottom);
    EXPECT_DOUBLE_EQ(bottom->normal.y, 1);
    EXPECT_DOUBLE_EQ(bottom->pointsMm[0].y, 12);
    const auto clipped = sectionEquation(*bottom, project.section);
    EXPECT_DOUBLE_EQ(clipped[3], -17);
    project.section.enabled = false;
    project.section.source = SectionPlaneSource::Reference;
    EXPECT_EQ(positioningView(project), PositioningView::Top); // 仅识别出底板不等于已重建。
    EXPECT_DOUBLE_EQ(positioningPlane(project)->normal.z, 1);
    auto model = std::make_shared<ModelSummary>();
    model->partIds = {"web"};
    model->plates = project.recognition->plates;
    project.model = model;
    EXPECT_EQ(positioningView(project), PositioningView::Bottom);
    EXPECT_DOUBLE_EQ(positioningPlane(project)->normal.y, 1);
    project.section.enabled = true;
    EXPECT_EQ(positioningView(project), PositioningView::Section);
    EXPECT_DOUBLE_EQ(positioningPlane(project)->pointsMm[0].z, 20);
}

TEST(SectionPlane, TopViewDoesNotDependOnImportExportOrCustomReferencePlane)
{
    ProjectSnapshot project;
    const auto expectTop = [&] {
        EXPECT_EQ(positioningView(project), PositioningView::Top);
        const auto plane = positioningPlane(project);
        ASSERT_TRUE(plane);
        EXPECT_DOUBLE_EQ(plane->normal.z, 1);
        EXPECT_DOUBLE_EQ(plane->normal.x, 0);
        EXPECT_DOUBLE_EQ(plane->normal.y, 0);
        EXPECT_DOUBLE_EQ(plane->xAxis.x, 1);
    };
    expectTop(); // 空工程也可使用上视图。
    project.cloud = std::make_shared<CloudData>();
    project.sourcePathUtf8 = "input.pcd";
    project.exportedCloudPathUtf8 = "output.pcd";
    project.exportedPathUtf8 = "output.step";
    project.lastExport.emplace();
    project.lastCloudExport.emplace();
    expectTop();
    auto model = std::make_shared<ModelSummary>();
    model->partIds = {"web"};
    project.model = model;
    for (const auto *format : {"step", "cwrm"})
    {
        model->importedFormat = format;
        expectTop(); // 导入 CAD 文件本身不算本地重建。
    }
    model->importedFormat.clear();
    EXPECT_EQ(positioningView(project), PositioningView::Bottom);
    project.model.reset();
    expectTop(); // 回到无重建模型状态。
}

TEST(SectionPlane, ModelBottomFollowsPartTransformInsteadOfOldRecognition)
{
    ProjectSnapshot project;
    project.recognition.emplace();
    RecognizedPlate web;
    web.id = "web"; web.normalAxis = 1;
    project.recognition->plates.push_back(web);
    auto model = std::make_shared<ModelSummary>();
    model->partIds = {"web"};
    model->frame.originMm = {1000000, 2000000, 3000000};
    model->partTransforms["web"] = modelTransformFrame(
        {ModelTransformKind::Rotate, {}, RotationAxis::Z, 90, model->frame.originMm});
    project.model = model;
    const auto bottom = bottomSectionPlane(project);
    ASSERT_TRUE(bottom);
    EXPECT_NEAR(bottom->normal.x, -1, 1e-8);
    EXPECT_NEAR(bottom->normal.y, 0, 1e-8);
    EXPECT_NEAR(bottom->pointsMm[0].x, 1000000, 1e-8);
    const auto local = localReferencePlane(*bottom, model->frame.originMm);
    EXPECT_NEAR(sectionEquation(local, {true, 3, false, SectionPlaneSource::Bottom})[3], -3, 1e-8);
    model->partIds.clear();
    EXPECT_FALSE(bottomSectionPlane(project)); // 底板删除后不能复用旧识别结果。
    model->partIds = {"web"};
    model->importedFormat = "step";
    EXPECT_FALSE(bottomSectionPlane(project)); // 导入文件没有语义底板时不能猜测。
}

TEST(SectionPlane, HistoryKeepsAvailableSourceAndDisablesMissingSource)
{
    ProjectSnapshot project;
    auto model = std::make_shared<ModelSummary>();
    model->partIds = {"web"};
    project.model = model;
    const auto saved = ProjectContent::capture(project, {});
    project.section = {true, 8, true, SectionPlaneSource::Bottom};
    EXPECT_EQ(saved.restore(project, false).section, project.section);
    EXPECT_FALSE(saved.restore(project, true).section.enabled);
    project.section.source = SectionPlaneSource::Reference;
    EXPECT_FALSE(saved.restore(project, false).section.enabled);
}

TEST(ReferencePlane, ThreePointsDefineOrthonormalFrameAndOrder)
{
    const auto plane = makeReferencePlane({Vec3{10, 20, 30}, {110, 20, 30}, {10, 70, 30}});
    EXPECT_DOUBLE_EQ(plane.normal.z, 1);
    EXPECT_DOUBLE_EQ(plane.xAxis.x, 1);
    EXPECT_DOUBLE_EQ(plane.normal.x, 0);
    const auto reversed = makeReferencePlane({plane.pointsMm[0], plane.pointsMm[2], plane.pointsMm[1]});
    EXPECT_DOUBLE_EQ(reversed.normal.z, -1);
}
TEST(ReferencePlane, RejectsDuplicateCollinearAndNonFinitePoints)
{
    EXPECT_THROW((void)makeReferencePlane({Vec3{}, {}, {1, 2, 3}}), GeometryError);
    EXPECT_THROW((void)makeReferencePlane({Vec3{}, {1, 2, 3}, {2, 4, 6}}), GeometryError);
    EXPECT_THROW((void)makeReferencePlane({Vec3{}, {1, 0, 0}, {1, 1e-9, 0}}), GeometryError);
    EXPECT_THROW((void)makeReferencePlane({Vec3{}, {1, 0, 0}, {0, std::numeric_limits<double>::infinity(), 0}}), GeometryError);
}
TEST(ReferencePlane, LocalOriginPreservesNormalAndSectionPosition)
{
    const auto world = makeReferencePlane({Vec3{1000000, -3000, 25}, {1000010, -3000, 25}, {1000000, -2990, 25}});
    const auto local = localReferencePlane(world, {1000000, -3000, 20});
    EXPECT_DOUBLE_EQ(local.pointsMm[0].x, 0);
    EXPECT_DOUBLE_EQ(local.pointsMm[0].z, 5);
    const auto e = sectionEquation(local, {true, 7, false});
    EXPECT_DOUBLE_EQ(e[2], 1);
    EXPECT_DOUBLE_EQ(e[3], -12);
    const auto reverse = sectionEquation(local, {true, 7, true});
    for (std::size_t i = 0; i < 4; ++i) EXPECT_DOUBLE_EQ(reverse[i], -e[i]);
    EXPECT_THROW((void)sectionEquation(local, {true, std::numeric_limits<double>::quiet_NaN(), false}), GeometryError);
}
TEST(ReferencePlane, FeatureFaceRejectsCurvedTopologyAndUsesWholeFaceIndex)
{
    workbench::ModelDisplayPart part;
    part.vertices = {{0, 0, 50}, {20, 0, 50}, {0, 30, 50}, {0, 0, 0}, {5, 0, 0}, {0, 5, 0}};
    part.triangles = {{0, 1, 2}, {3, 4, 5}};
    part.triangleFaceIndices = {8, 9};
    part.planarFaceIndices = {8};
    const auto plane = workbench::referencePlaneFromFace(part, 8);
    EXPECT_DOUBLE_EQ(plane.pointsMm[0].z, 50);
    EXPECT_DOUBLE_EQ(plane.normal.z, 1);
    EXPECT_THROW((void)workbench::referencePlaneFromFace(part, 9), GeometryError);
    EXPECT_THROW((void)workbench::referencePlaneFromFace(part, 100), GeometryError);
}
TEST(ReferencePlane, CadPlanesSurviveModelTransformAndDisplayOrigin)
{
    BeamParameters parameters;
    parameters.length = 1000; parameters.width = 200; parameters.height = 300;
    parameters.webThickness = 10; parameters.flangeThickness = 15; parameters.ribThickness = 8;
    const auto model = buildBeam({{10000, 20000, 30000}, {0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, parameters);
    const Vec3 origin{10000, 20000, 30000};
    const auto parts = workbench::makeModelDisplayParts(model->mesh(), model->summary(), origin);
    ASSERT_FALSE(parts.empty());
    for (const auto &part : parts)
    {
        ASSERT_FALSE(part.planarFaceIndices.empty());
        for (const auto face : part.planarFaceIndices)
        {
            const auto plane = workbench::referencePlaneFromFace(part, face);
            const auto e = sectionEquation(plane, {});
            for (std::size_t i = 0; i < part.triangles.size(); ++i)
            {
                if (part.triangleFaceIndices[i] != face) continue;
                for (auto vertex : part.triangles[i])
                {
                    const auto &p = part.vertices[vertex];
                    EXPECT_NEAR(e[0] * p.x + e[1] * p.y + e[2] * p.z + e[3], 0, 1e-7);
                }
            }
        }
    }
}
