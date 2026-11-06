/*
 * 文件描述：绘制板件操作前后示意图，并在菜单项旁展示无需激活的图文卡。
 * 复用 CamAssemblyWelding 的图标、说明图和菜单悬停交互；截取补充主体保留示意图。
 * 卡片随菜单隐藏，不参与目标拾取、几何预览或命令提交。
 */
#include <algorithm>

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QScreen>
#include <QVBoxLayout>

#include "workbench/PlateEditHelp.h"

namespace reverse::workbench
{
namespace
{
const QColor sourceColor("#32bcec"), targetColor("#91a2b8"), accentColor("#ff9d45");

QString instructions(const QString &operation, bool all)
{
    if (operation == "extend")
        return QStringLiteral("选中一个源板，再左键点选目标板，自动延长至贴合；目标板保持不变。");
    if (operation == "trim")
        return QStringLiteral("选中一个源板，再点选目标板，沿目标板分割后保留体积最大的主体。");
    if (operation == "merge")
        return QStringLiteral("选中一个源板，再点选目标板，合成一个板件；以目标板的位置和厚度为准。");
    if (operation == "split")
        return QStringLiteral("选中一个源板，再点选相交的目标板，扣除目标板厚度范围，生成独立板件。");
    if (operation == "crop")
        return QStringLiteral("左键拖框选择要切除的板件一端，松开即整端切除；不支持中部或整件切除。");
    return all ? QStringLiteral("一键扫描并净化当前模型中所有板件的孔洞与缺陷。")
               : QStringLiteral("剥离板件内部孔洞并平滑边界，消除逆向扫描噪点。");
}

void plate(QPainter &p, const QRectF &rect, const QColor &color)
{
    p.setPen(QPen(color.darker(135), 1.4));
    p.setBrush(color);
    p.drawRoundedRect(rect, 2, 2);
}

void diagram(QPainter &p, const QString &op, bool after)
{
    Q_UNUSED(op);
    plate(p, {122, 14, 18, 98}, targetColor);
    plate(p, {16, 40, after ? 106.0 : 138.0, 46}, sourceColor);
    p.setPen(QPen(accentColor, 2, Qt::DashLine));
    if (!after)
        p.drawLine(124, 63, 152, 63);
}

class HelpCard final : public QFrame
{
  public:
    explicit HelpCard(QMenu &menu) : QFrame(&menu, Qt::ToolTip | Qt::WindowDoesNotAcceptFocus)
    {
        setObjectName(QStringLiteral("plateEditHelpCard"));
        setAttribute(Qt::WA_ShowWithoutActivating);
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setStyleSheet(QStringLiteral("QFrame#plateEditHelpCard { background:#101f2b; border:1px solid #4c7187; border-radius:7px; }"
                                     "QLabel { background:transparent; border:none; color:#e5f2fa; }"));
        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(12, 10, 12, 10);
        m_title = new QLabel(this);
        auto font = m_title->font();
        font.setBold(true);
        m_title->setFont(font);
        m_image = new QLabel(this);
        m_text = new QLabel(this);
        m_text->setWordWrap(true);
        m_text->setFixedWidth(360);
        layout->addWidget(m_title);
        layout->addWidget(m_image);
        layout->addWidget(m_text);
        menu.installEventFilter(this);
        connect(&menu, &QMenu::hovered, this, [this, &menu](QAction *action) { showAction(menu, action); });
        connect(&menu, &QMenu::aboutToHide, this, &QWidget::hide);
    }

  protected:
    bool eventFilter(QObject *object, QEvent *event) override
    {
        if (event->type() == QEvent::Leave)
            hide();
        return QFrame::eventFilter(object, event);
    }

  private:
    void showAction(QMenu &menu, QAction *action)
    {
        const auto op = action->property("plateHelp.operation").toString();
        if (op.isEmpty())
        {
            hide();
            return;
        }
        m_title->setText(action->text());
        auto text = instructions(op, action->property("plateHelp.all").toBool());
        text += QStringLiteral("\n无需预览或填数值；Ctrl+Z 撤销，Esc 取消拾取。");
        if (!action->isEnabled())
            text += QStringLiteral("\n当前不可用：请等待任务结束，并只选择一个源板件（修复全部除外）。");
        m_text->setText(text);
        if (op != "trim")
            m_image->setPixmap(QPixmap(QStringLiteral(":/workbench/plate-edit/preview_%1.png").arg(op))
                                   .scaled(360, 152, Qt::KeepAspectRatio, Qt::SmoothTransformation));
        else
        {
            const auto ratio = menu.devicePixelRatioF();
            QPixmap pixmap(qRound(360 * ratio), qRound(152 * ratio));
            pixmap.setDevicePixelRatio(ratio);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            for (int i = 0; i < 2; ++i)
            {
                p.save();
                p.translate(i * 198, 20);
                p.setPen(QPen(QColor("#3b5266"), 1));
                p.setBrush(QColor("#162632"));
                p.drawRoundedRect(QRectF(0, 0, 160, 128), 5, 5);
                diagram(p, op, i == 1);
                p.restore();
            }
            p.setPen(QColor("#c9e2f0"));
            p.drawText(QRect(0, 0, 160, 18), Qt::AlignCenter, QStringLiteral("操作前"));
            p.drawText(QRect(198, 0, 160, 18), Qt::AlignCenter, QStringLiteral("操作后"));
            p.drawText(QRect(162, 60, 34, 30), Qt::AlignCenter, QStringLiteral("→"));
            p.end();
            m_image->setPixmap(pixmap);
        }
        adjustSize();
        auto position = menu.mapToGlobal(QPoint(menu.width() + 6, menu.actionGeometry(action).top()));
        const auto *screen = menu.screen();
        const auto bounds = screen->availableGeometry();
        if (position.x() + width() > bounds.right())
            position.setX(menu.mapToGlobal(QPoint(0, 0)).x() - width() - 6);
        position.setX(std::max(bounds.left(), position.x()));
        position.setY(std::clamp(position.y(), bounds.top(), std::max(bounds.top(), bounds.bottom() - height())));
        move(position);
        show();
    }
    QLabel *m_title{}, *m_image{}, *m_text{};
};
} // namespace

void addPlateEditHelp(QMenu &menu, QAction &action, const std::string &operation, bool all)
{
    if (!menu.findChild<QFrame *>(QStringLiteral("plateEditHelpCard")))
        new HelpCard(menu);
    action.setProperty("plateHelp.operation", QString::fromStdString(operation));
    action.setProperty("plateHelp.all", all);
    const auto icon = QString::fromStdString(operation == "trim" ? "split" : operation);
    action.setIcon(QIcon(QStringLiteral(":/workbench/plate-edit/plate_%1.png").arg(icon)));
}
} // namespace reverse::workbench
