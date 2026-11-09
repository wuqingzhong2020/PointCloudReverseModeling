/* 文件描述：直接与批量板件编辑的完整输入，所有坐标为工程坐标 mm，不依赖当前选择或相机。 */
#pragma once
#include "core/Types.h"

namespace reverse
{
struct EndCapEdge
{
    Vec3 position;
    std::string wing;
    double automaticDistance{};
};
struct EndCapInspection
{
    std::string part, web;
    std::array<EndCapEdge, 2> edges;
};
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
    std::string operation; // extend / trim / merge / split / crop / repair / holes / cross / cap
    std::vector<std::string> parts;
    std::string target;
    std::vector<PlateContact> contacts;
    std::vector<PlateCut> cuts;
    std::vector<PlateHole> holes;
    std::optional<CloudSelectionRect> rectangle; // 仅框选解析使用；提交和录制使用 cuts。
    double tolerance{0.15}, maxDistance{50}, clearance{}, normalAngle{1}, inplaneAngle{1}, maxGap{5};
    double maxHoleArea{}, notchWidth{35}, notchDepth{20}, simplifyTolerance{0.01};
    double heightOffset{10}; // cross：A 顶面高于 B 的距离；0 不调整高度。
    int capEdge{1}; // cap：稳定的横向两侧，1-based；一次仅修改当前边缘。
    bool capConvex{}, capAutomatic{true};
    double capDistance{15}; // 从该侧翼板外表面量取；错边向内、凸边向外。
    bool boundary{};
    bool direct{}, all{}; // 菜单直接操作采用 CamAssemblyWelding 的固定规则。
    int viewportWidth{}, viewportHeight{}; // 仅框选解析使用，不进入回放。
};
} // namespace reverse
