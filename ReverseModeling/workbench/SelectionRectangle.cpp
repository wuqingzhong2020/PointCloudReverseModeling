/* 文件描述：按屏幕像素命中四边、四角和内部；拖动固定对边，移动保持尺寸，并限制在视口内。 */
#include <algorithm>
#include <cmath>

#include "workbench/SelectionRectangle.h"

namespace reverse::workbench
{
const QRectF &SelectionRectangle::rect() const { return m_rect; }

RectangleHandle SelectionRectangle::hitTest(const QPointF &point, double tolerance) const
{
    using enum RectangleHandle;
    if (!m_rect.isValid() || !m_rect.adjusted(-tolerance, -tolerance, tolerance, tolerance).contains(point))
        return None;
    const double leftDistance = std::abs(point.x() - m_rect.left());
    const double rightDistance = std::abs(point.x() - m_rect.right());
    const double topDistance = std::abs(point.y() - m_rect.top());
    const double bottomDistance = std::abs(point.y() - m_rect.bottom());
    const bool left = leftDistance <= tolerance && leftDistance <= rightDistance;
    const bool right = rightDistance <= tolerance && !left;
    const bool top = topDistance <= tolerance && topDistance <= bottomDistance;
    const bool bottom = bottomDistance <= tolerance && !top;
    if (left && top) return TopLeft;
    if (right && top) return TopRight;
    if (left && bottom) return BottomLeft;
    if (right && bottom) return BottomRight;
    if (left) return Left;
    if (right) return Right;
    if (top) return Top;
    if (bottom) return Bottom;
    return m_rect.contains(point) ? Move : None;
}

Qt::CursorShape SelectionRectangle::cursor(RectangleHandle handle)
{
    using enum RectangleHandle;
    switch (handle)
    {
    case Move: return Qt::SizeAllCursor;
    case Left: case Right: return Qt::SizeHorCursor;
    case Top: case Bottom: return Qt::SizeVerCursor;
    case TopLeft: case BottomRight: return Qt::SizeFDiagCursor;
    case TopRight: case BottomLeft: return Qt::SizeBDiagCursor;
    default: return Qt::ArrowCursor;
    }
}

void SelectionRectangle::beginCreate(const QPointF &point)
{
    clear();
    m_anchor = point;
    m_rect = QRectF(point, point);
    m_creating = true;
}

bool SelectionRectangle::beginEdit(const QPointF &point, double tolerance)
{
    m_handle = hitTest(point, tolerance);
    if (m_handle == RectangleHandle::None) return false;
    m_creating = false;
    m_anchor = point;
    m_initial = m_rect;
    return true;
}

void SelectionRectangle::update(const QPointF &point, const QSizeF &bounds, double minimumExtent)
{
    using enum RectangleHandle;
    if (!dragging() || bounds.width() <= 0 || bounds.height() <= 0) return;
    if (m_creating)
    {
        const auto clamped = [&](const QPointF &p)
        {
            return QPointF(std::clamp(p.x(), 0.0, bounds.width()), std::clamp(p.y(), 0.0, bounds.height()));
        };
        m_rect = QRectF(clamped(m_anchor), clamped(point)).normalized();
        return;
    }
    const auto delta = point - m_anchor;
    m_rect = m_initial;
    if (m_handle == Move)
    {
        m_rect.translate(std::clamp(delta.x(), -m_initial.left(), bounds.width() - m_initial.right()),
                         std::clamp(delta.y(), -m_initial.top(), bounds.height() - m_initial.bottom()));
        return;
    }
    const double minWidth = std::min(std::max(1.0, minimumExtent), m_initial.width());
    const double minHeight = std::min(std::max(1.0, minimumExtent), m_initial.height());
    if (m_handle == Left || m_handle == TopLeft || m_handle == BottomLeft)
        m_rect.setLeft(std::clamp(m_initial.left() + delta.x(), 0.0, m_initial.right() - minWidth));
    if (m_handle == Right || m_handle == TopRight || m_handle == BottomRight)
        m_rect.setRight(std::clamp(m_initial.right() + delta.x(), m_initial.left() + minWidth, bounds.width()));
    if (m_handle == Top || m_handle == TopLeft || m_handle == TopRight)
        m_rect.setTop(std::clamp(m_initial.top() + delta.y(), 0.0, m_initial.bottom() - minHeight));
    if (m_handle == Bottom || m_handle == BottomLeft || m_handle == BottomRight)
        m_rect.setBottom(std::clamp(m_initial.bottom() + delta.y(), m_initial.top() + minHeight, bounds.height()));
}

bool SelectionRectangle::dragging() const { return m_creating || editing(); }
bool SelectionRectangle::editing() const { return m_handle != RectangleHandle::None; }
void SelectionRectangle::finish()
{
    m_creating = false;
    m_handle = RectangleHandle::None;
}
void SelectionRectangle::clear()
{
    finish();
    m_rect = {};
    m_initial = {};
}
} // namespace reverse::workbench
