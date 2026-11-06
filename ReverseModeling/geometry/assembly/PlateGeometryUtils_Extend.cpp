/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * 实现板件实体之间的几何延伸与贴合算法（PlateGeometryUtils::ExtendPlateToTarget）。
 * 算法立足第一性原理，采用物理真实缝隙分析与布尔差集贴合架构：
 * 1. 真实物理缝隙分析（BRepExtrema_DistShapeShape）：
 *    直接计算两工件之间的空间真实最短距离与最近点对 (P_A 在源板上, P_B 在目标板上)，确定唯一真实延伸方向；
 * 2. 幂等性守护：
 *    若实测物理缝隙 realGap 已 <= 0.15mm，判定两板已完全贴合，立即退出保持原样；
 * 3. 面组联合拉伸与目标板布尔差集（Cut）：
 *    收集源板面向延伸方向的侧端面集合，超长拉伸后减去目标板实体自身并剃平背面余量，
 *    确保边缘接触面 100% 贴合在目标板表面，并执行 ShapeUpgrade_UnifySameDomain 净化同域接缝。
*/
#include "geometry/assembly/PlateGeometryUtils.h"
#include "geometry/assembly/OccTopologyUtils.h"
#include <QDebug>

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS_Compound.hxx>
#include <gp_Ax2.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <TopoDS.hxx>
#include <limits>
#include <vector>

namespace
{
    // 构造位于目标平面背面的一块超大切除盒，切除所有超出目标板、裸露在空气中的微量穿透部分
    TopoDS_Shape makeBacksideCutterBox(const gp_Pln &targetPlane, const gp_Pnt &contactPnt, const gp_Dir &extendDir, double boxSize, double boxDepth)
    {
        const gp_Dir zDir(extendDir);
        gp_Vec tempVec(0, 0, 1);
        if (std::abs(zDir.Dot(gp_Dir(0, 0, 1))) > 0.9) {
            tempVec = gp_Vec(0, 1, 0);
        }
        const gp_Dir xDir(tempVec.Crossed(gp_Vec(zDir)));
        const gp_Dir yDir(gp_Vec(zDir).Crossed(gp_Vec(xDir)));

        // 将物理接触点 contactPnt 正交投影到 targetPlane 上作为切片中心
        const gp_Vec plnNorm = targetPlane.Axis().Direction();
        const double signedDist = gp_Vec(targetPlane.Location(), contactPnt).Dot(plnNorm);
        const gp_Pnt centerOnPlane = contactPnt.Translated(-plnNorm * signedDist);

        const gp_Pnt origin = centerOnPlane.Translated(gp_Vec(xDir) * (-boxSize * 0.5)
                              + gp_Vec(yDir) * (-boxSize * 0.5));

        const gp_Ax2 boxAx2(origin, zDir, xDir);
        BRepPrimAPI_MakeBox makeBox(boxAx2, boxSize, boxSize, boxDepth);
        makeBox.Build();
        return makeBox.Shape();
    }
} // namespace

