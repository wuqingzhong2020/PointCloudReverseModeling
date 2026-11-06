/*
 * 文件描述：独立绘制外扩贴板、下底板的透视示意；绿色为新增板，虚线为源板投影。
 * D 标示轮廓外扩方向，T 标示新板厚度；仅解释参数含义，不读取模型或参与几何计算。
 * 采用 QPainter 矢量绘制，适配侧栏宽度和高 DPI，不使用参考软件的图片资源。
 */
#include <algorithm>
#include <cmath>

#include <QPainter>
#include <QPolygonF>

#include "workbench/PlateGenerationDiagram.h"

namespace reverse::workbench
{
namespace
{
const QColor outline("#416a7a");
const QColor greenLine("#71b447");
const QColor greenFront("#b5dd9e");

void face(QPainter &p, const QPolygonF &points, const QColor &fill, const QColor &edge)
{
    p.setPen(QPen(edge, 1.4));
    p.setBrush(fill);
    p.drawPolygon(points);
}

void arrowTip(QPainter &p, QPointF tip, QPointF inside)
{
    auto direction = inside - tip;
    const auto length = std::hypot(direction.x(), direction.y());
    if (length < 1e-6)
    {
        return;
    }
    direction /= length;
    const QPointF across(-direction.y(), direction.x());
    p.setPen(Qt::NoPen);
    p.setBrush(QColor("#f1f5f6"));
    p.drawPolygon(QPolygonF{tip, tip + direction * 4.5 + across * 2, tip + direction * 4.5 - across * 2});
}

void badge(QPainter &p, QPointF center, const QString &text)
{
    const QRectF box(center.x() - 10, center.y() - 10, 20, 20);
    p.setPen(QPen(QColor("#17232d"), 0.8));
    p.setBrush(QColor("#f0f3ef"));
    p.drawRoundedRect(box, 3, 3);
    QFont font = p.font();
    font.setPixelSize(15);
    font.setBold(true);
    p.setFont(font);
    p.setPen(QColor("#23332d"));
    p.drawText(box, Qt::AlignCenter, text);
}

void dimension(QPainter &p, QPointF a, QPointF b, QPointF shift, QPointF label, const QString &text)
{
    const auto start = a + shift, end = b + shift;
    // 深色衬线让浅色板面上的尺寸线也可辨认。
    p.setPen(QPen(QColor("#122119"), 3.5));
    p.drawLine(start, end);
    p.setPen(QPen(QColor("#ebf1eb"), 1.1));
    p.drawLine(a, start);
    p.drawLine(b, end);
    p.drawLine(start, end);
    arrowTip(p, start, end);
    arrowTip(p, end, start);
    badge(p, label, text);
}

void coverDiagram(QPainter &p)
{
    // 灰色原板：主面及向后延伸的板厚，避免把 T 误读为原板厚度。
    face(p, {{62, 29}, {206, 9}, {224, 35}, {80, 55}}, QColor("#e8edf0"), outline);
    face(p, {{62, 29}, {80, 55}, {80, 165}, {62, 139}}, QColor("#a6b4bc"), outline);
    face(p, {{80, 55}, {224, 35}, {224, 145}, {80, 165}}, QColor("#c8d3d9"), outline);
    // 新贴板在选中主面外侧；绿色薄侧面用于解释新板厚度。
    face(p, {{64, 43}, {240, 19}, {248, 31}, {72, 55}}, QColor("#d6ecc6"), greenLine);
    face(p, {{64, 43}, {72, 55}, {72, 193}, {64, 181}}, QColor("#74aa59"), greenLine);
    face(p, {{72, 55}, {248, 31}, {248, 169}, {72, 193}}, QColor(181, 221, 158, 225), greenLine);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor("#497c36"), 1.2, Qt::DashLine));
    p.drawPolygon(QPolygonF{{88, 67}, {232, 47}, {232, 157}, {88, 177}});
    // 下侧虚线为原轮廓投影，实线为外扩后的轮廓。
    dimension(p, {150, 168.4}, {150, 182.4}, {0, 0}, {174, 183}, QStringLiteral("D"));
    dimension(p, {240, 19}, {248, 31}, {22, 9}, {281, 39}, QStringLiteral("T"));
}

