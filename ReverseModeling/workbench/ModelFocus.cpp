/* 文件描述：优先保留清晰的现有视角；受遮挡或侧立时比较观察方向，取景包含整体以保留位置关系。 */
#include <algorithm>
#include <cmath>
#include <limits>
#include "workbench/ModelFocus.h"
#include "workbench/ModelLabelVisibility.h"

namespace reverse::workbench
{
namespace
{
Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 mul(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b) { return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
Vec3 unit(Vec3 p, Vec3 fallback)
{
    const double length = std::sqrt(dot(p, p));
    return finite(p) && length > 1e-12 ? mul(p, 1.0 / length) : fallback;
}
struct Sample { Vec3 point, normal; double area{}; };
}

std::optional<ModelFocusView> modelFocusView(const std::vector<ModelDisplayPart> &parts,
    const std::string &partId, Vec3 currentTowardEye, Vec3 currentUp, double aspect)
{
    const auto selected = std::find_if(parts.begin(), parts.end(), [&](const auto &p) { return p.id == partId; });
    if (selected == parts.end() || selected->vertices.empty() || !std::isfinite(aspect) || aspect <= 0) return {};
    Vec3 low = selected->vertices.front(), high = low;
    for (const auto &p : selected->vertices)
    {
        if (!finite(p)) return {};
        low = {std::min(low.x,p.x), std::min(low.y,p.y), std::min(low.z,p.z)};
        high = {std::max(high.x,p.x), std::max(high.y,p.y), std::max(high.z,p.z)};
    }
    std::vector<Sample> samples;
    for (const auto &tri : selected->triangles)
    {
        if (tri[0] >= selected->vertices.size() || tri[1] >= selected->vertices.size() || tri[2] >= selected->vertices.size()) return {};
        const auto a = selected->vertices[tri[0]], b = selected->vertices[tri[1]], c = selected->vertices[tri[2]];
        const auto n = cross(sub(b,a), sub(c,a));
        const double area = std::sqrt(dot(n,n));
        if (area > 1e-12) samples.push_back({mul(add(add(a,b),c),1.0/3), mul(n,1/area), area});
    }
    if (samples.empty()) return {};
    // 有界采样，不随高密度曲面三角化数量增加点击时的射线次数。
    std::stable_sort(samples.begin(), samples.end(), [](const auto &a, const auto &b) { return a.area > b.area; });
    if (samples.size() > 32) samples.resize(32);
    const auto current = unit(currentTowardEye, {0,0,1});
    std::vector<Vec3> candidates{current};
    const auto candidate = [&](Vec3 p) {
        p = unit(p, {0,0,1});
        if (std::none_of(candidates.begin(), candidates.end(), [&](Vec3 v) { return dot(v,p) > .995; })) candidates.push_back(p);
    };
    for (const auto &sample : samples) { candidate(sample.normal); candidate(mul(sample.normal,-1)); }
    for (int x=-1; x<=1; ++x) for (int y=-1; y<=1; ++y) for (int z=-1; z<=1; ++z)
        if (x || y || z) candidate({double(x),double(y),double(z)});

    const ModelLabelVisibility visibility(parts);
    const auto score = [&](Vec3 direction) {
        double visibleArea = 0;
        for (const auto &sample : samples)
        {
            const double projection = dot(sample.normal,direction);
            // 独立曲面两面均可观察；实体背面会被自身的前表面射线遮挡。
            const auto normal = projection >= 0 ? sample.normal : mul(sample.normal,-1);
            if (visibility.isVisible({sample.point,normal},direction)) visibleArea += sample.area * std::abs(projection);
        }
        return visibleArea;
    };
    const double currentScore = score(current);
    double bestScore = currentScore;
    Vec3 direction = current;
    for (const auto &p : candidates)
    {
        const double value = score(p);
        if (value > bestScore * 1.001) { bestScore = value; direction = p; }
    }
    if (currentScore >= bestScore * .8) direction = current;
    // 投影旧 Up 到新视平面，避免切换观察方向时无故翻转画面。
    Vec3 up = finite(currentUp) ? sub(currentUp,mul(direction,dot(currentUp,direction))) : Vec3{};
    if (dot(up,up) < 1e-10)
    {
        const Vec3 axis = std::abs(direction.z) < .9 ? Vec3{0,0,1} : Vec3{0,1,0};
        up = sub(axis,mul(direction,dot(axis,direction)));
    }
    up = unit(up,{0,1,0});
    const Vec3 right = cross(up,direction), center = mul(add(low,high),.5);
    double x0 = std::numeric_limits<double>::infinity(), y0 = x0, x1 = -x0, y1 = -x0;
    // 方向仍由选中对象的可见性决定；缩放和画面中心使用整体，避免丢失工件位置参照。
    for (const auto &part : parts)
        for (const auto &p : part.vertices)
        {
            if (!finite(p)) return {};
            const auto delta = sub(p,center);
            const double x = dot(delta,right), y = dot(delta,up);
            x0 = std::min(x0,x); x1 = std::max(x1,x); y0 = std::min(y0,y); y1 = std::max(y1,y);
        }
    return ModelFocusView{add(center,add(mul(right,(x0+x1)*.5),mul(up,(y0+y1)*.5))), direction, up,
                          1.25 * std::max({y1-y0,(x1-x0)/aspect,1e-3})};
}
}