namespace reverse::detail::assembly
{
    bool PlateGeometryUtils::ExtendPlateToTarget(
        const TopoDS_Shape &sourcePlate,
        const TopoDS_Shape &targetPlate,
        TopoDS_Shape &outExtendedPlate,
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
            // 1. 获取源板件 A 的主表面法向 normalWeb
            TopoDS_Face mainFaceA1, mainFaceA2;
            const bool hasMainFacesA = OccTopologyUtils::twoLargestFaces(sourcePlate, mainFaceA1, mainFaceA2);
            gp_Vec normalWeb(0, 0, 1);
            if (hasMainFacesA && OccTopologyUtils::tryFaceNormal(mainFaceA1, normalWeb)) {
                normalWeb.Normalize();
            }

            // 2. 第一性原理：精确计算两板之间的真实物理最近点对与最小物理间隙
            BRepExtrema_DistShapeShape extrema(sourcePlate, targetPlate);
            extrema.Perform();
            if (!extrema.IsDone() || extrema.NbSolution() < 1) {
                if (outError) *outError = QStringLiteral("无法计算两板件间的最短距离");
                return false;
            }

            const double realGap = extrema.Value();
            const gp_Pnt pntOnA = extrema.PointOnShape1(1);
            const gp_Pnt pntOnB = extrema.PointOnShape2(1);

            qInfo() << "[延长到板] 实测物理间隙 realGap:" << realGap << "mm"
                                          << "点A:" << pntOnA.X() << pntOnA.Y() << pntOnA.Z()
                                          << "点B:" << pntOnB.X() << pntOnB.Y() << pntOnB.Z();

            // 幂等性守护：若两板间的物理缝隙已经 <= 0.15mm，判定已完全紧密贴合，直接退出保持原样！
            if (realGap <= 0.15) {
                qInfo() << "[延长到板] 两板已充分贴合(缝隙 <= 0.15mm)，保持原样返回。";
                outExtendedPlate = sourcePlate;
                return true;
            }

            // 3. 计算横跨物理缝隙的延伸向量 extendDir (由 pntOnA 绝对正指向 pntOnB)
            gp_Vec gapVec(pntOnA, pntOnB);
            if (gapVec.Magnitude() < 1e-4) {
                outExtendedPlate = sourcePlate;
                return true;
            }

            // 正交投影在筋板主平面内，消除厚度方向分量
            gp_Vec projVec = gapVec - normalWeb * (gapVec.Dot(normalWeb));
            if (projVec.Magnitude() < 1e-4) {
                projVec = gapVec;
            }
            const gp_Dir extendDir(projVec);
            qInfo() << "[延长到板] 真实延伸方向 extendDir:"
                                          << extendDir.X() << extendDir.Y() << extendDir.Z();

            // 4. 定位目标板 B 上包含最近点 pntOnB 的接触平面 (即翼板内侧表面)
            const auto planesB = CollectPlanes(targetPlate);
            int bestIdxB = -1;
            double minDistToPntB = std::numeric_limits<double>::max();

            for (size_t j = 0; j < planesB.size(); ++j) {
                const double dist = planesB[j].plane.Distance(pntOnB);
                if (dist < minDistToPntB) {
                    minDistToPntB = dist;
                    bestIdxB = static_cast<int>(j);
                }
            }

            if (bestIdxB == -1) {
                if (outError) *outError = QStringLiteral("未能找到目标板上的接触面");
                return false;
            }

            const auto &targetFaceB = planesB[bestIdxB];
            const gp_Pln targetPlane = targetFaceB.plane;
            qInfo() << "[延长到板] 锁定目标接触面面积:" << targetFaceB.area
                                          << "距离最近点B:" << minDistToPntB << "mm";

            // 5. 收集筋板 A 上面向该延伸方向的全部侧端面 (长直端面、凸耳面、切角阶梯面等)
            const auto planesA = CollectPlanes(sourcePlate);
            std::vector<FacePlaneInfo> facesToExtend;
            double maxRemGap = 0.0;

            for (const auto &info : planesA) {
                // 排除正反大主表面
                if (hasMainFacesA && (info.face.IsSame(mainFaceA1) || info.face.IsSame(mainFaceA2))) {
                    continue;
                }

                // 侧面外法向朝向延伸方向 (法向与 extendDir 点积 > 0.1)
                const double dotDir = info.normal.Dot(gp_Vec(extendDir));
                if (dotDir <= 0.1) {
                    continue;
                }

                // 侧面位置在筋板朝向目标板的这一侧 (投影靠近 pntOnA)
                const double posProj = gp_Vec(pntOnA, info.center).Dot(gp_Vec(extendDir));
                if (posProj < -20.0) {
                    continue; // 属于远端背向侧面，跳过
                }

                // 计算该侧面到目标平面的最大距离
                const double dist = CalculateMaxVertexDistanceToPlane(info.face, targetPlane);
                if (dist > 0.15) {
                    facesToExtend.push_back(info);
                    maxRemGap = std::max(maxRemGap, dist);
                }
            }

            qInfo() << "[延长到板] 匹配到待延伸侧端面数:" << facesToExtend.size()
                                          << "最大缝隙:" << maxRemGap << "mm";

            if (facesToExtend.empty() || maxRemGap <= 0.15) {
                qInfo() << "[延长到板] 当前所有侧端面已贴合，保持原样返回。";
                outExtendedPlate = sourcePlate;
                return true;
            }

            // 6. 对 facesToExtend 中的所有侧面统一沿 extendDir 微量超长拉伸
            std::vector<TopoDS_Shape> prisms;
            for (const auto &fa : facesToExtend) {
                const double dist = CalculateMaxVertexDistanceToPlane(fa.face, targetPlane);
                if (dist <= 0.15) continue;

                const double extrudeLen = dist + 1.0; // 穿过间隙并微量穿透目标板 1mm 确保相交
                const gp_Vec extrudeVec = gp_Vec(extendDir) * extrudeLen;

                BRepPrimAPI_MakePrism prismMaker(fa.face, extrudeVec);
                prismMaker.Build();
                if (prismMaker.IsDone() && !prismMaker.Shape().IsNull()) {
                    prisms.push_back(prismMaker.Shape());
                }
            }

            if (prisms.empty()) {
                if (outError) *outError = QStringLiteral("生成拉伸过渡体失败");
                return false;
            }

            // 合并所有拉伸棱柱
            TopoDS_Shape combinedPrisms;
            if (prisms.size() == 1) {
                combinedPrisms = prisms[0];
            } else {
                TopoDS_Compound compound;
                BRep_Builder builder;
                builder.MakeCompound(compound);
                for (const auto &p : prisms) {
                    builder.Add(compound, p);
                }
                combinedPrisms = compound;
            }

            // 7. 布尔运算：
            // 步骤 7.1: 熔合原筋板与微量拉伸体
            BRepAlgoAPI_Fuse fuse(sourcePlate, combinedPrisms);
            fuse.SetFuzzyValue(0.01);
            fuse.Build();
            if (!fuse.IsDone() || fuse.Shape().IsNull()) {
                if (outError) *outError = QStringLiteral("熔合拉伸过渡块失败");
                return false;
            }

            // 步骤 7.2: 直接以目标板 targetPlate 实体执行差集 (Cut)
            BRepAlgoAPI_Cut cut(fuse.Shape(), targetPlate);
            cut.SetFuzzyValue(0.01);
            cut.Build();
            if (!cut.IsDone() || cut.Shape().IsNull()) {
                if (outError) *outError = QStringLiteral("切除目标板干涉体失败");
                return false;
            }

            TopoDS_Shape resultShape = cut.Shape();

            // 步骤 7.3: 以目标基准面剃平多余露头
            try {
                const TopoDS_Shape backsideBox = makeBacksideCutterBox(targetPlane, pntOnB, extendDir, 10000.0, 500.0);
                BRepAlgoAPI_Cut cutPlane(resultShape, backsideBox);
                cutPlane.SetFuzzyValue(0.01);
                cutPlane.Build();
                if (cutPlane.IsDone() && !cutPlane.Shape().IsNull()) {
                    resultShape = cutPlane.Shape();
                }
            } catch (...) {
                // 忽略异常，降级保留 resultShape
            }

            // 步骤 7.4: 拓扑同域净化 (消除侧壁共面接缝)
            try {
                ShapeUpgrade_UnifySameDomain unifier(resultShape, true, true, true);
                unifier.Build();
                const TopoDS_Shape unifiedShape = unifier.Shape();
                if (!unifiedShape.IsNull()) {
                    resultShape = unifiedShape;
                }
            } catch (...) {
                // 忽略净化异常，保留 cut 结果
            }

            outExtendedPlate = resultShape;
            qInfo() << "[延长到板] 成功完成精准延伸与差集贴合！";
            return true;

        } catch (const Standard_Failure &e) {
            if (outError) *outError = QStringLiteral("OCC异常: %1").arg(e.what());
            return false;
        } catch (const std::exception &e) {
            if (outError) *outError = QStringLiteral("标准异常: %1").arg(e.what());
            return false;
        } catch (...) {
            if (outError) *outError = QStringLiteral("未知异常导致延长失败");
            return false;
        }
    }
} // namespace reverse::detail::assembly
