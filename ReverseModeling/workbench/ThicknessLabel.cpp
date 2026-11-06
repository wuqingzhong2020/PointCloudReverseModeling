/*
 * 文件描述：统一格式化毫米板厚标签，并按布局后的真实矩形进行高 DPI 命中检测。
 * 标签只显示数值，不改变底板估算来源等业务元数据；未知厚度保持“未识别”。
 */
#include <cmath>

#include "workbench/ThicknessLabel.h"

namespace reverse::workbench
{
QString thicknessLabelText(std::optional<double> thicknessMm)
{
    if (!thicknessMm || !std::isfinite(*thicknessMm) || *thicknessMm <= 0)
    {
        return QStringLiteral("未识别");
    }
    return QString::number(*thicknessMm, 'f', 1) + QStringLiteral(" mm");
}

std::optional<ThicknessLabelTarget> hitThicknessLabel(
    std::span<const ThicknessLabelTarget> labels, const QPointF &position, double pixelRatio)
{
    if (!std::isfinite(pixelRatio) || pixelRatio <= 0 ||
        !std::isfinite(position.x()) || !std::isfinite(position.y()))
    {
        return std::nullopt;
    }
    const QPointF pixel = position * pixelRatio;
    for (const auto &label : labels)
    {
        if (!label.partId.empty() && std::isfinite(label.thicknessMm) && label.thicknessMm > 0 &&
            !label.pixelRect.isEmpty() && QRectF(label.pixelRect).contains(pixel))
        {
            return label;
        }
    }
    return std::nullopt;
}
}
