/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * 实现板件几何后处理的公共基础算法（PlateGeometryUtils_Common）：
 * 1. 提取实体中的所有平面及其经朝向纠正的外法向、面积与中心（CollectPlanes）；
 * 2. 提取板件的最大主平面与直边平行方向聚类正交标架（ExtractPlatePrincipalFrame），兼容矩形板、L 型阶梯板与斜切端面板；
 * 3. 提供绕交界支点的纯法向拉平变换（MakeNormalLevelingTrsf），消除长板远端杠杆放大误差；
 * 4. 提供三维标架投影包围极值计算（ComputeShapeProjectedBounds）、主平面对齐（AlignFaceToReferencePlane）与正体积拉伸（ExtrudeAlignedFaceToSolid）。
*/
#include "geometry/assembly/PlateGeometryUtils.h"
#include "geometry/assembly/OccTopologyUtils.h"

#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_OBB.hxx>
#include <GProp_GProps.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax1.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <TopoDS.hxx>
#include <limits>

namespace reverse::detail::assembly
{
    gp_Pnt PlateGeometryUtils::CalculateFaceCenter(const TopoDS_Face &face)
    {
        BRepAdaptor_Surface surface(face);
        const double u = (surface.FirstUParameter() + surface.LastUParameter()) * 0.5;
        const double v = (surface.FirstVParameter() + surface.LastVParameter()) * 0.5;
        gp_Pnt center;
        gp_Vec d1u, d1v;
        surface.D1(u, v, center, d1u, d1v);
        return center;
    }

    std::vector<PlateGeometryUtils::FacePlaneInfo> PlateGeometryUtils::CollectPlanes(const TopoDS_Shape &shape)
    {
        std::vector<FacePlaneInfo> planes;
        if (shape.IsNull()) return planes;

        for (TopExp_Explorer exp(shape, TopAbs_FACE); exp.More(); exp.Next()) {
            const TopoDS_Face face = TopoDS::Face(exp.Current());
            if (face.IsNull()) continue;

            BRepAdaptor_Surface surface(face);
            if (surface.GetType() != GeomAbs_Plane) continue;

            FacePlaneInfo info;
            info.face = face;
            info.plane = surface.Plane();
            info.isPlanar = true;
            info.area = OccTopologyUtils::faceArea(face);
            info.center = CalculateFaceCenter(face);

            gp_Vec normal;
            if (OccTopologyUtils::tryFaceNormal(face, normal)) {
                info.normal = normal;
            } else {
                info.normal = info.plane.Axis().Direction();
                if (face.Orientation() == TopAbs_REVERSED) {
                    info.normal.Reverse();
                }
            }
            planes.push_back(info);
        }
        return planes;
    }

