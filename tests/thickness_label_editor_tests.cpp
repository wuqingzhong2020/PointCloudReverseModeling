/* 文件描述：真实 Qt 输入事件验证标签只有回车提交，未知默认值可提交，焦点转移和外部点击取消。 */
#include <QApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>

#include <gtest/gtest.h>

#include "workbench/ThicknessLabelEditor.h"

namespace
{
using reverse::workbench::ThicknessLabelEditor;

QApplication &application()
{
    static int argc = 1;
    static char name[] = "thickness-label-test";
    static char *argv[] = {name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

void key(QWidget *widget, int code)
{
    QKeyEvent event(QEvent::KeyPress, code, Qt::NoModifier);
    QApplication::sendEvent(widget, &event);
    QApplication::processEvents();
}

TEST(ThicknessLabelEditor, EnterIsTheOnlyCommitAndUnknownDefaultCanBeConfirmed)
{
    (void)application();
    QWidget parent;
    ThicknessLabelEditor editor(&parent);
    int count = 0;
    double submitted = 0;
    editor.onSubmitted = [&](const std::string &id, double value) { EXPECT_EQ(id, "wall"); ++count; submitted = value; };
    editor.open("wall", 12.1, {300, 300});
    auto *input = editor.findChild<QLineEdit *>();
    ASSERT_NE(input, nullptr);
    input->setText("18.0 mm");
    EXPECT_EQ(count, 0);
    key(input, Qt::Key_Return);
    EXPECT_EQ(count, 1);
    EXPECT_DOUBLE_EQ(submitted, 18);
    EXPECT_FALSE(editor.isVisible());
    editor.open("wall", 12.123, {300, 300});
    key(input, Qt::Key_Return);
    EXPECT_EQ(count, 1); // 仅显示精度舍入不产生业务修改。
    editor.open("wall", 1, {300, 300}, true);
    key(input, Qt::Key_Enter);
    EXPECT_EQ(count, 2);
    EXPECT_DOUBLE_EQ(submitted, 1);
}

TEST(ThicknessLabelEditor, OutsideClickFocusLossAndEscapeCancelPendingInput)
{
    (void)application();
    QWidget parent;
    QLineEdit other(&parent);
    parent.resize(600, 400);
    parent.show();
    ThicknessLabelEditor editor(&parent);
    int count = 0;
    editor.onSubmitted = [&](const auto &, double) { ++count; };
    auto *input = editor.findChild<QLineEdit *>();
    ASSERT_NE(input, nullptr);
    editor.open("wall", 12.1, {300, 300});
    input->setText("23.0 mm");
    const QPointF global = editor.frameGeometry().topLeft() - QPoint(20, 20);
    QMouseEvent click(QEvent::MouseButtonPress, QPointF(0, 0), global,
        Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&other, &click);
    QApplication::processEvents();
    EXPECT_FALSE(editor.isVisible());
    EXPECT_DOUBLE_EQ(editor.value(), 12.1);
    EXPECT_EQ(count, 0);
    editor.open("wall", 12.1, {300, 300});
    input->setText("27.0 mm");
    QApplication::processEvents();
    ASSERT_TRUE(editor.hasFocus() || input->hasFocus());
    // Popup 会拦截向父窗口控件的 setFocus；显式清除实际输入焦点，触发真实 FocusOut。
    editor.clearFocus();
    QApplication::processEvents();
    EXPECT_NE(QApplication::focusWidget(), &editor);
    EXPECT_FALSE(editor.isVisible());
    EXPECT_DOUBLE_EQ(editor.value(), 12.1);
    EXPECT_EQ(count, 0);
    editor.open("wall", 1, {300, 300}, true);
    input->setText("15.0 mm");
    key(input, Qt::Key_Escape);
    EXPECT_FALSE(editor.isVisible());
    EXPECT_EQ(count, 0);
}
}
