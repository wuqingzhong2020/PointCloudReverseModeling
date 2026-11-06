/* 文件描述：定义工程坐标中的基准面及仅用于显示的点云剖视状态。 */
#pragma once

#include "core/Types.h"

namespace reverse
{
struct ReferencePlane
{
    std::array<Vec3, 3> pointsMm;
    Vec3 normal;
    Vec3 xAxis;
};

struct PlaneSection
{
    bool enabled{};
    double offsetMm{}; // 沿基准面原始法线偏移，反向仅切换保留侧。
    bool reversed{};
    bool operator==(const PlaneSection &) const = default;
};
}