    double PlateGeometryUtils::CalculateMaxVertexDistanceToPlane(const TopoDS_Face &face, const gp_Pln &plane)
    {
        double maxDist = 0.0;
        for (TopExp_Explorer exp(face, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const TopoDS_Vertex v = TopoDS::Vertex(exp.Current());
            const gp_Pnt p = BRep_Tool::Pnt(v);
            const double dist = plane.Distance(p);
            if (dist > maxDist) {
                maxDist = dist;
            }
        }
        return maxDist;
    }

    bool PlateGeometryUtils::ExtractPlatePrincipalFrame(const TopoDS_Shape &shape, PlatePrincipalFrame &outFrame)
    {
        const auto planes = CollectPlanes(shape);
        if (planes.empty()) return false;

        int maxIdx = -1;
        double maxArea = -1.0;
        for (size_t i = 0; i < planes.size(); ++i) {
            if (planes[i].area > maxArea && planes[i].normal.Magnitude() > 1e-6) {
                maxArea = planes[i].area;
                maxIdx = static_cast<int>(i);
            }
        }
        if (maxIdx < 0 || maxArea <= 1e-3) return false;

        const FacePlaneInfo &mainInfo = planes[maxIdx];
        outFrame.mainFace = mainInfo.face;
        outFrame.mainArea = mainInfo.area;
        outFrame.origin = mainInfo.center;
        outFrame.dirT = gp_Dir(mainInfo.normal);
        outFrame.edgeClusters.clear();

        struct EdgeDirSample
        {
            gp_Dir dir;
            double length = 0.0;
        };
        std::vector<EdgeDirSample> edgeSamples;

        const gp_Vec vecT(outFrame.dirT);
        for (TopExp_Explorer exp(outFrame.mainFace, TopAbs_EDGE); exp.More(); exp.Next()) {
            const TopoDS_Edge edge = TopoDS::Edge(exp.Current());
            if (edge.IsNull()) continue;
            TopoDS_Vertex v1, v2;
            TopExp::Vertices(edge, v1, v2);
            if (v1.IsNull() || v2.IsNull()) continue;

            const gp_Pnt p1 = BRep_Tool::Pnt(v1);
            const gp_Pnt p2 = BRep_Tool::Pnt(v2);
            gp_Vec vec(p1, p2);
            vec = vec - vecT * vec.Dot(vecT); // 投影到主平面内
            const double len = vec.Magnitude();
            if (len < 1.0) continue;

            edgeSamples.push_back({gp_Dir(vec), len});
        }

        const double cos5Deg = std::cos(5.0 * std::numbers::pi / 180.0);
        if (!edgeSamples.empty()) {
            // 按纯平行关系（夹角 <= 5°）聚类所有直边方向，保留斜端边与水平长边各自独立的方向簇
            struct RawCluster
            {
                gp_Vec weightedSum{0.0, 0.0, 0.0};
                double totalLen = 0.0;
            };
            std::vector<RawCluster> rawClusters;
            for (const auto &sample : edgeSamples) {
                const gp_Vec d(sample.dir);
                bool matched = false;
                for (auto &cluster : rawClusters) {
                    const gp_Dir clusterDir(cluster.weightedSum);
                    const double dot = d.Dot(gp_Vec(clusterDir));
                    if (std::abs(dot) >= cos5Deg) {
                        cluster.weightedSum += (dot >= 0.0 ? d : -d) * sample.length;
                        cluster.totalLen += sample.length;
                        matched = true;
                        break;
                    }
                }
                if (!matched) {
                    rawClusters.push_back({d * sample.length, sample.length});
                }
            }

            for (const auto &rc : rawClusters) {
                gp_Vec v = rc.weightedSum - vecT * rc.weightedSum.Dot(vecT);
                if (v.Magnitude() > 1e-6) {
                    outFrame.edgeClusters.push_back({gp_Dir(v), rc.totalLen});
                }
            }
            std::sort(outFrame.edgeClusters.begin(), outFrame.edgeClusters.end(),
                      [](const EdgeDirectionCluster &a, const EdgeDirectionCluster &b) {
                          return a.totalLength > b.totalLength;
                      });

            if (!outFrame.edgeClusters.empty()) {
                outFrame.dirU = outFrame.edgeClusters.front().dir;
                outFrame.dirV = outFrame.dirT.Crossed(outFrame.dirU);
                return true;
            }
        }

        // 兜底：若无有效直边，使用 OBB 主轴投影到主平面内
        Bnd_OBB obb;
        BRepBndLib::AddOBB(shape, obb);
        gp_Vec fallbackU(obb.XDirection());
        fallbackU = fallbackU - vecT * fallbackU.Dot(vecT);
        if (fallbackU.Magnitude() < 1e-4) {
            fallbackU = gp_Vec(obb.YDirection());
            fallbackU = fallbackU - vecT * fallbackU.Dot(vecT);
        }
        if (fallbackU.Magnitude() < 1e-4) {
            gp_Vec temp(1.0, 0.0, 0.0);
            if (std::abs(vecT.Dot(temp)) > 0.9) temp = gp_Vec(0.0, 1.0, 0.0);
            fallbackU = vecT.Crossed(temp);
        }
        outFrame.dirU = gp_Dir(fallbackU);
        outFrame.dirV = outFrame.dirT.Crossed(outFrame.dirU);
        outFrame.edgeClusters.push_back({outFrame.dirU, 100.0});
        return true;
    }

    gp_Trsf PlateGeometryUtils::MakeNormalLevelingTrsf(const gp_Dir &fromT, const gp_Dir &targetT, const gp_Pnt &pivot)
    {
        gp_Vec curT(fromT);
        const gp_Vec dstT(targetT);
        if (curT.Dot(dstT) < 0.0) {
            curT.Reverse();
        }

        gp_Trsf rot;
        const gp_Vec crossT = curT.Crossed(dstT);
        const double sinT = crossT.Magnitude();
        const double cosT = std::clamp(curT.Dot(dstT), -1.0, 1.0);
        if (sinT > 1e-7) {
            const double angleT = std::atan2(sinT, cosT);
            rot.SetRotation(gp_Ax1(pivot, gp_Dir(crossT)), angleT);
        }
        return rot;
    }

    void PlateGeometryUtils::ComputeShapeProjectedBounds(
        const TopoDS_Shape &shape,
        const gp_Pnt &origin,
        const gp_Dir &dirU,
        const gp_Dir &dirV,
        const gp_Dir &dirT,
        double &uMin, double &uMax,
        double &vMin, double &vMax,
        double &tMin, double &tMax)
    {
        uMin = vMin = tMin = std::numeric_limits<double>::max();
        uMax = vMax = tMax = std::numeric_limits<double>::lowest();

        const gp_Vec vecU(dirU);
        const gp_Vec vecV(dirV);
        const gp_Vec vecT(dirT);

        for (TopExp_Explorer exp(shape, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            const gp_Vec rel(origin, p);
            const double u = rel.Dot(vecU);
            const double v = rel.Dot(vecV);
            const double t = rel.Dot(vecT);
            uMin = std::min(uMin, u);
            uMax = std::max(uMax, u);
            vMin = std::min(vMin, v);
            vMax = std::max(vMax, v);
            tMin = std::min(tMin, t);
            tMax = std::max(tMax, t);
        }
    }

    TopoDS_Face PlateGeometryUtils::AlignFaceToReferencePlane(
        const PlatePrincipalFrame &plateFrame,
        const gp_Pnt &pivotPnt,
        const gp_Pnt &bottomOrigin,
        const gp_Dir &refU,
        const gp_Dir &refV,
        const gp_Dir &refT,
        double inPlaneAdjustAngleRad)
    {
        const gp_Vec targetT(refT);
        const gp_Trsf rot1 = MakeNormalLevelingTrsf(plateFrame.dirT, refT, pivotPnt);

        gp_Trsf rot2;
        if (std::abs(inPlaneAdjustAngleRad) > 1e-7) {
            rot2.SetRotation(gp_Ax1(pivotPnt, refT), inPlaneAdjustAngleRad);
        }

        const gp_Trsf rotTotal = rot2 * rot1;
        BRepBuilderAPI_Transform rotTransform(plateFrame.mainFace, rotTotal, true);
        TopoDS_Face rotatedFace = TopoDS::Face(rotTransform.Shape());

        // 法向平移对齐：将拉平后的主平面平移至 bottomOrigin 所在的基准底平面 (t = 0)
        double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0, tMin = 0.0, tMax = 0.0;
        ComputeShapeProjectedBounds(rotatedFace, bottomOrigin, refU, refV, refT,
                                    uMin, uMax, vMin, vMax, tMin, tMax);
        const double tMid = 0.5 * (tMin + tMax);
        gp_Trsf transT;
        transT.SetTranslation(-targetT * tMid);
        BRepBuilderAPI_Transform finalTransform(rotatedFace, transT, true);
        return TopoDS::Face(finalTransform.Shape());
    }

    TopoDS_Shape PlateGeometryUtils::ExtrudeAlignedFaceToSolid(
        const TopoDS_Face &alignedFace,
        const gp_Dir &refT,
        double thickness)
    {
        BRepPrimAPI_MakePrism prismMaker(alignedFace, gp_Vec(refT) * thickness);
        prismMaker.Build();
        if (!prismMaker.IsDone() || prismMaker.Shape().IsNull()) {
            return TopoDS_Shape();
        }
        TopoDS_Shape solid = prismMaker.Shape();
        GProp_GProps props;
        BRepGProp::VolumeProperties(solid, props);
        if (props.Mass() < 0.0) {
            solid.Reverse();
        }
        return solid;
    }
} // namespace reverse::detail::assembly
