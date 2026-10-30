/*
 * 文件描述：
 * 加载工作台 QSS 和深色调色板，并绘制支持高 DPI 与禁用状态的线性工具图标。
 * 图标在本工程生成，不依赖参考软件的位图或外部主题框架。
 */
#include <QApplication>
#include <QFile>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>

#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
void WorkbenchTheme::apply(QApplication &application)
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#081521"));
    palette.setColor(QPalette::WindowText, QColor("#dce4ea"));
    palette.setColor(QPalette::Base, QColor("#071119"));
    palette.setColor(QPalette::AlternateBase, QColor("#0b202a"));
    palette.setColor(QPalette::Text, QColor("#dce4ea"));
    palette.setColor(QPalette::Button, QColor("#102a3a"));
    palette.setColor(QPalette::ButtonText, QColor("#dce4ea"));
    palette.setColor(QPalette::Highlight, QColor("#16475b"));
    palette.setColor(QPalette::HighlightedText, QColor("#43c4f3"));
    palette.setColor(QPalette::ToolTipBase, QColor("#102a3a"));
    palette.setColor(QPalette::ToolTipText, QColor("#dce4ea"));
    for (const auto role : {QPalette::Text, QPalette::ButtonText, QPalette::WindowText})
    {
        palette.setColor(QPalette::Disabled, role, QColor("#617582"));
    }
    application.setPalette(palette);
    QFile stylesheet(QStringLiteral(":/workbench/workbench.qss"));
    if (stylesheet.open(QIODevice::ReadOnly))
    {
        application.setStyleSheet(QString::fromUtf8(stylesheet.readAll()));
    }
}

QIcon WorkbenchTheme::icon(const QString &name)
{
    if (name.isEmpty())
    {
        return {};
    }
    QIcon icon;
    for (const auto mode : {QIcon::Normal, QIcon::Disabled})
    {
        for (const int size : {24, 32, 48, 64})
        {
            QPixmap pixmap(size, size);
            pixmap.fill(Qt::transparent);
            QPainter painter(&pixmap);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.scale(size / 32.0, size / 32.0);
            QColor color(mode == QIcon::Disabled ? "#405563" : "#27b9ed");
            painter.setPen(QPen(color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            const auto line = [&painter](int x1, int y1, int x2, int y2)
            {
                painter.drawLine(QPoint(x1, y1), QPoint(x2, y2));
            };
            if (name == "folder")
            {
                painter.drawPolyline(QPolygon{{3, 25}, {3, 7}, {12, 7}, {15, 11}, {28, 11}});
                painter.drawPolygon(QPolygon{{3, 25}, {7, 14}, {30, 14}, {25, 25}});
            }
            else if (name == "home")
            {
                painter.drawPolyline(QPolygon{{3, 15}, {16, 4}, {29, 15}});
                painter.drawPolyline(QPolygon{{7, 12}, {7, 28}, {13, 28}, {13, 19}, {19, 19},
                                             {19, 28}, {25, 28}, {25, 12}});
            }
            else if (name == "play" || name == "stop")
            {
                painter.setPen(Qt::NoPen);
                painter.setBrush(mode == QIcon::Disabled ? color : QColor(name == "play" ? "#27c75b" : "#d95256"));
                if (name == "play")
                {
                    painter.drawPolygon(QPolygon{{8, 4}, {27, 16}, {8, 28}});
                }
                else
                {
                    painter.drawRoundedRect(QRectF(6, 6, 20, 20), 2, 2);
                }
            }
            else if (name == "parameters")
            {
                for (int i = 0; i < 3; ++i)
                {
                    const int x = 7 + i * 9;
                    line(x, 4, x, 28);
                    painter.setBrush(QColor("#081521"));
                    painter.drawEllipse(QPoint(x, 10 + (i % 2) * 12), 3, 3);
                }
            }
            else if (name == "cloud")
            {
                QPainterPath path;
                path.moveTo(8, 25);
                path.cubicTo(0, 25, 1, 15, 8, 15);
                path.cubicTo(7, 3, 24, 3, 24, 15);
                path.cubicTo(32, 14, 33, 25, 25, 25);
                path.closeSubpath();
                painter.drawPath(path);
            }
            else if (name == "move" || name == "align")
            {
                line(16, 3, 16, 29);
                line(3, 16, 29, 16);
                painter.drawPolyline(QPolygon{{11, 8}, {16, 3}, {21, 8}});
                painter.drawPolyline(QPolygon{{24, 11}, {29, 16}, {24, 21}});
                painter.drawPolyline(QPolygon{{8, 11}, {3, 16}, {8, 21}});
                painter.drawPolyline(QPolygon{{11, 24}, {16, 29}, {21, 24}});
            }
            else if (name == "rotate" || name == "reset" || name == "undo" || name == "redo")
            {
                painter.drawArc(QRect(5, 5, 23, 23), 35 * 16, 285 * 16);
                painter.drawPolyline(QPolygon{{14, 3}, {7, 6}, {12, 12}});
            }
            else if (name == "plane")
            {
                painter.drawPolygon(QPolygon{{4, 23}, {11, 8}, {29, 8}, {22, 23}});
                line(16, 3, 16, 28);
            }
            else if (name == "cursor")
            {
                painter.drawPolygon(QPolygon{{7, 3}, {25, 19}, {17, 19}, {20, 28}, {15, 30}, {12, 21}, {7, 26}});
            }
            else if (name == "measure")
            {
                painter.drawRect(4, 10, 25, 12);
                for (int i = 8; i < 28; i += 5)
                {
                    line(i, 10, i, 16);
                }
            }
            else if (name == "delete")
            {
                painter.drawRect(8, 10, 16, 18);
                line(5, 6, 27, 6);
                line(12, 3, 20, 3);
                line(13, 14, 13, 24);
                line(19, 14, 19, 24);
            }
            else if (name == "fit" || name == "crop")
            {
                painter.drawPolyline(QPolygon{{4, 12}, {4, 4}, {12, 4}});
                painter.drawPolyline(QPolygon{{20, 4}, {28, 4}, {28, 12}});
                painter.drawPolyline(QPolygon{{4, 20}, {4, 28}, {12, 28}});
                painter.drawPolyline(QPolygon{{20, 28}, {28, 28}, {28, 20}});
                painter.drawRect(10, 10, 12, 12);
            }
            else if (name == "more")
            {
                painter.setBrush(color);
                for (const int x : {6, 16, 26})
                {
                    painter.drawEllipse(QPoint(x, 16), 2, 2);
                }
            }
            else if (name == "window")
            {
                painter.drawRoundedRect(QRect(3, 5, 26, 23), 2, 2);
                line(3, 11, 29, 11);
                line(21, 11, 21, 28);
            }
            else if (name == "hole")
            {
                painter.drawPolyline(QPolygon{{3, 27}, {29, 27}, {29, 6}, {7, 6}, {7, 22}});
                painter.drawArc(QRect(12, 16, 12, 12), 0, 180 * 16);
            }
            else
            {
                painter.drawPolygon(QPolygon{{16, 3}, {28, 10}, {28, 23}, {16, 30}, {4, 23}, {4, 10}});
                painter.drawPolyline(QPolygon{{4, 10}, {16, 17}, {28, 10}});
                line(16, 17, 16, 30);
                if (name == "export")
                {
                    line(18, 10, 30, 10);
                    line(25, 5, 30, 10);
                    line(25, 15, 30, 10);
                }
            }
            painter.end();
            icon.addPixmap(pixmap, mode);
        }
    }
    return icon;
}
} // namespace reverse::workbench
