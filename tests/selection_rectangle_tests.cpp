/* 文件描述：验证框选边角命中、双向创建、拖动尺寸与边界，以及高 DPI 坐标的一致性。 */
#include <array>

#include <gtest/gtest.h>

#include "workbench/SelectionRectangle.h"

namespace
{
using namespace reverse::workbench;
SelectionRectangle rectangle(double scale = 1)
{
    SelectionRectangle result;
    result.beginCreate({100 * scale, 80 * scale});
    result.update({300 * scale, 200 * scale}, {500 * scale, 400 * scale}, 4 * scale);
    result.finish();
    return result;
}

TEST(SelectionRectangle, HitsEdgesCornersInteriorAndScalesTolerance)
{
    const std::array<std::pair<QPointF, RectangleHandle>, 10> cases{{
        {{96, 140}, RectangleHandle::Left}, {{304, 140}, RectangleHandle::Right},
        {{200, 76}, RectangleHandle::Top}, {{200, 204}, RectangleHandle::Bottom},
        {{96, 76}, RectangleHandle::TopLeft}, {{304, 76}, RectangleHandle::TopRight},
        {{96, 204}, RectangleHandle::BottomLeft}, {{304, 204}, RectangleHandle::BottomRight},
        {{200, 140}, RectangleHandle::Move}, {{90, 140}, RectangleHandle::None}}};
    for (double scale : {1.0, 1.5, 2.0})
        for (const auto &[point, handle] : cases)
            EXPECT_EQ(rectangle(scale).hitTest(point * scale, 6 * scale), handle);
}

TEST(SelectionRectangle, CreatesInEitherDirectionAndClampsToViewport)
{
    SelectionRectangle selection;
    selection.beginCreate({300, 200});
    selection.update({-30, -10}, {500, 400}, 4);
    EXPECT_EQ(selection.rect(), QRectF(0, 0, 300, 200));
    selection.finish();
    EXPECT_FALSE(selection.dragging());
    selection.clear();
    EXPECT_EQ(selection.hitTest({0, 0}, 6), RectangleHandle::None);
}

TEST(SelectionRectangle, MovesWithoutResizingAndUsesOriginalDragAnchor)
{
    auto selection = rectangle();
    ASSERT_TRUE(selection.beginEdit({180, 140}, 6));
    selection.update({230, 170}, {500, 400}, 4);
    EXPECT_EQ(selection.rect(), QRectF(150, 110, 200, 120));
    selection.update({1000, 1000}, {500, 400}, 4);
    EXPECT_EQ(selection.rect(), QRectF(300, 280, 200, 120));
    selection.update({-1000, -1000}, {500, 400}, 4);
    EXPECT_EQ(selection.rect(), QRectF(0, 0, 200, 120));
    selection.update({180, 140}, {500, 400}, 4);
    EXPECT_EQ(selection.rect(), QRectF(100, 80, 200, 120));
}

TEST(SelectionRectangle, ResizesEachEdgeAndCornerKeepingOppositeSidesFixed)
{
    struct Case { QPointF start; QPointF end; QRectF expected; };
    const std::array<Case, 8> cases{{
        {{100, 140}, {50, 150}, {50, 80, 250, 120}},
        {{300, 140}, {250, 150}, {100, 80, 150, 120}},
        {{200, 80}, {220, 50}, {100, 50, 200, 150}},
        {{200, 200}, {220, 250}, {100, 80, 200, 170}},
        {{100, 80}, {50, 50}, {50, 50, 250, 150}},
        {{300, 80}, {250, 50}, {100, 50, 150, 150}},
        {{100, 200}, {50, 250}, {50, 80, 250, 170}},
        {{300, 200}, {250, 250}, {100, 80, 150, 170}}}};
    for (const auto &entry : cases)
    {
        auto selection = rectangle();
        ASSERT_TRUE(selection.beginEdit(entry.start, 6));
        selection.update(entry.end, {500, 400}, 4);
        EXPECT_EQ(selection.rect(), entry.expected);
    }
}

TEST(SelectionRectangle, PreventsInversionAndSupportsRepeatedEdits)
{
    auto selection = rectangle(2);
    ASSERT_TRUE(selection.beginEdit({200, 160}, 12));
    selection.update({900, 900}, {1000, 800}, 8);
    EXPECT_EQ(selection.rect(), QRectF(592, 392, 8, 8));
    selection.finish();
    ASSERT_TRUE(selection.beginEdit({600, 400}, 2));
    selection.update({2000, 2000}, {1000, 800}, 8);
    EXPECT_EQ(selection.rect(), QRectF(592, 392, 408, 408));
    selection.finish();
    const auto beforeClick = selection.rect();
    ASSERT_TRUE(selection.beginEdit({700, 500}, 12));
    selection.finish();
    EXPECT_EQ(selection.rect(), beforeClick);
}
} // namespace
