/*
 * 文件描述：在板厚标签位置显示毫米输入框；有效数值回车时提交一次，取消/未改值不产生业务。
 * 使用独立 Qt Popup 绘制在原生 OCCT 视口上，编辑器不执行几何算法或阻塞等待任务。
 */
#include <algorithm>
#include <cmath>

#include <QEvent>
#include <QGuiApplication>
#include <QHideEvent>
#include <QKeyEvent>
#include <QLineEdit>
#include <QScreen>
#include <QToolTip>

#include "workbench/ThicknessLabelEditor.h"

namespace reverse::workbench
{
ThicknessLabelEditor::ThicknessLabelEditor(QWidget *parent) : QDoubleSpinBox(parent)
{
    setWindowFlags(Qt::Popup);
    setObjectName(QStringLiteral("editor.thicknessLabel"));
    setAccessibleName(QStringLiteral("板厚标签编辑"));
    setDecimals(1);
    setRange(0.1, 1000000.0);
    setSingleStep(1.0);
    setSuffix(QStringLiteral(" mm"));
    setKeyboardTracking(false);
    setToolTip(QStringLiteral("回车修改此板件的板厚；Esc 或点击外部取消"));
    setMinimumSize(170, 34);
    lineEdit()->installEventFilter(this);
    installEventFilter(this);
    hide();
}

void ThicknessLabelEditor::open(const std::string &partId, double thicknessMm, const QPoint &globalCenter)
{
    hide();
    if (partId.empty() || !std::isfinite(thicknessMm) || thicknessMm < minimum() || thicknessMm > maximum())
    {
        return;
    }
    m_partId = partId;
    setValue(thicknessMm);
    m_initialValue = value(); // 防止输入框精度舍入使“未改值回车”提交一次修改。
    resize(std::max(170, sizeHint().width()), std::max(34, sizeHint().height()));
    QPoint position = globalCenter - QPoint(width() / 2, height() / 2);
    if (const auto *screen = QGuiApplication::screenAt(globalCenter))
    {
        const QRect available = screen->availableGeometry();
        position.setX(std::clamp(position.x(), available.left(), std::max(available.left(), available.right() - width())));
        position.setY(std::clamp(position.y(), available.top(), std::max(available.top(), available.bottom() - height())));
    }
    move(position);
    show();
    raise();
    setFocus(Qt::MouseFocusReason);
    selectAll();
}

bool ThicknessLabelEditor::eventFilter(QObject *object, QEvent *event)
{
    if ((object == this || object == lineEdit()) &&
        (event->type() == QEvent::KeyPress || event->type() == QEvent::ShortcutOverride))
    {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter || key->key() == Qt::Key_Escape)
        {
            event->accept();
            if (event->type() == QEvent::KeyPress)
            {
                if (key->key() == Qt::Key_Escape)
                {
                    hide();
                }
                else
                {
                    submit();
                }
            }
            return true;
        }
    }
    return QDoubleSpinBox::eventFilter(object, event);
}

void ThicknessLabelEditor::submit()
{
    if (m_partId.empty())
    {
        return;
    }
    if (!hasAcceptableInput())
    {
        QToolTip::showText(mapToGlobal(QPoint(0, height())), QStringLiteral("板厚必须为 0.1 到 1000000 mm，保留一位小数。"), this);
        return;
    }
    interpretText();
    const auto id = m_partId;
    const double thickness = value();
    const bool changed = thickness != m_initialValue;
    hide();
    if (changed && onSubmitted)
    {
        onSubmitted(id, thickness);
    }
}

void ThicknessLabelEditor::hideEvent(QHideEvent *event)
{
    m_partId.clear();
    QDoubleSpinBox::hideEvent(event);
}
}
