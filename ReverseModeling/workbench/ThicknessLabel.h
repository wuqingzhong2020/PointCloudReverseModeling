/*
 * 文件描述：板厚标签文字与屏幕命中数据，供 OCCT 显示、就地编辑和单元测试复用。
 */
#pragma once

#include <optional>
#include <span>
#include <string>

#include <QPointF>
#include <QRect>
#include <QString>

namespace reverse::workbench
{
struct ThicknessLabelTarget
{
    std::string partId;
    double thicknessMm{};
    QRect pixelRect; // 与 OCCT 标签布局一致的设备像素矩形，仅保存可见数值标签。
};

[[nodiscard]] QString thicknessLabelText(std::optional<double> thicknessMm);
// Qt 逻辑坐标转换为设备像素；不命中隐藏、未识别或无效标签。
[[nodiscard]] std::optional<ThicknessLabelTarget> hitThicknessLabel(
    std::span<const ThicknessLabelTarget> labels, const QPointF &position, double pixelRatio);
}
