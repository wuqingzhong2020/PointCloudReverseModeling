/*
 * 文件描述：以视口中心定位 OCCT 二维框选框，避免宽视口的二维视场限制使显示框偏离鼠标选区。
 * 输入与框选计算共用物理像素坐标；仅适配显示，不改变点云选择范围。
 */
#pragma once

#include <QRectF>
#include <QSize>

#include <AIS_RubberBand.hxx>
#include <Graphic3d_TransformPers.hxx>

namespace reverse::workbench
{
inline void setSelectionBandRectangle(AIS_RubberBand &band, const QRectF &rectangle, const QSize &viewport)
{
    // 默认左下角锚点受 Camera::NDC2dOffsetX/Y 限制；正交宽视口同样会产生额外偏移。
    // 中心锚点不使用该限制，再将物理像素转换为中心坐标，保证显示框与命中计算一致。
    const auto &persistence = band.TransformPersistence();
    if (persistence.IsNull() || persistence->Mode() != Graphic3d_TMF_2d ||
        persistence->Corner2d() != Aspect_TOTP_CENTER)
    {
        band.SetTransformPersistence(new Graphic3d_TransformPers(Graphic3d_TMF_2d, Aspect_TOTP_CENTER));
    }
    const double halfWidth = viewport.width() / 2.0;
    const double halfHeight = viewport.height() / 2.0;
    band.SetRectangle(qRound(rectangle.left() - halfWidth), qRound(halfHeight - rectangle.bottom()),
                      qRound(rectangle.right() - halfWidth), qRound(halfHeight - rectangle.top()));
}
}
