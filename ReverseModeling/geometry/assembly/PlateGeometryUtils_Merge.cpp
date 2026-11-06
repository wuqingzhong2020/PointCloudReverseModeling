/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * 实现板件实体之间的共面合并算法（PlateGeometryUtils::MergePlateToTarget）：
 * 1. 基于主平面法向平行度、面内直边主轴向匹配度及交界支点处的法向共面度开展三道门禁校验；
 * 2. 遵循目标板主基准规则（Target-Master Rule）：将源板合并到目标板时，严格以目标板的法向、底面位置及板厚作为唯一主基准；
 * 3. 支持标准同宽矩形板快速重构，以及 L 型阶梯板、带斜切端面板、不等宽异形板的端面桥接拉伸与布尔融合（Fuse + ShapeUpgrade_UnifySameDomain）。
*/
#include "geometry/assembly/PlateGeometryUtils.h"
#include "geometry/assembly/OccTopologyUtils.h"
#include <QDebug>

#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Vertex.hxx>
#include <gp_Ax2.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <TopoDS.hxx>
#include <limits>
#include <vector>

namespace
{
    // 计算对齐后的面在靠近拼接缝一侧端面的截面宽度区间 [cNearMin, cNearMax]
    void computeNearStitchWidthRange(
        const TopoDS_Shape &alignedFace,
        const gp_Pnt &origin,
        const gp_Dir &axisS,
        const gp_Dir &axisC,
        double sMin, double sMax,
        bool nearMaxEnd,
        double fullCMin, double fullCMax,
        double &outCNearMin, double &outCNearMax)
    {
        const double spanS = std::max(1.0, sMax - sMin);
        const double bandTol = std::max(15.0, 0.18 * spanS);
        const gp_Vec vecS(axisS);
        const gp_Vec vecC(axisC);

        double cMin = std::numeric_limits<double>::max();
        double cMax = std::numeric_limits<double>::lowest();
        int count = 0;

        for (TopExp_Explorer exp(alignedFace, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            const gp_Vec rel(origin, p);
            const double s = rel.Dot(vecS);
            const double c = rel.Dot(vecC);
            const bool isNear = nearMaxEnd ? (s >= sMax - bandTol) : (s <= sMin + bandTol);
            if (isNear) {
                cMin = std::min(cMin, c);
                cMax = std::max(cMax, c);
                ++count;
            }
        }

        if (count >= 2 && (cMax - cMin) > 1.0) {
            outCNearMin = cMin;
            outCNearMax = cMax;
        } else {
            outCNearMin = fullCMin;
            outCNearMax = fullCMax;
        }
    }

    // 将较窄侧板件朝向对方板件的侧端面沿延伸方向拉伸穿入对方内部（完美适配斜端面与折线端面）
    TopoDS_Shape buildEndFaceBridgePrisms(
        const TopoDS_Shape &narrowSolid,
        const TopoDS_Shape &wideSolid,
        const gp_Dir &refT,
        const gp_Dir &extendDir,
        double maxEmbedDepth)
    {
        BRepExtrema_DistShapeShape extrema(narrowSolid, wideSolid);
        extrema.Perform();
        if (!extrema.IsDone() || extrema.NbSolution() < 1) {
            return TopoDS_Shape();
        }

        const gp_Pnt pntOnNarrow = extrema.PointOnShape1(1);
        const gp_Pnt pntOnWide   = extrema.PointOnShape2(1);
        const gp_Vec vecExtend(extendDir);
        const gp_Vec vecT(refT);

        // 1. 定位 wideSolid 上靠近接缝的目标接触端面（排除正反大主平面）
        const auto widePlanes = reverse::detail::assembly::PlateGeometryUtils::CollectPlanes(wideSolid);
        int bestWideIdx = -1;
        double minDistToWidePnt = std::numeric_limits<double>::max();
        for (size_t i = 0; i < widePlanes.size(); ++i) {
            if (std::abs(widePlanes[i].normal.Dot(vecT)) > 0.8) continue;
            if (widePlanes[i].normal.Dot(vecExtend) > -0.1) continue;

            const double d = widePlanes[i].plane.Distance(pntOnWide);
            if (d < minDistToWidePnt) {
                minDistToWidePnt = d;
                bestWideIdx = static_cast<int>(i);
            }
        }

        // 2. 收集 narrowSolid 上朝向 extendDir 的侧端面集合（排除顶/底长边上的微折面）
        const auto narrowPlanes = reverse::detail::assembly::PlateGeometryUtils::CollectPlanes(narrowSolid);
        std::vector<reverse::detail::assembly::PlateGeometryUtils::FacePlaneInfo> endFaces;
        for (const auto &info : narrowPlanes) {
            if (std::abs(info.normal.Dot(vecT)) > 0.8) continue;
            if (info.normal.Dot(vecExtend) <= 0.45) continue;
            if (gp_Vec(pntOnNarrow, info.center).Dot(vecExtend) < -50.0) continue;
            endFaces.push_back(info);
        }

        if (endFaces.empty()) {
            return TopoDS_Shape();
        }

        std::vector<TopoDS_Shape> prisms;
        for (const auto &fa : endFaces) {
            double maxRayDist = extrema.Value();
            if (bestWideIdx >= 0) {
                const gp_Pln targetPln = widePlanes[bestWideIdx].plane;
                const gp_Vec plnNorm(targetPln.Axis().Direction());
                const double denom = std::abs(vecExtend.Dot(plnNorm));
                if (denom > 0.15) {
                    for (TopExp_Explorer exp(fa.face, TopAbs_VERTEX); exp.More(); exp.Next()) {
                        const gp_Pnt vPnt = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                        const double signedDist = gp_Vec(vPnt, targetPln.Location()).Dot(plnNorm);
                        const double rayDist = std::abs(signedDist) / denom;
                        maxRayDist = std::max(maxRayDist, rayDist);
                    }
                } else {
                    maxRayDist = std::max(maxRayDist, reverse::detail::assembly::PlateGeometryUtils::CalculateMaxVertexDistanceToPlane(fa.face, targetPln));
                }
            }

            const double extrudeLen = maxRayDist + maxEmbedDepth;
            if (extrudeLen <= 0.15) continue;

            BRepPrimAPI_MakePrism prismMaker(fa.face, vecExtend * extrudeLen);
            prismMaker.Build();
            if (prismMaker.IsDone() && !prismMaker.Shape().IsNull()) {
                TopoDS_Shape prism = prismMaker.Shape();
                GProp_GProps props;
                BRepGProp::VolumeProperties(prism, props);
                if (props.Mass() < 0.0) {
                    prism.Reverse();
                }
                prisms.push_back(prism);
            }
        }

        if (prisms.empty()) {
            return TopoDS_Shape();
        }
        if (prisms.size() == 1) {
            return prisms.front();
        }
        TopoDS_Compound compound;
        BRep_Builder builder;
        builder.MakeCompound(compound);
        for (const auto &p : prisms) {
            builder.Add(compound, p);
        }
        return compound;
    }
} // namespace

namespace reverse::detail::assembly
{
    bool PlateGeometryUtils::MergePlateToTarget(
        const TopoDS_Shape &sourcePlate,
        const TopoDS_Shape &targetPlate,
        TopoDS_Shape &outMergedPlate,
        QString *outError)
    {
        if (sourcePlate.IsNull() || targetPlate.IsNull()) {
            if (outError) *outError = QStringLiteral("输入板件模型为空");
            return false;
        }

        if (sourcePlate.IsSame(targetPlate)) {
            if (outError) *outError = QStringLiteral("源板件与目标板件不能为同一个实体");
            return false;
        }

        try {
            // 1. 提取源板件 A 与目标板件 B 的主平面及直边方向簇 (兼容矩形板、L 型阶梯板及带斜切端面的异形板)
            PlatePrincipalFrame frameA, frameB;
            if (!ExtractPlatePrincipalFrame(sourcePlate, frameA) ||
                !ExtractPlatePrincipalFrame(targetPlate, frameB)) {
                if (outError) *outError = QStringLiteral("无法提取板件的主平面或主轴方向");
                return false;
            }

            // 2. 门禁 ①：校验两板主平面法向（板厚方向）平行度
            const double cosNormal = std::abs(gp_Vec(frameA.dirT).Dot(gp_Vec(frameB.dirT)));
            const double normalAngleDeg = std::acos(std::clamp(cosNormal, 0.0, 1.0)) * 180.0 / std::numbers::pi;
            constexpr double kMaxNormalAngleDeg = 8.0;
            if (normalAngleDeg > kMaxNormalAngleDeg) {
                if (outError) {
                    *outError = QStringLiteral("两块板的主平面存在明显倾斜（法向夹角 %1° > %2°），无法合并。")
                                .arg(normalAngleDeg, 0, 'f', 1)
                                .arg(kMaxNormalAngleDeg, 0, 'f', 0);
                }
                return false;
            }

            // 3. 计算两板之间的真实物理最近点对，取交界中点 pivotPnt 作为法向拉平支点（彻底消除长板远端杠杆放大效应）
            gp_Pnt pivotPnt(0.5 * (frameA.origin.XYZ() + frameB.origin.XYZ()));
            {
                BRepExtrema_DistShapeShape extremaInit(sourcePlate, targetPlate);
                extremaInit.Perform();
                if (extremaInit.IsDone() && extremaInit.NbSolution() >= 1) {
                    const gp_Pnt pA = extremaInit.PointOnShape1(1);
                    const gp_Pnt pB = extremaInit.PointOnShape2(1);
                    pivotPnt = gp_Pnt(0.5 * (pA.XYZ() + pB.XYZ()));
                }
            }

            // Target-Master 基准原则：A 合并到 B 时，严格以目标板 B 的法向、平面位置与板厚作为唯一主基准
            const PlatePrincipalFrame &refFrame = frameB;
            const gp_Dir refT = refFrame.dirT;
            const gp_Vec vecRefT(refT);

            // 4. 门禁 ②：校验两板面内直边主轴向匹配度，并锁定公共长边延伸主轴 (refU, refV)
            const gp_Trsf levelTrsfA = MakeNormalLevelingTrsf(frameA.dirT, refT, pivotPnt);
            const gp_Trsf levelTrsfB = MakeNormalLevelingTrsf(frameB.dirT, refT, pivotPnt);

            gp_Vec centerChord(frameA.origin, frameB.origin);
            centerChord = centerChord - vecRefT * centerChord.Dot(vecRefT);
            if (centerChord.Magnitude() > 1e-4) {
                centerChord.Normalize();
            }

            double bestMatchAngleDeg = 90.0;
            double bestScore = -1.0;
            gp_Dir matchedDirB = refFrame.dirU;
            gp_Dir matchedDirA = frameA.dirU;
            constexpr double kMaxInPlaneAngleDeg = 10.0;

            for (const auto &clB : frameB.edgeClusters) {
                gp_Vec vB = gp_Vec(clB.dir).Transformed(levelTrsfB);
                vB = vB - vecRefT * vB.Dot(vecRefT);
                if (vB.Magnitude() < 1e-6) continue;
                vB.Normalize();

                for (const auto &clA : frameA.edgeClusters) {
                    gp_Vec vA = gp_Vec(clA.dir).Transformed(levelTrsfA);
                    vA = vA - vecRefT * vA.Dot(vecRefT);
                    if (vA.Magnitude() < 1e-6) continue;
                    vA.Normalize();

                    const double dotPar = std::abs(vA.Dot(vB));
                    const double angParDeg = std::acos(std::clamp(dotPar, 0.0, 1.0)) * 180.0 / std::numbers::pi;
                    if (angParDeg < bestMatchAngleDeg) {
                        bestMatchAngleDeg = angParDeg;
                    }
                    if (angParDeg <= kMaxInPlaneAngleDeg) {
                        const double chordWeight = 1.0 + 0.5 * std::abs(vB.Dot(centerChord));
                        const double score = (clA.totalLength + clB.totalLength) * chordWeight;
                        if (score > bestScore) {
                            bestScore = score;
                            matchedDirB = gp_Dir(vB);
                            matchedDirA = gp_Dir(vA.Dot(vB) >= 0.0 ? vA : -vA);
                        }
                    }
                }
            }

            // 兜底：检查正交标架 (dirU, dirV) 的匹配度
            if (bestScore < 0.0) {
                gp_Vec uA = gp_Vec(frameA.dirU).Transformed(levelTrsfA);
                uA = uA - vecRefT * uA.Dot(vecRefT);
                gp_Vec uB = gp_Vec(frameB.dirU).Transformed(levelTrsfB);
                uB = uB - vecRefT * uB.Dot(vecRefT);
                if (uA.Magnitude() > 1e-6 && uB.Magnitude() > 1e-6) {
                    uA.Normalize();
                    uB.Normalize();
                    const gp_Vec vB = vecRefT.Crossed(uB);
                    const double dotU = std::abs(uA.Dot(uB));
                    const double dotV = std::abs(uA.Dot(vB));
                    const double maxCos = std::max(dotU, dotV);
                    const double orthoAngleDeg = std::acos(std::clamp(maxCos, 0.0, 1.0)) * 180.0 / std::numbers::pi;
                    bestMatchAngleDeg = std::min(bestMatchAngleDeg, orthoAngleDeg);
                    if (orthoAngleDeg <= kMaxInPlaneAngleDeg) {
                        matchedDirB = gp_Dir(uB);
                        const gp_Vec targetB = (dotU >= dotV) ? uB : vB;
                        matchedDirA = gp_Dir(uA.Dot(targetB) >= 0.0 ? uA : -uA);
                        bestScore = 1.0;
                    }
                }
            }

            if (bestScore < 0.0) {
                if (outError) {
                    *outError = QStringLiteral("两块板的OBB主轴向不匹配（面内轴向偏差 %1° > %2°），无法合并。")
                                .arg(bestMatchAngleDeg, 0, 'f', 1)
                                .arg(kMaxInPlaneAngleDeg, 0, 'f', 0);
                }
                return false;
            }

            const gp_Dir refU = matchedDirB;
            const gp_Dir refV = refT.Crossed(refU);

            // 计算源板 A 在面内相对于目标板 B 公共主轴的微小修正角（消除 <= 10° 的面内微偏角）
            double inPlaneAdjustRadA = 0.0;
            {
                const gp_Vec vecA(matchedDirA);
                const gp_Vec vecB(matchedDirB);
                const double dotAB = std::clamp(vecA.Dot(vecB), -1.0, 1.0);
                const double sinAB = vecA.Crossed(vecB).Dot(vecRefT);
                const double angRad = std::atan2(sinAB, dotAB);
                if (std::abs(angRad) <= kMaxInPlaneAngleDeg * std::numbers::pi / 180.0) {
                    inPlaneAdjustRadA = angRad;
                }
            }

            // 5. 门禁 ③：在绕交界支点 pivotPnt 纯法向拉平后的两板上校验板厚方向共面度
            const TopoDS_Shape leveledPlateA = BRepBuilderAPI_Transform(sourcePlate, levelTrsfA, true).Shape();
            const TopoDS_Shape leveledPlateB = BRepBuilderAPI_Transform(targetPlate, levelTrsfB, true).Shape();

            double uAMin = 0.0, uAMax = 0.0, vAMin = 0.0, vAMax = 0.0, tAMin = 0.0, tAMax = 0.0;
            double uBMin = 0.0, uBMax = 0.0, vBMin = 0.0, vBMax = 0.0, tBMin = 0.0, tBMax = 0.0;
            ComputeShapeProjectedBounds(leveledPlateA, pivotPnt, refU, refV, refT,
                                        uAMin, uAMax, vAMin, vAMax, tAMin, tAMax);
            ComputeShapeProjectedBounds(leveledPlateB, pivotPnt, refU, refV, refT,
                                        uBMin, uBMax, vBMin, vBMax, tBMin, tBMax);

            double thickA = tAMax - tAMin;
            double thickB = tBMax - tBMin;
            double calcThickA = 0.0, calcThickB = 0.0;
            if (OccTopologyUtils::tryCalculateThickness(sourcePlate, calcThickA) && calcThickA > 0.5) {
                thickA = std::min(thickA, calcThickA);
            }
            if (OccTopologyUtils::tryCalculateThickness(targetPlate, calcThickB) && calcThickB > 0.5) {
                thickB = std::min(thickB, calcThickB);
            }

            const double maxSingleThick = std::max(thickA, thickB);
            if (maxSingleThick < 0.2) {
                if (outError) *outError = QStringLiteral("板件厚度异常，无法合并");
                return false;
            }

            const double tMidA = 0.5 * (tAMin + tAMax);
            const double tMidB = 0.5 * (tBMin + tBMax);
            const double realTAMin = tMidA - 0.5 * thickA;
            const double realTAMax = tMidA + 0.5 * thickA;
            const double realTBMin = tMidB - 0.5 * thickB;
            const double realTBMax = tMidB + 0.5 * thickB;

            const double envelopeThick = std::max(realTAMax, realTBMax) - std::min(realTAMin, realTBMin);
            const double maxAllowedEnvelopeThick = std::max(1.75 * maxSingleThick, maxSingleThick + 6.0);

            if (envelopeThick > maxAllowedEnvelopeThick) {
                if (outError) {
                    *outError = QStringLiteral("两块板在板厚方向未对齐（法向包络厚度 %1 mm 超过单板厚度 %2 mm 的允许范围），无法合并。")
                                .arg(envelopeThick, 0, 'f', 1)
                                .arg(maxSingleThick, 0, 'f', 1);
                }
                return false;
            }

            // 6. 遵循目标板继承规则（A 合并到 B 则新板厚 = B 的板厚，且严格锁定目标板 B 的上下主表面位置不动）
            const double mergedThick = thickB;
            const gp_Pnt bottomOrigin = pivotPnt.Translated(gp_Vec(refT) * realTBMin);
            TopoDS_Face alignedFaceA = AlignFaceToReferencePlane(frameA, pivotPnt, bottomOrigin, refU, refV, refT, inPlaneAdjustRadA);
            TopoDS_Face alignedFaceB = AlignFaceToReferencePlane(frameB, pivotPnt, bottomOrigin, refU, refV, refT, 0.0);
            if (alignedFaceA.IsNull() || alignedFaceB.IsNull()) {
                if (outError) *outError = QStringLiteral("对齐板件主平面轮廓失败");
                return false;
            }

            double tDummy1 = 0.0, tDummy2 = 0.0;
            ComputeShapeProjectedBounds(alignedFaceA, bottomOrigin, refU, refV, refT,
                                        uAMin, uAMax, vAMin, vAMax, tDummy1, tDummy2);
            ComputeShapeProjectedBounds(alignedFaceB, bottomOrigin, refU, refV, refT,
                                        uBMin, uBMax, vBMin, vBMax, tDummy1, tDummy2);

            const double ovU = std::min(uAMax, uBMax) - std::max(uAMin, uBMin);
            const double ovV = std::min(vAMax, vBMax) - std::max(vAMin, vBMin);

            if (ovU < 1.0 && ovV < 1.0) {
                if (outError) {
                    *outError = QStringLiteral("两块板在面内呈对角错开位置，无公共投影重叠区域，无法直接合并。");
                }
                return false;
            }

            // 7. 判定拼接延伸轴 axisS 与截面宽度轴 axisC
            bool stitchAlongU = true;
            if (ovU < 1.0) {
                stitchAlongU = true;
            } else if (ovV < 1.0) {
                stitchAlongU = false;
            } else {
                const double lenU = std::max(1.0, std::min(uAMax - uAMin, uBMax - uBMin));
                const double lenV = std::max(1.0, std::min(vAMax - vAMin, vBMax - vBMin));
                const double ratioU = ovU / lenU;
                const double ratioV = ovV / lenV;
                stitchAlongU = (ratioV >= ratioU);
            }

            const gp_Dir axisS = stitchAlongU ? refU : refV;
            const gp_Dir axisC = stitchAlongU ? refV : refU.Reversed();

            double sAMin = 0.0, sAMax = 0.0, cAMin = 0.0, cAMax = 0.0;
            double sBMin = 0.0, sBMax = 0.0, cBMin = 0.0, cBMax = 0.0;
            ComputeShapeProjectedBounds(alignedFaceA, bottomOrigin, axisS, axisC, refT,
                                        sAMin, sAMax, cAMin, cAMax, tDummy1, tDummy2);
            ComputeShapeProjectedBounds(alignedFaceB, bottomOrigin, axisS, axisC, refT,
                                        sBMin, sBMax, cBMin, cBMax, tDummy1, tDummy2);

            const double spanAS = sAMax - sAMin;
            const double spanAC = cAMax - cAMin;
            const double spanBS = sBMax - sBMin;
            const double spanBC = cBMax - cBMin;

            const bool isRectA = (frameA.mainArea >= 0.95 * spanAS * spanAC);
            const bool isRectB = (frameB.mainArea >= 0.95 * spanBS * spanBC);
            const double snapTol = std::clamp(mergedThick * 1.2, 8.0, 15.0);

            // 分支 A：若两板均为规整矩形板，且上下宽度边界均在平齐容差内 -> 以目标板 B 的宽度与厚度生成 6 面标准矩形板
            if (isRectA && isRectB &&
                std::abs(cBMin - cAMin) <= snapTol &&
                std::abs(cBMax - cAMax) <= snapTol) {
                const double sMinAll = std::min(sAMin, sBMin);
                const double sMaxAll = std::max(sAMax, sBMax);
                const double cMinTarget = cBMin;
                const double cMaxTarget = cBMax;

                const gp_Pnt boxCorner = bottomOrigin
                                         .Translated(gp_Vec(axisS) * sMinAll)
                                         .Translated(gp_Vec(axisC) * cMinTarget);
                const gp_Ax2 boxAx2(boxCorner, refT, axisS);
                BRepPrimAPI_MakeBox makeBox(boxAx2, sMaxAll - sMinAll, cMaxTarget - cMinTarget, mergedThick);
                makeBox.Build();
                if (!makeBox.IsDone() || makeBox.Shape().IsNull()) {
                    if (outError) *outError = QStringLiteral("构造合并矩形板失败");
                    return false;
                }
                outMergedPlate = makeBox.Shape();
                qInfo() << "[合并到板] 同宽矩形板合并成功，合并后板厚:" << mergedThick << "mm";
                return true;
            }

            // 分支 B：存在异形板（如 L 型/阶梯板、斜切端面板、折线边板）或两板宽度不同
            const double midSA = 0.5 * (sAMin + sAMax);
            const double midSB = 0.5 * (sBMin + sBMax);
            const bool aIsLeftOfB = (midSA <= midSB);

            double cANearMin = cAMin, cANearMax = cAMax;
            double cBNearMin = cBMin, cBNearMax = cBMax;
            computeNearStitchWidthRange(alignedFaceA, bottomOrigin, axisS, axisC,
                                        sAMin, sAMax, aIsLeftOfB, cAMin, cAMax,
                                        cANearMin, cANearMax);
            computeNearStitchWidthRange(alignedFaceB, bottomOrigin, axisS, axisC,
                                        sBMin, sBMax, !aIsLeftOfB, cBMin, cBMax,
                                        cBNearMin, cBNearMax);

            double shiftC = 0.0;
            double bestAbsShift = snapTol + 1.0;
            const double shiftCandidates[4] = {
                cBMin - cAMin,
                cBMax - cAMax,
                cBNearMin - cANearMin,
                cBNearMax - cANearMax
            };
            for (const double cand : shiftCandidates) {
                if (std::abs(cand) <= snapTol && std::abs(cand) < bestAbsShift) {
                    bestAbsShift = std::abs(cand);
                    shiftC = cand;
                }
            }

            if (std::abs(shiftC) > 1e-5) {
                gp_Trsf trsfShiftC;
                trsfShiftC.SetTranslation(gp_Vec(axisC) * shiftC);
                alignedFaceA = TopoDS::Face(BRepBuilderAPI_Transform(alignedFaceA, trsfShiftC, true).Shape());
                cAMin += shiftC;
                cAMax += shiftC;
                cANearMin += shiftC;
                cANearMax += shiftC;
            }

            double cBridgeMin = std::max(cANearMin, cBNearMin);
            double cBridgeMax = std::min(cANearMax, cBNearMax);
            if (cBridgeMax - cBridgeMin < 1.0) {
                cBridgeMin = std::max(cAMin, cBMin);
                cBridgeMax = std::min(cAMax, cBMax);
            }

            const double minWidth = std::min(spanAC, spanBC);
            if (cBridgeMax - cBridgeMin < std::min(5.0, 0.2 * minWidth)) {
                if (outError) {
                    *outError = QStringLiteral("两块板在拼接截面上的重叠宽度过小，无法合并。");
                }
                return false;
            }

            const TopoDS_Shape solidA = ExtrudeAlignedFaceToSolid(alignedFaceA, refT, mergedThick);
            const TopoDS_Shape solidB = ExtrudeAlignedFaceToSolid(alignedFaceB, refT, mergedThick);
            if (solidA.IsNull() || solidB.IsNull()) {
                if (outError) *outError = QStringLiteral("重构同厚板件实体失败");
                return false;
            }

            TopoDS_Shape mergedShape = solidB;
            const double embed = std::min({6.0, 0.25 * spanAS, 0.25 * spanBS});

            const double nearWidthA = cANearMax - cANearMin;
            const double nearWidthB = cBNearMax - cBNearMin;
            const bool aIsNarrower = (nearWidthA <= nearWidthB);
            const TopoDS_Shape &narrowSolid = aIsNarrower ? solidA : solidB;
            const TopoDS_Shape &wideSolid   = aIsNarrower ? solidB : solidA;
            const bool narrowLeftOfWide     = aIsNarrower ? aIsLeftOfB : !aIsLeftOfB;
            const gp_Dir extendDir          = narrowLeftOfWide ? axisS : axisS.Reversed();

            bool bridgedByEndFace = false;
            const TopoDS_Shape endFaceBridge = buildEndFaceBridgePrisms(narrowSolid, wideSolid, refT, extendDir, embed);
            if (!endFaceBridge.IsNull()) {
                BRepAlgoAPI_Fuse fuseEndBridge(mergedShape, endFaceBridge);
                fuseEndBridge.SetFuzzyValue(0.02);
                fuseEndBridge.Build();
                if (fuseEndBridge.IsDone() && !fuseEndBridge.Shape().IsNull()) {
                    mergedShape = fuseEndBridge.Shape();
                    bridgedByEndFace = true;
                }
            }

            if (!bridgedByEndFace) {
                const double sGapStart = aIsLeftOfB ? sAMax : sBMax;
                const double sGapEnd   = aIsLeftOfB ? sBMin : sAMin;
                if (sGapEnd >= sGapStart - 1.0) {
                    const double sBridgeMin = sGapStart - embed;
                    const double sBridgeMax = sGapEnd + embed;
                    if (sBridgeMax - sBridgeMin > 0.2) {
                        const gp_Pnt bridgeCorner = bottomOrigin
                                                    .Translated(gp_Vec(axisS) * sBridgeMin)
                                                    .Translated(gp_Vec(axisC) * cBridgeMin);
                        const gp_Ax2 bridgeAx2(bridgeCorner, refT, axisS);
                        BRepPrimAPI_MakeBox makeBridge(bridgeAx2,
                                                       sBridgeMax - sBridgeMin,
                                                       cBridgeMax - cBridgeMin,
                                                       mergedThick);
                        makeBridge.Build();
                        if (makeBridge.IsDone() && !makeBridge.Shape().IsNull()) {
                            BRepAlgoAPI_Fuse fuseBridge(mergedShape, makeBridge.Shape());
                            fuseBridge.SetFuzzyValue(0.02);
                            fuseBridge.Build();
                            if (fuseBridge.IsDone() && !fuseBridge.Shape().IsNull()) {
                                mergedShape = fuseBridge.Shape();
                            }
                        }
                    }
                }
            }

            BRepAlgoAPI_Fuse fuseAll(mergedShape, solidA);
            fuseAll.SetFuzzyValue(0.02);
            fuseAll.Build();
            if (!fuseAll.IsDone() || fuseAll.Shape().IsNull()) {
                if (outError) *outError = QStringLiteral("布尔融合两块板件失败");
                return false;
            }
            mergedShape = fuseAll.Shape();

            try {
                ShapeUpgrade_UnifySameDomain unifier(mergedShape, true, true, false);
                unifier.Build();
                if (!unifier.Shape().IsNull()) {
                    mergedShape = unifier.Shape();
                }
            } catch (...) {
            }

            outMergedPlate = mergedShape;
            qInfo() << "[合并到板] 异形/阶梯板合并成功，合并后统一板厚:" << mergedThick << "mm";
            return true;

        } catch (const Standard_Failure &e) {
            if (outError) *outError = QStringLiteral("OCC异常: %1").arg(e.what());
            return false;
        } catch (const std::exception &e) {
            if (outError) *outError = QStringLiteral("标准异常: %1").arg(e.what());
            return false;
        } catch (...) {
            if (outError) *outError = QStringLiteral("未知异常导致合并失败");
            return false;
        }
    }
} // namespace reverse::detail::assembly
