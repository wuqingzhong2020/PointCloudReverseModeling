/* 文件描述：验证基准面退化检查、工程/显示坐标一致性、剖视偏移反向及精确平面拾取。 */
#include <limits>

#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "geometry/ReferencePlane.h"
#include "workbench/ReferencePlanePicking.h"

using namespace reverse;

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