void baseDiagram(QPainter &p, BasePlateMode mode)
{
    // 新底板位于源板的下方，围绕底部轮廓四周外扩。
    face(p, {{38, 120}, {216, 94}, {259, 160}, {72, 186}}, greenFront, greenLine);
    face(p, {{72, 186}, {259, 160}, {259, 171}, {72, 197}}, QColor("#80b662"), greenLine);
    face(p, {{38, 120}, {72, 186}, {72, 197}, {38, 131}}, QColor("#638f4e"), greenLine);
    if (mode == BasePlateMode::Horizontal)
    {
        face(p, {{62, 113}, {206, 93}, {226, 125}, {82, 145}}, QColor("#d7e7fb"), QColor("#548ad7"));
        face(p, {{62, 113}, {82, 145}, {82, 166}, {62, 134}}, QColor("#8faed0"), QColor("#548ad7"));
        face(p, {{82, 145}, {226, 125}, {226, 146}, {82, 166}}, QColor("#bfd7f6"), QColor("#548ad7"));
    }
    else
    {
        for (const QPointF shift : {QPointF(0, 0), QPointF(20, 32)})
        {
            p.save();
            p.translate(shift);
            face(p, {{62, 40}, {206, 20}, {211, 28}, {67, 48}}, QColor("#d7e7fb"), QColor("#548ad7"));
            face(p, {{62, 40}, {67, 48}, {67, 140}, {62, 132}}, QColor("#8faed0"), QColor("#548ad7"));
            face(p, {{67, 48}, {211, 28}, {211, 120}, {67, 140}}, QColor("#bfd7f6"), QColor("#548ad7"));
            p.restore();
        }
    }
    dimension(p, {150, 156.6}, {150, 175.2}, {0, 0}, {171, 178}, QStringLiteral("D"));
    dimension(p, {259, 160}, {259, 171}, {18, 0}, {277, 142}, QStringLiteral("T"));
}
} // namespace

PlateGenerationDiagram::PlateGenerationDiagram(QWidget *parent) : QWidget(parent)
{
    setObjectName(QStringLiteral("plateGeneration.diagram"));
    setMinimumSize(160, 140);
    setMaximumHeight(224);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setKind(PlateGenerationKind::Cover);
}
void PlateGenerationDiagram::setKind(PlateGenerationKind kind)
{
    m_kind = kind;
    const auto text =
        kind == PlateGenerationKind::Cover
            ? QStringLiteral("外扩贴板示意：灰色原板，绿色新增贴板；D 为沿原轮廓向外的距离，T "
                             "为新贴板厚度。虚线表示原轮廓投影。反向选项使贴板沿所选面内法线生成。")
            : QStringLiteral(
                  "下底板示意：蓝色原板，绿色新增底板；D 为四周外扩距离，T 为沿世界 -Z 的厚度。选择单块水平板或两块竖直板，允许偏差 ±5°，最终生成一块平行于 XY 平面的底板。");
    setAccessibleName(text);
    setToolTip(text);
    update();
}
void PlateGenerationDiagram::setBaseMode(BasePlateMode mode)
{
    m_baseMode = mode;
    update();
}
QSize PlateGenerationDiagram::sizeHint() const
{
    return {280, 216};
}
void PlateGenerationDiagram::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor("#20647a"), 1));
    p.setBrush(QColor("#040d13"));
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 4, 4);
    const auto scale = std::min((width() - 8) / 300.0, (height() - 8) / 220.0);
    p.translate((width() - 300 * scale) / 2, (height() - 220 * scale) / 2);
    p.scale(scale, scale);
    if (m_kind == PlateGenerationKind::Cover)
    {
        coverDiagram(p);
    }
    else
    {
        baseDiagram(p, m_baseMode);
    }
    QFont font = p.font();
    font.setPixelSize(11);
    font.setBold(false);
    p.setFont(font);
    p.setPen(QColor("#a9bdc8"));
    p.drawText(QRectF(10, 201, 280, 18), Qt::AlignCenter,
               QStringLiteral("绿色：新增板件   ·   示意不按比例"));
}
} // namespace reverse::workbench
