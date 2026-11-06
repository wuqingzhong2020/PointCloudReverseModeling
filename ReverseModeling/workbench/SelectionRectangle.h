/* 文件描述：管理屏幕框选的边角命中、创建、移动和缩放，不依赖窗口或 OCCT。 */
#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <Qt>

namespace reverse::workbench
{
enum class RectangleHandle
{
    None, Move, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight
};

class SelectionRectangle
{
  public:
    const QRectF &rect() const;
    RectangleHandle hitTest(const QPointF &point, double tolerance) const;
    static Qt::CursorShape cursor(RectangleHandle handle);
    void beginCreate(const QPointF &point);
    bool beginEdit(const QPointF &point, double tolerance);
    void update(const QPointF &point, const QSizeF &bounds, double minimumExtent);
    bool dragging() const;
    bool editing() const;
    void finish();
    void clear();

  private:
    QRectF m_rect;
    QRectF m_initial;
    QPointF m_anchor;
    RectangleHandle m_handle{RectangleHandle::None};
    bool m_creating{};
};
} // namespace reverse::workbench
