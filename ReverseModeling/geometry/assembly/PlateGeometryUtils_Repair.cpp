/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * 实现逆向建模板件孔洞清除与边缘缺口/锯齿修复算法（PlateGeometryUtils::RepairPlateHolesAndBoundary）：
 * 1. 提取板件最大主平面与法向真实厚度区间，通过 BRepTools::OuterWire 剥离全部内部孔洞环（Inner Wires）；
 * 2. 将外轮廓有序顶点投影至底平面 2D 坐标系，执行极短毛刺边过滤、共线冗余折点合并（simplifyCollinearAndShortEdges）
 *    与焊缝直边内凹小缺口桥接（bridgeSmallBoundaryNotches），同时严格保护矩形直角、斜切角与 L 型阶梯拐角；
 * 3. 由规整后的无孔外轮廓重构底平面并沿法向拉伸为纯净实体，经 ShapeUpgrade_UnifySameDomain 统一同域面与边。
*/
#include "geometry/assembly/PlateGeometryUtils.h"
#include <QDebug>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepTools.hxx>
#include <BRepTools_WireExplorer.hxx>
#include <BRep_Tool.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <TopoDS.hxx>
#include <vector>

namespace
{
    struct Pnt2D
    {
        double u = 0.0;
        double v = 0.0;
    };

    double dist2D(const Pnt2D &a, const Pnt2D &b)
    {
        return std::hypot(b.u - a.u, b.v - a.v);
    }

    // 消除 2D 多边形外轮廓中的极短毛刺边、共线冗余折点与微小锯齿台阶
    void simplifyCollinearAndShortEdges(std::vector<Pnt2D> &pts)
    {
        const double cos4Deg = std::cos(4.0 * std::numbers::pi / 180.0);
        for (int iter = 0; iter < 12 && pts.size() > 3; ++iter) {
            bool changed = false;

            // 1. 合并距离过近的相邻顶点 (< 0.8mm)
            for (size_t i = 0; i < pts.size() && pts.size() > 3;) {
                const size_t nextIdx = (i + 1) % pts.size();
                if (dist2D(pts[i], pts[nextIdx]) < 0.8) {
                    pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(nextIdx));
                    changed = true;
                } else {
                    ++i;
                }
            }

            // 2. 移除近似共线的中间折点与微小抖动顶点
            for (size_t i = 0; i < pts.size() && pts.size() > 3;) {
                const size_t n = pts.size();
                const Pnt2D &prev = pts[(i + n - 1) % n];
                const Pnt2D &curr = pts[i];
                const Pnt2D &next = pts[(i + 1) % n];

                const double v1u = curr.u - prev.u;
                const double v1v = curr.v - prev.v;
                const double v2u = next.u - curr.u;
                const double v2v = next.v - curr.v;
                const double len1 = std::hypot(v1u, v1v);
                const double len2 = std::hypot(v2u, v2v);

                if (len1 < 0.5 || len2 < 0.5) {
                    pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                    continue;
                }

                const double chordU = next.u - prev.u;
                const double chordV = next.v - prev.v;
                const double chordLen = std::hypot(chordU, chordV);
                const double cosAngle = (v1u * v2u + v1v * v2v) / (len1 * len2);
                const double crossAbs = std::abs(v1u * chordV - v1v * chordU);
                const double perpDist = (chordLen > 1e-6) ? (crossAbs / chordLen) : len1;

                const bool isCollinear = (cosAngle >= cos4Deg && perpDist <= 1.5);
                const bool isMicroStep = (cosAngle >= 0.0 && perpDist <= 0.65);
                const bool isTinySpike = (cosAngle < -0.92 && std::min(len1, len2) <= 5.0);

                if (isCollinear || isMicroStep || isTinySpike) {
                    pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(i));
                    changed = true;
                } else {
                    ++i;
                }
            }

