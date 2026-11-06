/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * - 实现板件分割算法（SplitPlateToTarget）。
 * - 核心业务需求：
 *   切割后剩下的两块零件（或盲插时的一块零件）与目标板绝对没有重合，并且端面与目标板外表面
 *   紧密共面贴合，能正常交出贴角焊缝（效果相当于从两侧各自延长贴靠到目标板表面）。
 * - 算法原理：
 *   1. 提取源板的长度主延伸方向与目标板的板厚法向；
 *   2. 确定目标板在板厚方向的两个外表面平面（P_front 与 P_back）；
 *   3. 根据源板的跨越范围构造两道超大剖切面，利用 BRepAlgoAPI_Splitter 对源板进行双切面剖切；
 *   4. 自动剔除落在目标板板厚内部的干涉实体（让出目标板厚度空间），保留外部的独立零件；
 *   5. 对保留零件进行残余接触面净化（UnifySameDomain），确保两块板件端面与目标板零重叠、齐平贴合出焊缝。
*/
#include "geometry/assembly/PlateGeometryUtils.h"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Splitter.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_OBB.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <NCollection_List.hxx>
#include <gp_Ax3.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <TopoDS.hxx>
#include <limits>
#include <vector>

namespace reverse::detail::assembly
{
    namespace
    {
        double CalculateShapeVolume(const TopoDS_Shape &shape)
        {
            if (shape.IsNull()) return 0.0;
            GProp_GProps gprops;
            BRepGProp::VolumeProperties(shape, gprops);
            return std::max(0.0, gprops.Mass());
        }

        gp_Pnt CalculateShapeCenter(const TopoDS_Shape &shape)
        {
            if (shape.IsNull()) return gp_Pnt();
            GProp_GProps gprops;
            BRepGProp::VolumeProperties(shape, gprops);
            if (gprops.Mass() > 1e-6) {
                return gprops.CentreOfMass();
            }
            // 兜底：若体积极小，使用顶点均值
            double x = 0, y = 0, z = 0;
            int count = 0;
            for (TopExp_Explorer exp(shape, TopAbs_VERTEX); exp.More(); exp.Next()) {
                const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                x += p.X(); y += p.Y(); z += p.Z();
                count++;
            }
            if (count > 0) {
                return gp_Pnt(x / count, y / count, z / count);
            }
            return gp_Pnt();
        }

        TopoDS_Shape UnifyShape(const TopoDS_Shape &shape)
        {
            if (shape.IsNull()) return shape;
            try {
                ShapeUpgrade_UnifySameDomain unifier(shape, true, true, false);
                unifier.Build();
                return unifier.Shape();
            } catch (...) {
                return shape;
            }
        }

        // 提取形状的最长主延伸轴方向（长度轴）
        gp_Dir ExtractShapeLongitudinalDirection(const TopoDS_Shape &shape)
        {
            Bnd_OBB obb;
            BRepBndLib::AddOBB(shape, obb);
            gp_Dir longDir = obb.XDirection();
            double maxHSize = obb.XHSize();
            if (obb.YHSize() > maxHSize) {
                maxHSize = obb.YHSize();
                longDir = obb.YDirection();
            }
            if (obb.ZHSize() > maxHSize) {
                maxHSize = obb.ZHSize();
                longDir = obb.ZDirection();
            }
            return longDir;
        }

        // 构造在空间中大幅包覆被切割形状的超大切面
        TopoDS_Face CreateEncompassingCutterFace(const gp_Pln &cutterPln, const TopoDS_Shape &targetBoundsShape)
        {
            const gp_Ax3 &pos = cutterPln.Position();
            const gp_Pnt &loc = pos.Location();
            const gp_Vec vecX(pos.XDirection());
            const gp_Vec vecY(pos.YDirection());

            double realUMin = std::numeric_limits<double>::max();
            double realUMax = std::numeric_limits<double>::lowest();
            double realVMin = std::numeric_limits<double>::max();
            double realVMax = std::numeric_limits<double>::lowest();

            for (TopExp_Explorer exp(targetBoundsShape, TopAbs_VERTEX); exp.More(); exp.Next()) {
                const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
                const gp_Vec rel(loc, p);
                const double u = rel.Dot(vecX);
                const double v = rel.Dot(vecY);
                realUMin = std::min(realUMin, u);
                realUMax = std::max(realUMax, u);
                realVMin = std::min(realVMin, v);
                realVMax = std::max(realVMax, v);
            }

            const double marginU = std::max(300.0, (realUMax - realUMin) * 0.8);
            const double marginV = std::max(300.0, (realVMax - realVMin) * 0.8);

            return BRepBuilderAPI_MakeFace(
                cutterPln,
                realUMin - marginU, realUMax + marginU,
                realVMin - marginV, realVMax + marginV).Face();
        }
    } // namespace

