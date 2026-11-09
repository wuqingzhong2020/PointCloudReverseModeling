/* 文件描述：四圆角矩形围板的顶边测厚与等厚内外环重建，保持中间轮廓和高度。 */
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "geometry/OtherModelRecognition.h"

namespace reverse::detail
{
namespace
{
struct Loop
{
    double x0{}, x1{}, z0{}, z1{}, radius{};
};

std::optional<Loop> rectangle(const std::vector<PlateBoundarySegment> &edges)
{
    if (edges.size() != 8) return {};
    Loop box{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(), 0};
    std::vector<double> radii;
    for (const auto &edge : edges)
    {
        for (const auto p : {edge.firstMm, edge.lastMm})
        {
            box.x0 = std::min(box.x0, p.x); box.x1 = std::max(box.x1, p.x);
            box.z0 = std::min(box.z0, p.z); box.z1 = std::max(box.z1, p.z);
        }
        const double dx = std::abs(edge.lastMm.x - edge.firstMm.x);
        const double dz = std::abs(edge.lastMm.z - edge.firstMm.z);
        if (edge.arcMiddleMm)
        {
            if (dx < 1e-5 || std::abs(dx - dz) > 1e-5) return {};
            // 圆弧中点必须是四分之一圆的中点，不能把普通曲边当作圆角。
            const auto m = *edge.arcMiddleMm;
            const double chordSag = std::hypot(m.x - (edge.firstMm.x + edge.lastMm.x) / 2,
                m.z - (edge.firstMm.z + edge.lastMm.z) / 2);
            if (std::abs(chordSag - dx * (1 - 1 / std::sqrt(2.0))) > 1e-5) return {};
            radii.push_back(dx);
        }
        else if (std::min(dx, dz) > 1e-5 || std::max(dx, dz) < 1e-5) return {};
    }
    if (radii.size() != 4) return {};
    box.radius = radii.front();
    for (const auto radius : radii)
        if (std::abs(radius - box.radius) > 1e-5) return {};
    return box;
}

std::optional<std::array<Loop, 2>> enclosure(const RecognizedPlate &plate)
{
    if (plate.uprightFootprint.size() != 2) return {};
    const auto outer = rectangle(plate.uprightFootprint[0]);
    const auto inner = rectangle(plate.uprightFootprint[1]);
    if (!outer || !inner) return {};
    const std::array widths{inner->x0 - outer->x0, outer->x1 - inner->x1,
        inner->z0 - outer->z0, outer->z1 - inner->z1};
    const auto [low, high] = std::minmax_element(widths.begin(), widths.end());
    // 初次栅格轮廓允许少量离散误差；后续重建统一为严格等厚。
    if (*low <= 0 || *high - *low > std::max(3.0, *low * 0.3) ||
        std::abs(outer->radius - inner->radius - (*low + *high) / 2) > std::max(4.0, *high * 0.4)) return {};
    return std::array{*outer, *inner};
}

std::vector<PlateBoundarySegment> wire(Loop b, double y, bool reversed)
{
    const double r = b.radius, d = r / std::sqrt(2.0);
    const std::array<Vec3, 8> p{{{b.x0 + r, y, b.z0}, {b.x1 - r, y, b.z0},
        {b.x1, y, b.z0 + r}, {b.x1, y, b.z1 - r}, {b.x1 - r, y, b.z1},
        {b.x0 + r, y, b.z1}, {b.x0, y, b.z1 - r}, {b.x0, y, b.z0 + r}}};
    std::vector<PlateBoundarySegment> result{{p[0], p[1], {}},
        {p[1], p[2], Vec3{b.x1 - r + d, y, b.z0 + r - d}}, {p[2], p[3], {}},
        {p[3], p[4], Vec3{b.x1 - r + d, y, b.z1 - r + d}}, {p[4], p[5], {}},
        {p[5], p[6], Vec3{b.x0 + r - d, y, b.z1 - r + d}}, {p[6], p[7], {}},
        {p[7], p[0], Vec3{b.x0 + r - d, y, b.z0 + r - d}}};
    if (reversed)
    {
        std::reverse(result.begin(), result.end());
        for (auto &edge : result) std::swap(edge.firstMm, edge.lastMm);
    }
    return result;
}

double quantile(std::vector<double> values, double q)
{
    std::sort(values.begin(), values.end());
    return values[static_cast<std::size_t>(q * (values.size() - 1))];
}
}

std::optional<PlateThicknessInfo> enclosureThickness(const RecognizedPlate &plate)
{
    const auto loops = enclosure(plate);
    if (!loops) return {};
    const auto &a = (*loops)[0], &b = (*loops)[1];
    const double thickness = ((b.x0 - a.x0) + (a.x1 - b.x1) + (b.z0 - a.z0) + (a.z1 - b.z1)) / 4;
    return PlateThicknessInfo{thickness,
        {(a.x0 + b.x0) / 2, (plate.minimumMm.y + plate.maximumMm.y) / 2, (a.z0 + a.z1 + b.z0 + b.z1) / 4},
        {-1, 0, 0}};
}

void resizeEnclosure(RecognizedPlate &plate, double thicknessMm)
{
    const auto loops = enclosure(plate);
    if (!loops) throw GeometryError("unsupported_enclosure", "当前连接组不是规则四圆角矩形围板，无法统一修改板厚。");
    const auto &a = (*loops)[0], &b = (*loops)[1];
    const Loop middle{(a.x0 + b.x0) / 2, (a.x1 + b.x1) / 2, (a.z0 + b.z0) / 2,
        (a.z1 + b.z1) / 2, (a.radius + b.radius) / 2};
    const double half = thicknessMm / 2;
    if (!std::isfinite(half) || half <= 0 || half >= middle.radius - 1e-4 ||
        thicknessMm >= std::min(middle.x1 - middle.x0, middle.z1 - middle.z0))
        throw GeometryError("invalid_thickness", "围板板厚过大，内圆角或中空区域会消失，未应用修改。");
    const Loop outer{middle.x0 - half, middle.x1 + half, middle.z0 - half, middle.z1 + half, middle.radius + half};
    const Loop inner{middle.x0 + half, middle.x1 - half, middle.z0 + half, middle.z1 - half, middle.radius - half};
    plate.uprightFootprint = {wire(outer, plate.minimumMm.y, false), wire(inner, plate.minimumMm.y, true)};
    plate.minimumMm.x = outer.x0; plate.minimumMm.z = outer.z0;
    plate.maximumMm.x = outer.x1; plate.maximumMm.z = outer.z1;
    plate.thicknessMm = thicknessMm;
}

bool recognizeEnclosureThickness(RecognizedPlate &plate, const std::vector<Vec3> &points, double tolerance)
{
    const auto loops = enclosure(plate);
    if (!loops) return false;
    const auto &inner = (*loops)[1];
    const double cx = (inner.x0 + inner.x1) / 2, cz = (inner.z0 + inner.z1) / 2;
    const double sx = (inner.x1 - inner.x0) / 2 - inner.radius;
    const double sz = (inner.z1 - inner.z0) / 2 - inner.radius;
    std::array<std::vector<double>, 4> bands;
    for (const auto &p : points)
    {
        if (std::abs(p.z - cz) < sz * 0.7)
        {
            if (p.x < inner.x0 + tolerance) bands[0].push_back(p.x);
            if (p.x > inner.x1 - tolerance) bands[1].push_back(p.x);
        }
        if (std::abs(p.x - cx) < sx * 0.7)
        {
            if (p.z < inner.z0 + tolerance) bands[2].push_back(p.z);
            if (p.z > inner.z1 - tolerance) bands[3].push_back(p.z);
        }
    }
    std::vector<double> widths;
    for (const auto &band : bands)
    {
        if (band.size() < 20) return false;
        widths.push_back(quantile(band, 0.995) - quantile(band, 0.005));
    }
    const double thickness = (quantile(widths, 0.5) + quantile(widths, 0.75)) / 2;
    const double spread = *std::max_element(widths.begin(), widths.end()) - *std::min_element(widths.begin(), widths.end());
    if (thickness < tolerance || spread > std::max(tolerance, thickness * 0.2)) return false;
    resizeEnclosure(plate, thickness);
    plate.thicknessSpreadMm = spread;
    plate.method = "other_rounded_enclosure_top_width";
    return true;
}
}