            if (!changed) break;
        }
    }

    // 桥接修补长直边上因点云边缘缺失产生的局部内凹小缺口（保护正常拐角、斜切角与L型阶梯）
    void bridgeSmallBoundaryNotches(std::vector<Pnt2D> &pts)
    {
        const double cos8Deg  = std::cos(8.0 * std::numbers::pi / 180.0);
        const double cos10Deg = std::cos(10.0 * std::numbers::pi / 180.0);
        constexpr double kMaxNotchSpan  = 35.0; // 最大缺口跨度 (mm)
        constexpr double kMaxNotchDepth = 20.0; // 最大缺口凹陷深度 (mm)
        constexpr double kMinMainEdge   = 6.0;  // 缺口两侧主边最小长度 (mm)

        for (int pass = 0; pass < 4 && pts.size() > 4; ++pass) {
            bool bridgedAny = false;

            for (size_t i = 0; i < pts.size() && pts.size() > 4; ++i) {
                const size_t n = pts.size();
                const Pnt2D &pIn0 = pts[(i + n - 1) % n];
                const Pnt2D &pIn1 = pts[i];

                const double inU = pIn1.u - pIn0.u;
                const double inV = pIn1.v - pIn0.v;
                const double inLen = std::hypot(inU, inV);
                if (inLen < kMinMainEdge) continue;
                const double dirInU = inU / inLen;
                const double dirInV = inV / inLen;

                const int maxK = std::min(8, static_cast<int>(n) - 4);
                int matchedK = -1;

                // 从大到小尝试跨越 k 个凹坑内部顶点
                for (int k = maxK; k >= 1; --k) {
                    const size_t out0Idx = (i + static_cast<size_t>(k) + 1) % n;
                    const size_t out1Idx = (out0Idx + 1) % n;
                    const Pnt2D &pOut0 = pts[out0Idx];
                    const Pnt2D &pOut1 = pts[out1Idx];

                    const double outU = pOut1.u - pOut0.u;
                    const double outV = pOut1.v - pOut0.v;
                    const double outLen = std::hypot(outU, outV);
                    if (outLen < kMinMainEdge) continue;
                    const double dirOutU = outU / outLen;
                    const double dirOutV = outV / outLen;

                    // 条件1：进入缺口前的主边与离开缺口后的主边必须平行同向（夹角 <= 8°）
                    if (dirInU * dirOutU + dirInV * dirOutV < cos8Deg) continue;

                    // 条件2：缺口跨度在阈值内，且桥接弦方向与主边同向
                    const double spanU = pOut0.u - pIn1.u;
                    const double spanV = pOut0.v - pIn1.v;
                    const double spanLen = std::hypot(spanU, spanV);
                    if (spanLen < 0.1 || spanLen > kMaxNotchSpan) continue;
                    const double dirSpanU = spanU / spanLen;
                    const double dirSpanV = spanV / spanLen;
                    if (dirInU * dirSpanU + dirInV * dirSpanV < cos10Deg) continue;
                    if (dirOutU * dirSpanU + dirOutV * dirSpanV < cos10Deg) continue;

                    // 条件3：离开点 pOut0 与前主边直线的横向错位 <= 2.5mm（排除平行台阶）
                    const double lateralOffset = std::abs((pOut0.u - pIn0.u) * (-dirInV) + (pOut0.v - pIn0.v) * dirInU);
                    if (lateralOffset > 2.5) continue;

                    // 条件4：中间所有凹坑顶点到主边直线的凹陷深度 <= kMaxNotchDepth
                    bool depthOk = true;
                    for (int m = 1; m <= k; ++m) {
                        const Pnt2D &pMid = pts[(i + static_cast<size_t>(m)) % n];
                        const double depth = std::abs((pMid.u - pIn1.u) * (-dirSpanV) + (pMid.v - pIn1.v) * dirSpanU);
                        if (depth > kMaxNotchDepth) {
                            depthOk = false;
                            break;
                        }
                    }
                    if (!depthOk) continue;

                    matchedK = k;
                    break;
                }

                if (matchedK > 0) {
                    for (int m = 0; m < matchedK; ++m) {
                        const size_t eraseIdx = (i + 1) % pts.size();
                        pts.erase(pts.begin() + static_cast<std::ptrdiff_t>(eraseIdx));
                    }
                    bridgedAny = true;
                    break;
                }
            }

            if (!bridgedAny) break;
            simplifyCollinearAndShortEdges(pts);
        }
    }
} // namespace

