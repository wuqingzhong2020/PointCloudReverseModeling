/*
 * 文件描述：
 * 声明板件几何后处理工具类，提供板件特征提取、延长到板、合并到板及孔洞与边缘修复接口。
*/
#pragma once

#include <QRect>
#include <QString>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include "core/PlateEditing.h"
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include <vector>

namespace reverse::detail::assembly
{
    class PlateGeometryUtils
    {
    public:
        // ------------------- 共享板件几何特征数据结构 -------------------
        struct FacePlaneInfo
        {
            TopoDS_Face face;
            gp_Pln plane;
            gp_Vec normal; // 经 Orientation 纠正的单位外法向
            gp_Pnt center;
            double area = 0.0;
            bool isPlanar = false;
        };

        struct EdgeDirectionCluster
        {
            gp_Dir dir;
            double totalLength = 0.0;
        };

        struct PlatePrincipalFrame
        {
            TopoDS_Face mainFace;
            double mainArea = 0.0;
            gp_Pnt origin;
            gp_Dir dirT; // 板厚方向（主平面法向）
            gp_Dir dirU; // 面内主导直边轴 U
            gp_Dir dirV; // 面内正交直边轴 V = dirT ^ dirU
            std::vector<EdgeDirectionCluster> edgeClusters; // 按平行关系聚类的主平面直边方向簇（按总长度降序）
        };

        // ------------------- 共享板件特征分析与重构基础函数 (实现在 PlateGeometryUtils_Common.cpp) -------------------
        static gp_Pnt CalculateFaceCenter(const TopoDS_Face &face);
        static std::vector<FacePlaneInfo> CollectPlanes(const TopoDS_Shape &shape);
        static double CalculateMaxVertexDistanceToPlane(const TopoDS_Face &face, const gp_Pln &plane);
        static bool ExtractPlatePrincipalFrame(const TopoDS_Shape &shape, PlatePrincipalFrame &outFrame);
        static gp_Trsf MakeNormalLevelingTrsf(const gp_Dir &fromT, const gp_Dir &targetT, const gp_Pnt &pivot);
        static void ComputeShapeProjectedBounds(
            const TopoDS_Shape &shape,
            const gp_Pnt &origin,
            const gp_Dir &dirU,
            const gp_Dir &dirV,
            const gp_Dir &dirT,
            double &uMin, double &uMax,
            double &vMin, double &vMax,
            double &tMin, double &tMax);
        static TopoDS_Face AlignFaceToReferencePlane(
            const PlatePrincipalFrame &plateFrame,
            const gp_Pnt &pivotPnt,
            const gp_Pnt &bottomOrigin,
            const gp_Dir &refU,
            const gp_Dir &refV,
            const gp_Dir &refT,
            double inPlaneAdjustAngleRad = 0.0);
        static TopoDS_Shape ExtrudeAlignedFaceToSolid(
            const TopoDS_Face &alignedFace,
            const gp_Dir &refT,
            double thickness);

        // ------------------- 板件后处理核心算法 -------------------
        /**
         * @brief 将源板件 sourcePlate 沿其朝向目标板件 targetPlate 的侧端面延伸，使其端面完全贴合在目标板表面上
         *        (实现在 PlateGeometryUtils_Extend.cpp)
         */
        static bool ExtendPlateToTarget(
            const TopoDS_Shape &sourcePlate,
            const TopoDS_Shape &targetPlate,
            TopoDS_Shape &outExtendedPlate,
            QString *outError = nullptr);

        /**
         * @brief 将源板件 sourcePlate 合并到目标板件 targetPlate 中，生成共面同厚且保留异形阶梯特征的单一板件实体
         *        (实现在 PlateGeometryUtils_Merge.cpp)
         */
        static bool MergePlateToTarget(
            const TopoDS_Shape &sourcePlate,
            const TopoDS_Shape &targetPlate,
            TopoDS_Shape &outMergedPlate,
            QString *outError = nullptr);

        /**
         * @brief 修复逆向建模板件的内部孔洞与边缘锯齿/小缺口，重构为纯净无孔的板件实体
         *        (实现在 PlateGeometryUtils_Repair.cpp)
         */
        static bool RepairPlateHolesAndBoundary(
            const TopoDS_Shape &sourcePlate,
            TopoDS_Shape &outRepairedPlate,
            int *outRemovedHoles = nullptr,
            int *outSimplifiedVertices = nullptr,
            QString *outError = nullptr);

        /**
         * @brief 根据屏幕 2D 框选区域（物理像素矩形）沿板件正交主轴将板件被框选的一端整端齐头切除
         *        严格禁止切除整块板件，也禁止从板件中间切断
         *        (实现在 PlateGeometryUtils_Crop.cpp)
         */
        static bool CropPlateByScreenBox(
            const TopoDS_Shape &sourcePlate,
            const PlateEdit &request,
            PlateCut &resolvedCut,
            TopoDS_Shape &outCroppedPlate,
            QString *outError = nullptr);

        /**
         * @brief 将源板件 sourcePlate 沿目标板件 targetPlate 剖分切断
         *        - 若完全贯穿且两侧均有实质体积，outResultShapes 返回多个实体（通常为 2 个）；
         *        - 若仅盲插穿入未透，或穿出端极其微小（体积 < 3%），自动剔除残片并返回 1 个齐平贴靠的实体。
         *        (实现在 PlateGeometryUtils_Split.cpp)
         */
        static bool SplitPlateToTarget(
            const TopoDS_Shape &sourcePlate,
            const TopoDS_Shape &targetPlate,
            std::vector<TopoDS_Shape> &outResultShapes,
            QString *outError = nullptr);
    };
} // namespace reverse::detail::assembly

