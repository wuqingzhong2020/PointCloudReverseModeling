/*
 * 文件描述：
 * 从 CamAssemblyWelding 的同名文件移植，保留原几何算法及阈值；仅适配命名空间、日志与 OCCT 接口。
 * 实现板件框选整端齐头切割算法（PlateGeometryUtils::CropPlateByScreenBox）：
 * 1. 提取板件的主平面与正交主轴系 (dirU, dirV, dirT)，对板件真实几何边界与主表面开展加密采样；
 * 2. 将采样点变换至场景坐标系并通过 V3d_View::Convert 投射至视口物理像素平面，精确统计落入用户框选矩形内的主轴区间；
 * 3. 严格执行两条防呆准则：
 *    - 禁止全切：若框选区域覆盖了整块板件（或切割后剩余体积过小），立即拦截提示；
 *    - 禁止从中间切：框选区域必须包含板件某侧主轴端头且切除后必须保持单一连通 Solid，严禁从中间腰斩或挖槽；
 * 4. 沿板件自身正交主轴构造贯穿全板厚的切除盒（Cutter Box），执行 BRepAlgoAPI_Cut 与 ShapeUpgrade_UnifySameDomain 整端齐头切平。
*/
#include "geometry/assembly/PlateGeometryUtils.h"
#include <QDebug>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepGProp.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRep_Tool.hxx>
#include <GCPnts_UniformAbscissa.hxx>
#include <GProp_GProps.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
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
    enum class AxisCropDecision
    {
        NoneHit,        // 未命中任何采样点
        CutMinEnd,      // 命中最小端 (sMin 端)，整端齐头切至 sHitMax
        CutMaxEnd,      // 命中最大端 (sMax 端)，从 sHitMin 整端齐头切至 sMax
        MiddleCutError, // 仅命中中间段（未触及两端），违背“禁止从中间切”规则
        BothEndsCovered // 两端均被框入，需结合另一正交主轴判定是沿副轴切边还是全切
    };

    AxisCropDecision evaluateAxisCrop(
        double sMin,
        double sMax,
        double sHitMin,
        double sHitMax,
        double &outCutStart,
        double &outCutEnd)
    {
        const double span = sMax - sMin;
        if (span < 1.0 || sHitMax < sHitMin) {
            return AxisCropDecision::NoneHit;
        }

        // 端头判定容差：兼顾点云逆向模型的微小端面折角与采样步长
        const double endTol = std::clamp(span * 0.035, 2.0, 12.0);
        const bool touchesMin = (sHitMin <= sMin + endTol);
        const bool touchesMax = (sHitMax >= sMax - endTol);

        if (touchesMin && touchesMax) {
            return AxisCropDecision::BothEndsCovered;
        }
        if (!touchesMin && !touchesMax) {
            return AxisCropDecision::MiddleCutError;
        }

        const double margin = 50.0;
        if (touchesMin) {
            // 切除左端/最小端：[sMin - margin, sHitMax]
            if (sHitMax - sMin < 1.0) {
                return AxisCropDecision::NoneHit;
            }
            if (sMax - sHitMax < std::max(2.0, span * 0.03)) {
                return AxisCropDecision::BothEndsCovered;
            }
            outCutStart = sMin - margin;
            outCutEnd   = sHitMax;
            return AxisCropDecision::CutMinEnd;
        } else {
            // 切除右端/最大端：[sHitMin, sMax + margin]
            if (sMax - sHitMin < 1.0) {
                return AxisCropDecision::NoneHit;
            }
            if (sHitMin - sMin < std::max(2.0, span * 0.03)) {
                return AxisCropDecision::BothEndsCovered;
            }
            outCutStart = sHitMin;
            outCutEnd   = sMax + margin;
            return AxisCropDecision::CutMaxEnd;
        }
    }
} // namespace

