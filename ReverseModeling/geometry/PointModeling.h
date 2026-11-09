/* 文件描述：选点轮廓、拉伸及参考面障碍物包络的独立几何规则。 */
#pragma once
#include "geometry/Geometry.h"
#include <TopoDS_Shape.hxx>

namespace reverse
{
struct PointModeling
{
    std::string shape{"rectangle"}, method{"axis"};
    std::vector<Vec3> points; // 工程坐标，按用户点选顺序。
    Vec3 origin{}, normal{0, 0, 1}, xAxis{1, 0, 0};
    double width{20}, length{}, diameter{}, height{100}; // 0 长度/直径表示按选点计算。
    bool toPlane{}; // 轮廓位于选点高度，沿法线反向拉到参考面。
    bool cut{};
    std::string cutScope{"no_base"};
    std::vector<std::string> targets;
    CloudSelection indices; // box 使用完整原始点云索引，命令录制后可重放。
    bool section{true}, reversed{};
    double offset{10};
};
struct PointProfile
{
    std::vector<Vec3> contour;
    Vec3 center{}, extrusion{};
    double length{}, width{}, diameter{}, height{};
    bool circle{};
};
[[nodiscard]] PointProfile pointModelProfile(const PointModeling &request);
namespace detail
{
[[nodiscard]] TopoDS_Shape pointModelShape(const PointModeling &request);
}
[[nodiscard]] std::shared_ptr<const Model> buildModelByPoints(const Model *source, const PointModeling &request,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
}
