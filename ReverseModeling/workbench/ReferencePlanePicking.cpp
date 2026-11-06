/* 文件描述：复用 BRep 平面标志与面编号，挑选面积最大的非退化三角形作为三点定义。
 * 返回显示局部坐标；提交命令前由界面加回显示原点，保证脚本使用工程坐标。
 */
#include <algorithm>
#include <cmath>

#include "geometry/Geometry.h"
#include "geometry/ReferencePlane.h"
#include "workbench/ReferencePlanePicking.h"

namespace reverse::workbench
{
ReferencePlane referencePlaneFromFace(const ModelDisplayPart &part, std::size_t faceIndex)
{
    if (std::find(part.planarFaceIndices.begin(), part.planarFaceIndices.end(), faceIndex) ==
        part.planarFaceIndices.end())
        throw GeometryError("not_planar", "请选择平面特征面；曲面不能直接作为基准面，可改用三点方式。");
    std::array<Vec3, 3> points{};
    double bestArea = 0;
    for (std::size_t i = 0; i < part.triangles.size(); ++i)
    {
        if (i >= part.triangleFaceIndices.size() || part.triangleFaceIndices[i] != faceIndex) continue;
        const auto &t = part.triangles[i];
        const auto &p = part.vertices.at(t[0]), &q = part.vertices.at(t[1]), &r = part.vertices.at(t[2]);
        const Vec3 a{q.x - p.x, q.y - p.y, q.z - p.z}, b{r.x - p.x, r.y - p.y, r.z - p.z};
        const double area = std::hypot(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
        if (area > bestArea)
        {
            bestArea = area;
            points = {p, q, r};
        }
    }
    const auto original = makeReferencePlane(points);
    const auto &normal = original.normal;
    const auto distance = [&](const Vec3 &p) {
        return (p.x - points[0].x) * normal.x + (p.y - points[0].y) * normal.y + (p.z - points[0].z) * normal.z;
    };
    // 以面上的最长直边为横向，避免三角化对角线让正视结果倾斜。
    double longest = 0;
    std::optional<ReferencePlane> aligned;
    for (const auto &edge : part.edges)
    {
        if (edge.size() != 2 || std::abs(distance(edge[0])) > 1e-5 || std::abs(distance(edge[1])) > 1e-5) continue;
        const double length = std::hypot(edge[1].x - edge[0].x, edge[1].y - edge[0].y, edge[1].z - edge[0].z);
        if (length <= longest) continue;
        for (const auto &third : points)
        {
            try
            {
                auto candidate = makeReferencePlane({edge[0], edge[1], third});
                if (candidate.normal.x * normal.x + candidate.normal.y * normal.y + candidate.normal.z * normal.z < 0)
                    candidate = makeReferencePlane({edge[1], edge[0], third});
                aligned = candidate;
                longest = length;
                break;
            }
            catch (const GeometryError &) { } // 当前三角形角点可能恰好落在该边上，尝试另一角点。
        }
    }
    return aligned.value_or(original);
}
}