namespace reverse::detail::assembly
{
    bool PlateGeometryUtils::CropPlateByScreenBox(
        const TopoDS_Shape &sourcePlate,
        const PlateEdit &request,
        PlateCut &resolvedCut,
        TopoDS_Shape &outCroppedPlate,
        QString *outError)
    {
        if (sourcePlate.IsNull()) {
            if (outError) *outError = QStringLiteral("输入板件模型或三维视图为空");
            return false;
        }

        QRect normRect;
        if (request.rectangle) {
            const auto &r = *request.rectangle;
            normRect = QRect(QPoint(qRound((r.minX + 1) * request.viewportWidth / 2),
                                    qRound((1 - r.maxY) * request.viewportHeight / 2)),
                             QPoint(qRound((r.maxX + 1) * request.viewportWidth / 2),
                                    qRound((1 - r.minY) * request.viewportHeight / 2))).normalized();
        }
        if (request.rectangle && (normRect.width() < 4 || normRect.height() < 4)) {
            if (outError) *outError = QStringLiteral("框选区域过小，请按住左键拖拽框选要切除的板件一端");
            return false;
        }

        try {
            // 1. 提取板件主平面与正交主轴标架 (origin, dirU, dirV, dirT)
            PlatePrincipalFrame frame;
            if (!ExtractPlatePrincipalFrame(sourcePlate, frame)) {
                if (outError) *outError = QStringLiteral("无法提取板件的主平面与主轴特征");
                return false;
            }

            double uMin = 0.0, uMax = 0.0, vMin = 0.0, vMax = 0.0, tMin = 0.0, tMax = 0.0;
            ComputeShapeProjectedBounds(sourcePlate, frame.origin, frame.dirU, frame.dirV, frame.dirT,
                                        uMin, uMax, vMin, vMax, tMin, tMax);

            // 确保 axis1 恒为板件较长的主轴，axis2 为较短的副轴，且保持右手系 (axis1 ^ axis2 = dirT)
            gp_Dir axis1 = frame.dirU;
            gp_Dir axis2 = frame.dirV;
            double a1Min = uMin, a1Max = uMax;
            double a2Min = vMin, a2Max = vMax;
            if ((vMax - vMin) > (uMax - uMin)) {
                axis1 = frame.dirV;
                axis2 = frame.dirU.Reversed();
                a1Min = vMin;
                a1Max = vMax;
                a2Min = -uMax;
                a2Max = -uMin;
            }

            const gp_Vec vecA1(axis1);
            const gp_Vec vecA2(axis2);
            const gp_Vec vecT(frame.dirT);

            gp_Dir cutAxis = axis1;
            gp_Dir crossAxis = axis2;
            double cutStart = 0.0, cutEnd = 0.0;
            double crossMin = a2Min, crossMax = a2Max;
            if (request.rectangle) {
            // 2. 在板件所有拓扑边与顶点上密集采样真实 3D 点（步长约 2mm），确保 100% 贴合真实板件轮廓
            std::vector<gp_Pnt> samplePoints;
            samplePoints.reserve(1024);

            for (TopExp_Explorer expV(sourcePlate, TopAbs_VERTEX); expV.More(); expV.Next()) {
                samplePoints.push_back(BRep_Tool::Pnt(TopoDS::Vertex(expV.Current())));
            }

            for (TopExp_Explorer expE(sourcePlate, TopAbs_EDGE); expE.More(); expE.Next()) {
                const TopoDS_Edge edge = TopoDS::Edge(expE.Current());
                if (edge.IsNull()) continue;
                try {
                    BRepAdaptor_Curve curve(edge);
                    GCPnts_UniformAbscissa discretizer(curve, 2.0);
                    if (discretizer.IsDone() && discretizer.NbPoints() >= 2) {
                        for (int i = 1; i <= discretizer.NbPoints(); ++i) {
                            samplePoints.push_back(curve.Value(discretizer.Parameter(i)));
                        }
                    }
                } catch (...) {
                }
            }

            // 3. 将采样点变换到场景坐标系并投影至视口物理像素平面，统计落入框选矩形的主轴范围
            double hitA1Min = std::numeric_limits<double>::max();
            double hitA1Max = std::numeric_limits<double>::lowest();
            double hitA2Min = std::numeric_limits<double>::max();
            double hitA2Max = std::numeric_limits<double>::lowest();
            int hitCount = 0;

            for (const gp_Pnt &pOrig : samplePoints) {
                // 主线程提供相机矩阵和物理像素尺寸，工作线程不访问 V3d_View。
                const auto &m = request.rectangle->viewProjection;
                const double w = m[12]*pOrig.X() + m[13]*pOrig.Y() + m[14]*pOrig.Z() + m[15];
                const double x = (m[0]*pOrig.X() + m[1]*pOrig.Y() + m[2]*pOrig.Z() + m[3]) / w;
                const double y = (m[4]*pOrig.X() + m[5]*pOrig.Y() + m[6]*pOrig.Z() + m[7]) / w;
                const int xp = qRound((x + 1) * request.viewportWidth / 2);
                const int yp = qRound((1 - y) * request.viewportHeight / 2);

                if (normRect.contains(static_cast<int>(xp), static_cast<int>(yp))) {
                    const gp_Vec rel(frame.origin, pOrig);
                    const double s1 = rel.Dot(vecA1);
                    const double s2 = rel.Dot(vecA2);
                    hitA1Min = std::min(hitA1Min, s1);
                    hitA1Max = std::max(hitA1Max, s1);
                    hitA2Min = std::min(hitA2Min, s2);
                    hitA2Max = std::max(hitA2Max, s2);
                    ++hitCount;
                }
            }

            if (hitCount == 0) {
                if (outError) {
                    *outError = QStringLiteral("框选区域未包含当前板件的有效边缘，请重新框选要切除的板件一端。");
                }
                return false;
            }

            // 4. 判定切割轴向与切割区间（严格执行：禁止全切、禁止从中间切）

            double candStart1 = 0.0, candEnd1 = 0.0;
            const AxisCropDecision dec1 = evaluateAxisCrop(a1Min, a1Max, hitA1Min, hitA1Max, candStart1, candEnd1);

            if (dec1 == AxisCropDecision::CutMinEnd || dec1 == AxisCropDecision::CutMaxEnd) {
                cutAxis   = axis1;
                crossAxis = axis2;
                cutStart  = candStart1;
                cutEnd    = candEnd1;
                crossMin  = a2Min;
                crossMax  = a2Max;
            } else if (dec1 == AxisCropDecision::MiddleCutError) {
                if (outError) {
                    *outError = QStringLiteral("不能从板件中间切割，请框选板件的一端边缘区域进行整端切除。");
                }
                return false;
            } else if (dec1 == AxisCropDecision::BothEndsCovered) {
                // 长轴两端均被框入时，检查短轴方向是否只框选了一侧边缘
                double candStart2 = 0.0, candEnd2 = 0.0;
                const AxisCropDecision dec2 = evaluateAxisCrop(a2Min, a2Max, hitA2Min, hitA2Max, candStart2, candEnd2);
                if (dec2 == AxisCropDecision::CutMinEnd || dec2 == AxisCropDecision::CutMaxEnd) {
                    cutAxis   = axis2;
                    crossAxis = axis1.Reversed();
                    cutStart  = candStart2;
                    cutEnd    = candEnd2;
                    crossMin  = -a1Max;
                    crossMax  = -a1Min;
                } else if (dec2 == AxisCropDecision::BothEndsCovered) {
                    if (outError) {
                        *outError = QStringLiteral("不能切除整块板件（若需移除整块板件请使用“删除板件”功能）。");
                    }
                    return false;
                } else {
                    if (outError) {
                        *outError = QStringLiteral("不能从板件中间切割，请框选板件的一端边缘区域进行整端切除。");
                    }
                    return false;
                }
            } else {
                if (outError) {
                    *outError = QStringLiteral("框选区域过小或未覆盖板件端头，请重新框选。");
                }
                return false;
            }

            } else {
                if (request.cuts.size() != 1) {
                    if (outError) *outError = QStringLiteral("缺少框选切割平面");
                    return false;
                }
                const auto &c = request.cuts.front();
                cutAxis = gp_Dir(c.normal.x, c.normal.y, c.normal.z);
                crossAxis = frame.dirT.Crossed(cutAxis);
                ComputeShapeProjectedBounds(sourcePlate, frame.origin, cutAxis, crossAxis, frame.dirT,
                                            a1Min, a1Max, crossMin, crossMax, tMin, tMax);
                const double position = gp_Vec(frame.origin, gp_Pnt(c.origin.x,c.origin.y,c.origin.z)).Dot(gp_Vec(cutAxis));
                cutStart = c.positive ? a1Min - 50.0 : position;
                cutEnd = c.positive ? position : a1Max + 50.0;
            }
            const bool keepPositive = request.rectangle ? cutStart <
                (std::abs(gp_Vec(cutAxis).Dot(vecA1)) > .99 ? a1Min : a2Min) : request.cuts.front().positive;
            const auto point = frame.origin.Translated(gp_Vec(cutAxis) * (keepPositive ? cutEnd : cutStart));
            resolvedCut = {request.parts.front(), {point.X(),point.Y(),point.Z()},
                           {cutAxis.X(),cutAxis.Y(),cutAxis.Z()}, keepPositive};

            // 5. 构造沿板件正交主轴贯穿整个宽度与板厚的切除盒（Cutter Box），执行整端齐头切除
            constexpr double kMargin = 50.0;
            const double boxLenS = cutEnd - cutStart;
            const double boxLenC = (crossMax - crossMin) + 2.0 * kMargin;
            const double boxLenT = (tMax - tMin) + 2.0 * kMargin;

            const gp_Pnt boxCorner = frame.origin
                                     .Translated(gp_Vec(cutAxis) * cutStart)
                                     .Translated(gp_Vec(crossAxis) * (crossMin - kMargin))
                                     .Translated(vecT * (tMin - kMargin));

            const gp_Ax2 boxAx2(boxCorner, frame.dirT, cutAxis);
            BRepPrimAPI_MakeBox makeBox(boxAx2, boxLenS, boxLenC, boxLenT);
            makeBox.Build();
            if (!makeBox.IsDone() || makeBox.Shape().IsNull()) {
                if (outError) *outError = QStringLiteral("构造正交切除体失败");
                return false;
            }

            BRepAlgoAPI_Cut cutMaker(sourcePlate, makeBox.Shape());
            cutMaker.SetFuzzyValue(0.01);
            cutMaker.Build();
            if (!cutMaker.IsDone() || cutMaker.Shape().IsNull()) {
                if (outError) *outError = QStringLiteral("布尔切割板件失败");
                return false;
            }

            TopoDS_Shape resultShape = cutMaker.Shape();

            // 6. 拓扑与体积后置校验（确保切除后仍为单一连通实体且未被全切）
            int solidCount = 0;
            for (TopExp_Explorer expS(resultShape, TopAbs_SOLID); expS.More(); expS.Next()) {
                ++solidCount;
            }
            if (solidCount == 0) {
                if (outError) {
                    *outError = QStringLiteral("不能切除整块板件（若需移除整块板件请使用“删除板件”功能）。");
                }
                return false;
            }
            if (solidCount > 1) {
                if (outError) {
                    *outError = QStringLiteral("不能将板件从中间切断为多个独立部分，请重新框选板件的一端。");
                }
                return false;
            }

            GProp_GProps origProps, newProps;
            BRepGProp::VolumeProperties(sourcePlate, origProps);
            BRepGProp::VolumeProperties(resultShape, newProps);
            const double origVol = std::abs(origProps.Mass());
            const double newVol  = std::abs(newProps.Mass());
            if (origVol > 1e-3 && newVol < 0.03 * origVol) {
                if (outError) {
                    *outError = QStringLiteral("切除区域过大（接近全切），请保留板件主体部分。");
                }
                return false;
            }

            try {
                ShapeUpgrade_UnifySameDomain unifier(resultShape, true, true, false);
                unifier.Build();
                if (!unifier.Shape().IsNull()) {
                    resultShape = unifier.Shape();
                }
            } catch (...) {
            }

            outCroppedPlate = resultShape;
            qInfo()
                << QStringLiteral("[框选切割板件] 整端齐头切除成功，原体积: %1 -> 剩余体积: %2")
                   .arg(origVol, 0, 'f', 1).arg(newVol, 0, 'f', 1);
            return true;

        } catch (const Standard_Failure &e) {
            if (outError) *outError = QStringLiteral("OCC异常: %1").arg(e.what());
            return false;
        } catch (const std::exception &e) {
            if (outError) *outError = QStringLiteral("标准异常: %1").arg(e.what());
            return false;
        } catch (...) {
            if (outError) *outError = QStringLiteral("未知异常导致框选切割失败");
            return false;
        }
    }
} // namespace reverse::detail::assembly
