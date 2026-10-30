/*
 * 文件描述：
 * 缓存显示网格的遮挡几何，判断板厚标注锚点是否朝向相机且未被模型遮蔽。
 */
#pragma once

#include <array>
#include <limits>
#include <vector>

#include "workbench/ModelDisplayData.h"

namespace reverse::workbench
{
class ModelLabelVisibility
{
public:
    explicit ModelLabelVisibility(const std::vector<ModelDisplayPart> &parts);
    [[nodiscard]] bool isVisible(const ModelLabelAnchor &anchor, const Vec3 &towardEye,
        double eyeDistance = std::numeric_limits<double>::infinity()) const;

private:
    struct Surface
    {
        Vec3 minimum;
        Vec3 maximum;
        std::vector<std::array<Vec3, 3>> triangles;
    };
    std::vector<Surface> m_surfaces;
    double m_tolerance{1e-7};
};
}