    bool PlateGeometryUtils::SplitPlateToTarget(
        const TopoDS_Shape &sourcePlate,
        const TopoDS_Shape &targetPlate,
        std::vector<TopoDS_Shape> &outResultShapes,
        QString *outError)
    {
        outResultShapes.clear();

        if (sourcePlate.IsNull()) {
            if (outError) *outError = QStringLiteral("源板件几何为空");
            return false;
        }
        if (targetPlate.IsNull()) {
            if (outError) *outError = QStringLiteral("目标板件几何为空");
            return false;
        }

        const double origVolume = CalculateShapeVolume(sourcePlate);
        if (origVolume <= 1e-4) {
            if (outError) *outError = QStringLiteral("源板件体积无效");
            return false;
        }

        // 1. 检查两板最短距离（超过 50mm 无法进行装配关联分割）
        BRepExtrema_DistShapeShape distAlgo(sourcePlate, targetPlate);
        distAlgo.Perform();
        if (distAlgo.IsDone() && distAlgo.Value() > 50.0) {
            if (outError) *outError = QStringLiteral("两块板件相距过远（>50mm），无法进行关联分割");
            return false;
        }

        // 2. 提取源板的长度主延伸方向
        const gp_Dir sourceLongDir = ExtractShapeLongitudinalDirection(sourcePlate);
        const gp_Vec vecSourceLong(sourceLongDir);

        // 3. 确定目标板的板厚方向（主法向）与厚度区间 [tMinB, tMaxB]
        // 横向肋板的板厚法向必须与源板长度方向基本平行（|dot| >= 0.25）
        gp_Dir normalB = sourceLongDir;
        gp_Pnt originB = CalculateShapeCenter(targetPlate);
        bool normalFound = false;

        // 优先查看目标板的主特征面
        PlatePrincipalFrame targetFrame;
        if (ExtractPlatePrincipalFrame(targetPlate, targetFrame)) {
            const double align = std::abs(gp_Vec(targetFrame.dirT).Dot(vecSourceLong));
            if (align >= 0.25) {
                normalB = targetFrame.dirT;
                originB = targetFrame.origin;
                normalFound = true;
            }
        }

        // 若主特征面未对齐（如长宽相近或倒角影响），从目标板所有平面中寻找最平行于源板长度的表面
        if (!normalFound) {
            const auto allPlanes = CollectPlanes(targetPlate);
            double bestAlign = -1.0;
            for (const auto &plnInfo : allPlanes) {
                if (plnInfo.normal.Magnitude() < 1e-6) continue;
                const double align = std::abs(plnInfo.normal.Dot(vecSourceLong));
                if (align > bestAlign) {
                    bestAlign = align;
                    normalB = gp_Dir(plnInfo.normal);
                    originB = plnInfo.center;
                }
            }
            if (bestAlign >= 0.25) {
                normalFound = true;
            }
        }

        if (!normalFound) {
            if (outError) *outError = QStringLiteral("未能在目标板上找到对齐源板延伸方向的横切表面");
            return false;
        }

        // 确保 normalB 始终与 sourceLongDir 同向
        if (gp_Vec(normalB).Dot(vecSourceLong) < 0.0) {
            normalB.Reverse();
        }
        const gp_Vec vecNormalB(normalB);

        // 计算目标板在自身板厚法向上的投影范围 [tMinB, tMaxB]
        double tMinB = std::numeric_limits<double>::max();
        double tMaxB = std::numeric_limits<double>::lowest();
        for (TopExp_Explorer exp(targetPlate, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            const double t = gp_Vec(originB, p).Dot(vecNormalB);
            tMinB = std::min(tMinB, t);
            tMaxB = std::max(tMaxB, t);
        }

        // 计算源板在 normalB 方向上的投影范围 [tMinA, tMaxA]
        double tMinA = std::numeric_limits<double>::max();
        double tMaxA = std::numeric_limits<double>::lowest();
        for (TopExp_Explorer exp(sourcePlate, TopAbs_VERTEX); exp.More(); exp.Next()) {
            const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(exp.Current()));
            const double t = gp_Vec(originB, p).Dot(vecNormalB);
            tMinA = std::min(tMinA, t);
            tMaxA = std::max(tMaxA, t);
        }

        // 4. 判断切割表面与剖切策略：
        // 目标板在 normalB 方向的两个物理外表面：
        // - 表面 1 (t = tMinB)：对应目标板的负侧外表面
        // - 表面 2 (t = tMaxB)：对应目标板的正侧外表面
        // 两表面之间的 [tMinB, tMaxB] 区间就是目标板自身占用的板厚厚度空间！
        const bool crossesFace1 = (tMinA < tMinB - 0.5 && tMaxA > tMinB + 0.5);
        const bool crossesFace2 = (tMinA < tMaxB - 0.5 && tMaxA > tMaxB + 0.5);

        NCollection_List<TopoDS_Shape> splitterTools;

        if (crossesFace1 && crossesFace2) {
            // 4.1 贯穿分割：源板完全跨过了目标板的整个厚度区间！
            // 构造两个剖切面（P_min 与 P_max），分别对齐目标板的两侧外表面
            const gp_Pnt ptFace1 = originB.Translated(vecNormalB * tMinB);
            const gp_Pln plnFace1(ptFace1, normalB);
            splitterTools.Append(CreateEncompassingCutterFace(plnFace1, sourcePlate));

            const gp_Pnt ptFace2 = originB.Translated(vecNormalB * tMaxB);
            const gp_Pln plnFace2(ptFace2, normalB);
            splitterTools.Append(CreateEncompassingCutterFace(plnFace2, sourcePlate));
        } else if (crossesFace1) {
            // 4.2 负侧盲插/贴合：源板穿过了负侧外表面，但未穿透正侧外表面
            const gp_Pnt ptFace1 = originB.Translated(vecNormalB * tMinB);
            const gp_Pln plnFace1(ptFace1, normalB);
            splitterTools.Append(CreateEncompassingCutterFace(plnFace1, sourcePlate));
        } else if (crossesFace2) {
            // 4.3 正侧盲插/贴合：源板穿过了正侧外表面，但未穿透负侧外表面
            const gp_Pnt ptFace2 = originB.Translated(vecNormalB * tMaxB);
            const gp_Pln plnFace2(ptFace2, normalB);
            splitterTools.Append(CreateEncompassingCutterFace(plnFace2, sourcePlate));
        } else {
            // 4.4 两板处于微小缝隙状态，使用中分面或最邻近表面延展切断
            const double tMidB = (tMinB + tMaxB) * 0.5;
            if (tMinA < tMidB - 0.5 && tMaxA > tMidB + 0.5) {
                const gp_Pnt ptMid = originB.Translated(vecNormalB * tMidB);
                const gp_Pln plnMid(ptMid, normalB);
                splitterTools.Append(CreateEncompassingCutterFace(plnMid, sourcePlate));
            } else {
                if (outError) *outError = QStringLiteral("目标板的外表面延展面未能切断源板件");
                return false;
            }
        }

        // 5. 执行拓扑剖切（BRepAlgoAPI_Splitter）
        NCollection_List<TopoDS_Shape> args;
        args.Append(sourcePlate);

        BRepAlgoAPI_Splitter splitter;
        splitter.SetArguments(args);
        splitter.SetTools(splitterTools);
        splitter.Build();

        if (!splitter.IsDone() || splitter.Shape().IsNull()) {
            if (outError) *outError = QStringLiteral("剖切计算失败");
            return false;
        }

        std::vector<TopoDS_Shape> splitSolids;
        for (TopExp_Explorer exp(splitter.Shape(), TopAbs_SOLID); exp.More(); exp.Next()) {
            const TopoDS_Shape &solid = exp.Current();
            if (CalculateShapeVolume(solid) > 10.0) {
                splitSolids.push_back(solid);
            }
        }

        if (splitSolids.empty()) {
            if (outError) *outError = QStringLiteral("剖切后未生成有效实体");
            return false;
        }

        // 6. 核心重叠过滤与零件分类：
        // 关键物理规则：落在目标板板厚内部区间 [tMinB, tMaxB] 的实体属于干涉实体，必须剔除！
        // 只有位于两表面外侧的实体才是切除后合法保留的零件！
        std::vector<TopoDS_Shape> validLeftParts;  // 负侧零件 (t < tMinB)
        std::vector<TopoDS_Shape> validRightParts; // 正侧零件 (t > tMaxB)
        std::vector<TopoDS_Shape> insideInterferenceParts; // 目标板厚度内部的干涉实体 (丢弃)

        for (const auto &solid : splitSolids) {
            const gp_Pnt center = CalculateShapeCenter(solid);
            const double tCenter = gp_Vec(originB, center).Dot(vecNormalB);
            const double vol = CalculateShapeVolume(solid);

            // 忽略微小噪点实体
            if (vol < 200.0 && (vol / origVolume < 0.03)) {
                continue;
            }

            if (crossesFace1 && crossesFace2) {
                // 贯穿模式：落在 [tMinB, tMaxB] 之间的为干涉实体，剔除
                if (tCenter < tMinB + 0.1) {
                    validLeftParts.push_back(solid);
                } else if (tCenter > tMaxB - 0.1) {
                    validRightParts.push_back(solid);
                } else {
                    insideInterferenceParts.push_back(solid);
                }
            } else if (crossesFace1) {
                // 仅切穿 Face1
                if (tCenter < tMinB) {
                    validLeftParts.push_back(solid);
                } else {
                    insideInterferenceParts.push_back(solid);
                }
            } else if (crossesFace2) {
                // 仅切穿 Face2
                if (tCenter > tMaxB) {
                    validRightParts.push_back(solid);
                } else {
                    insideInterferenceParts.push_back(solid);
                }
            } else {
                // 单面剖切兜底
                validLeftParts.push_back(solid);
            }
        }

        // 收集保留零件
        std::vector<TopoDS_Shape> retainedParts;
        retainedParts.insert(retainedParts.end(), validLeftParts.begin(), validLeftParts.end());
        retainedParts.insert(retainedParts.end(), validRightParts.begin(), validRightParts.end());

        if (retainedParts.empty()) {
            if (outError) *outError = QStringLiteral("切割后未能保留有效板件实体");
            return false;
        }

        // 若单侧生成了多个碎片，按体积降序排列，仅保留主要工程零件
        std::sort(retainedParts.begin(), retainedParts.end(),
                  [](const TopoDS_Shape &s1, const TopoDS_Shape &s2) {
                      return CalculateShapeVolume(s1) > CalculateShapeVolume(s2);
                  });

        // 7. 对保留的零件做安全干涉消除与同域面净化
        for (const auto &part : retainedParts) {
            // 对每个保留零件，如果与目标板仍有微米级实体干涉，做微量布尔差集确保 100% 零重合
            TopoDS_Shape cleanPart = part;
            try {
                BRepAlgoAPI_Cut cutInterf(part, targetPlate);
                cutInterf.Build();
                if (cutInterf.IsDone() && !cutInterf.Shape().IsNull()) {
                    for (TopExp_Explorer exp(cutInterf.Shape(), TopAbs_SOLID); exp.More(); exp.Next()) {
                        const TopoDS_Shape &subSolid = exp.Current();
                        if (CalculateShapeVolume(subSolid) > CalculateShapeVolume(cleanPart) * 0.95) {
                            cleanPart = subSolid;
                            break;
                        }
                    }
                }
            } catch (...) {
                // 若差集异常，保持剖切原样
            }

            outResultShapes.push_back(UnifyShape(cleanPart));
        }

        if (outResultShapes.empty()) {
            if (outError) *outError = QStringLiteral("目标板未能在有效位置切断源板件");
            return false;
        }

        return true;
    }
} // namespace reverse::detail::assembly
