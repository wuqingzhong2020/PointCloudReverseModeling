/* 文件描述：验证重新三角化与旧模型拓扑隔离、局部边界语义和显示摘要一致性。 */
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gtest/gtest.h>

#include "geometry/ModelState.h"

namespace
{
using namespace reverse;
using namespace reverse::detail::model;

TEST(ModelPresentation, RemeshingDoesNotAttachTriangulationToAnEarlierSnapshot)
{
    const auto original = BRepPrimAPI_MakeBox(20, 30, 4).Shape();
    Model::Impl next;
    next.plates.push_back({"plate", {1, 2, 3}, {4, 5, 6}, original});
    next.summary.partIds = {"plate"};
    next.summary.solidCount = 1;
    beginModelPresentation(next);
    appendModelPart(next, 0);
    EXPECT_FALSE(next.plates.front().shape.IsPartner(original));
    for (TopExp_Explorer faces(original, TopAbs_FACE); faces.More(); faces.Next())
    {
        TopLoc_Location location;
        EXPECT_TRUE(BRep_Tool::Triangulation(TopoDS::Face(faces.Current()), location).IsNull());
    }
    EXPECT_FALSE(next.mesh.triangles.empty());
    EXPECT_EQ(next.mesh.partIds, next.summary.partIds);
    EXPECT_NEAR(next.summary.volumeMm3, 2400, 1e-6);
    EXPECT_EQ(next.summary.solidCount, 1u);
    // 默认保留参数化板件的局部坐标边界，不能偷偷改成世界坐标。
    EXPECT_EQ(next.plates.front().minimum.x, 1);
    EXPECT_EQ(next.plates.front().maximum.z, 6);
}

TEST(ModelPresentation, ExplicitWorldBoundsAndRepeatedRebuildDoNotAccumulateGeometry)
{
    Model::Impl next;
    next.plates.push_back({"plate", {}, {}, BRepPrimAPI_MakeBox(gp_Pnt(10, 20, 30), 20, 30, 4).Shape()});
    next.summary.partIds = {"plate"};
    std::size_t triangleCount = 0;
    for (int iteration = 0; iteration < 2; ++iteration)
    {
        beginModelPresentation(next);
        appendModelPart(next, 0, PartBounds::World);
        EXPECT_NEAR(next.plates.front().minimum.x, 10, 1e-7);
        EXPECT_NEAR(next.plates.front().maximum.z, 34, 1e-7);
        EXPECT_NEAR(next.summary.volumeMm3, 2400, 1e-6);
        EXPECT_NEAR(checkedVolume(next.compound), next.summary.volumeMm3, 1e-6);
        if (iteration == 0)
        {
            triangleCount = next.mesh.triangles.size();
        }
        EXPECT_EQ(next.mesh.triangles.size(), triangleCount);
    }
}
} // namespace