namespace reverse::detail::assembly
{
    bool PlateGeometryUtils::RepairPlateHolesAndBoundary(
        const TopoDS_Shape &sourcePlate,
        TopoDS_Shape &outRepairedPlate,
        int *outRemovedHoles,
        int *outSimplifiedVertices,
        QString *outError)
    {
        if (outRemovedHoles) *outRemovedHoles = 0;
        if (outSimplifiedVertices) *outSimplifiedVertices = 0;

        if (sourcePlate.IsNull()) {
            if (outError) *outError = QStringLiteral("输入板件模型为空");
            return false;
        }

        try {
            PlatePrincipalFrame frame;
            if (!ExtractPlatePrincipalFrame(sourcePlate, frame)) {
                if (outError) *outError = QStringLiteral("无法提取板件的主平面特征");
                return false;
            }

            // 1. 计算板件在主法向 frame.dirT 上的真实厚度区间 [tMin, tMax]
            double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0, tMin = 0.0, tMax = 0.0;
            ComputeShapeProjectedBounds(sourcePlate, frame.origin, frame.dirU, frame.dirV, frame.dirT,
                                        uMin, uMax, vMin, vMax, tMin, tMax);
            const double thickness = tMax - tMin;
            if (thickness < 0.5 || thickness > 150.0) {
                if (outError) {
                    *outError = QStringLiteral("识别到的零件厚度 (%1 mm) 超出常规板件范围，跳过修复").arg(thickness, 0, 'f', 1);
                }
                return false;
            }

            // 2. 统计主平面上的孔洞环数量（Inner Wires = 总 Wire 数 - 1）并提取唯一外边界 OuterWire
            int totalWires = 0;
            for (TopExp_Explorer expW(frame.mainFace, TopAbs_WIRE); expW.More(); expW.Next()) {
                ++totalWires;
            }
            const int innerHoleCount = std::max(0, totalWires - 1);

            const TopoDS_Wire outerWire = BRepTools::OuterWire(frame.mainFace);
            if (outerWire.IsNull()) {
                if (outError) *outError = QStringLiteral("无法提取板件主平面的外边界环");
                return false;
            }

            // 3. 将外边界有序顶点投影到底平面 (t = tMin) 的 2D 坐标系 (dirU, dirV)
            const gp_Pnt bottomOrigin = frame.origin.Translated(gp_Vec(frame.dirT) * tMin);
            const gp_Vec vecU(frame.dirU);
            const gp_Vec vecV(frame.dirV);

            std::vector<Pnt2D> polyPts;
            for (BRepTools_WireExplorer wExp(outerWire, frame.mainFace); wExp.More(); wExp.Next()) {
                const TopoDS_Vertex vtx = wExp.CurrentVertex();
                if (vtx.IsNull()) continue;
                const gp_Pnt p = BRep_Tool::Pnt(vtx);
                const gp_Vec rel(bottomOrigin, p);
                const Pnt2D pt2d{rel.Dot(vecU), rel.Dot(vecV)};
                if (polyPts.empty() || dist2D(polyPts.back(), pt2d) > 0.1) {
                    polyPts.push_back(pt2d);
                }
            }
            if (polyPts.size() > 1 && dist2D(polyPts.front(), polyPts.back()) <= 0.1) {
                polyPts.pop_back();
            }

            const int originalVertexCount = static_cast<int>(polyPts.size());
            if (originalVertexCount < 3) {
                if (outError) *outError = QStringLiteral("板件外边界有效顶点不足 3 个");
                return false;
            }

            // 4. 执行 2D 外轮廓去锯齿、共线折点合并与边缘小凹坑桥接
            simplifyCollinearAndShortEdges(polyPts);
            bridgeSmallBoundaryNotches(polyPts);
            simplifyCollinearAndShortEdges(polyPts);

            const int finalVertexCount = static_cast<int>(polyPts.size());
            if (finalVertexCount < 3) {
                if (outError) *outError = QStringLiteral("简化后外轮廓退化，取消修复");
                return false;
            }

            const int simplifiedVertices = std::max(0, originalVertexCount - finalVertexCount);
            if (outRemovedHoles) *outRemovedHoles = innerHoleCount;
            if (outSimplifiedVertices) *outSimplifiedVertices = simplifiedVertices;

            // 5. 由纯净无孔的 2D 外轮廓构建底平面 Face 并沿主法向拉伸为 Solid
            BRepBuilderAPI_MakePolygon makePoly;
            for (const Pnt2D &pt : polyPts) {
                const gp_Pnt p3d = bottomOrigin
                                   .Translated(vecU * pt.u)
                                   .Translated(vecV * pt.v);
                makePoly.Add(p3d);
            }
            makePoly.Close();
            if (!makePoly.IsDone() || makePoly.Wire().IsNull()) {
                if (outError) *outError = QStringLiteral("构造修复后的板件外轮廓环失败");
                return false;
            }

            const gp_Pln bottomPlane(bottomOrigin, frame.dirT);
            BRepBuilderAPI_MakeFace makeFace(bottomPlane, makePoly.Wire(), true);
            if (!makeFace.IsDone() || makeFace.Face().IsNull()) {
                if (outError) *outError = QStringLiteral("构造修复后的无孔板件底面失败");
                return false;
            }

            TopoDS_Shape repairedSolid = ExtrudeAlignedFaceToSolid(makeFace.Face(), frame.dirT, thickness);
            if (repairedSolid.IsNull()) {
                if (outError) *outError = QStringLiteral("拉伸生成修复后的板件实体失败");
                return false;
            }

            try {
                ShapeUpgrade_UnifySameDomain unifier(repairedSolid, true, true, false);
                unifier.Build();
                if (!unifier.Shape().IsNull()) {
                    repairedSolid = unifier.Shape();
                }
            } catch (...) {
            }

            outRepairedPlate = repairedSolid;
            qInfo()
                << QStringLiteral("[修复板件] 成功清除 %1 个内部孔洞，简化 %2 个边缘锯齿/缺口顶点（外轮廓顶点数: %3 -> %4）")
                   .arg(innerHoleCount).arg(simplifiedVertices).arg(originalVertexCount).arg(finalVertexCount);
            return true;

        } catch (const Standard_Failure &e) {
            if (outError) *outError = QStringLiteral("OCC异常: %1").arg(e.what());
            return false;
        } catch (const std::exception &e) {
            if (outError) *outError = QStringLiteral("标准异常: %1").arg(e.what());
            return false;
        } catch (...) {
            if (outError) *outError = QStringLiteral("未知异常导致板件修复失败");
            return false;
        }
    }
} // namespace reverse::detail::assembly
