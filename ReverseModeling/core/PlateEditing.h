/* 文件描述：批量板件编辑的完整输入，所有坐标为工程坐标 mm，不依赖当前选择或相机。 */
#pragma once
#include "core/Types.h"

namespace reverse
{
struct PlateCut
{
    std::string part;
    Vec3 origin, normal;
    bool positive{};
};
struct PlateContact
{
    std::string part;
    std::uint32_t face{}; // 目标 BRep 面编号；0 在预览时解析最近主面。
};
struct PlateEdit
{
    std::string operation; // extend / trim / merge / split / crop / repair / holes
    std::vector<std::string> parts;
    std::string target;
    std::vector<PlateContact> contacts;
    std::vector<PlateCut> cuts;
    std::vector<PlateHole> holes;
    std::optional<CloudSelectionRect> rectangle; // 仅预览准备使用；应用录制为 cuts。
    double tolerance{0.15}, maxDistance{50}, clearance{}, normalAngle{1}, inplaneAngle{1}, maxGap{5};
    double maxHoleArea{}, notchWidth{35}, notchDepth{20}, simplifyTolerance{0.01};
    bool boundary{};
};
} // namespace reverse
