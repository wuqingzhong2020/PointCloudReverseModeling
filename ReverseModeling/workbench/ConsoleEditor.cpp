/* 文件描述：输入输出共用文本区，限制编辑边界并提供键盘历史和基础补全。 */
#include <algorithm>
#include <QContextMenuEvent>
#include <QFontDatabase>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QRegularExpression>
#include <QTextBlock>
#include "workbench/ConsoleEditor.h"

namespace reverse::workbench
{
ConsoleEditor::ConsoleEditor(QWidget *parent) : QPlainTextEdit(parent)
{
    setObjectName(QStringLiteral("command.console"));
    setAccessibleName(QStringLiteral("交互控制台"));
    setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    setUndoRedoEnabled(false);
    setAcceptDrops(false);
    setTabChangesFocus(false);
    setLineWrapMode(QPlainTextEdit::NoWrap);
    setReadOnly(true);
}
bool ConsoleEditor::acceptingInput() const
{
    return m_accepting;
}
QString ConsoleEditor::input() const
{
    if (!m_accepting)
    {
        return {};
    }
    auto cursor = textCursor();
    cursor.setPosition(m_inputStart);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    return cursor.selectedText().replace(QChar::ParagraphSeparator, '\n');
}
void ConsoleEditor::showPrompt(const QString &prompt, const QString &initial, bool rawInput)
{
    m_prompt = prompt;
    m_rawInput = rawInput;
    auto cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    if (!rawInput && cursor.positionInBlock() != 0)
    {
        cursor.insertText(QStringLiteral("\n"));
    }
    // Bound scrollback only between prompts, so editing offsets cannot shift under an input request.
    while (document()->blockCount() > 3000)
    {
        QTextCursor old(document());
        old.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor);
        old.removeSelectedText();
    }
    cursor.movePosition(QTextCursor::End);
    m_promptStart = cursor.position();
    cursor.insertText(prompt);
    m_inputStart = cursor.position();
    cursor.insertText(initial);
    setTextCursor(cursor);
    m_accepting = true;
    setReadOnly(false);
    ensureCursorVisible();
}
QString ConsoleEditor::takeInput()
{
    const auto value = input();
    auto cursor = textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral("\n"));
    setTextCursor(cursor);
    m_accepting = false;
    setReadOnly(true);
    return value;
}
void ConsoleEditor::replaceInput(const QString &value)
{
    if (!m_accepting)
    {
        return;
    }
    auto cursor = textCursor();
    cursor.setPosition(m_inputStart);
    cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
    cursor.insertText(value);
    setTextCursor(cursor);
    ensureCursorVisible();
}
void ConsoleEditor::writeOutput(const QString &value)
{
    if (value.isEmpty())
    {
        return;
    }
    const bool restore = m_accepting;
    const auto draft = input();
    auto cursor = textCursor();
    if (restore)
    {
        cursor.setPosition(m_promptStart);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
    }
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(value);
    setTextCursor(cursor);
    if (restore)
    {
        showPrompt(m_prompt, draft, m_rawInput);
    }
    ensureCursorVisible();
}
void ConsoleEditor::clearScreen()
{
    const auto draft = input();
    const bool restore = m_accepting;
    clear();
    m_promptStart = m_inputStart = 0;
    if (restore)
    {
        showPrompt(m_prompt, draft, m_rawInput);
    }
}
void ConsoleEditor::setCompletions(QStringList values)
{
    m_completions = std::move(values);
    m_completions.removeDuplicates();
    m_completions.sort();
}
void ConsoleEditor::editableCursor()
{
    auto cursor = textCursor();
    if (cursor.hasSelection())
    {
        const int end = cursor.selectionEnd();
        const int begin = std::max(m_inputStart, cursor.selectionStart());
        if (end >= begin)
        {
            cursor.setPosition(begin);
            cursor.setPosition(end, QTextCursor::KeepAnchor);
        }
        else
        {
            cursor.movePosition(QTextCursor::End);
        }
    }
    else if (cursor.position() < m_inputStart)
    {
        cursor.movePosition(QTextCursor::End);
    }
    setTextCursor(cursor);
}
void ConsoleEditor::completeWord()
{
    editableCursor();
    const auto draft = input();
    const int offset = textCursor().position() - m_inputStart;
    const auto before = draft.left(offset);
    if (before.trimmed().isEmpty())
    {
        insertPlainText(QStringLiteral("    "));
        return;
    }
    const auto match = QRegularExpression(QStringLiteral("[A-Za-z0-9_.-]+$")).match(before);
    if (!match.hasMatch())
    {
        return;
    }
    const auto word = match.captured();
    QStringList choices;
    for (const auto &candidate : m_completions)
    {
        if (candidate.startsWith(word))
        {
            choices.push_back(candidate);
        }
    }
    if (choices.isEmpty())
    {
        return;
    }
    auto prefix = choices.front();
    for (const auto &candidate : choices)
    {
        while (!candidate.startsWith(prefix))
        {
            prefix.chop(1);
        }
    }
    if (prefix.size() > word.size())
    {
        insertPlainText(prefix.mid(word.size()));
    }
    else if (choices.size() > 1)
    {
        writeOutput(choices.join(QStringLiteral("  ")) + '\n');
    }
}
void ConsoleEditor::keyPressEvent(QKeyEvent *event)
{
    const bool control = event->modifiers().testFlag(Qt::ControlModifier);
    if (event->matches(QKeySequence::Copy))
    {
        if (textCursor().hasSelection())
        {
            copy();
        }
        else if (onInterrupt)
        {
            onInterrupt();
        }
        return;
    }
    if (control && event->key() == Qt::Key_L)
    {
        clearScreen();
        return;
    }
    if (control && event->key() == Qt::Key_D && m_accepting && m_rawInput && onEof)
    {
        onEof();
        return;
    }
    if (!m_accepting)
    {
        QPlainTextEdit::keyPressEvent(event);
        return;
    }
    if (event->matches(QKeySequence::Undo) || event->matches(QKeySequence::Redo))
    {
        return;
    }
    if (event->matches(QKeySequence::SelectAll))
    {
        auto cursor = textCursor();
        cursor.setPosition(m_inputStart);
        cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        setTextCursor(cursor);
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        if (event->modifiers().testFlag(Qt::ShiftModifier) && !m_rawInput)
        {
            editableCursor();
            insertPlainText(QStringLiteral("\n"));
        }
        else if (onSubmit)
        {
            onSubmit(takeInput());
        }
        return;
    }
    if ((event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) && !m_rawInput && !input().contains('\n') &&
        onHistory)
    {
        onHistory(event->key() == Qt::Key_Up ? -1 : 1);
        return;
    }
    if (event->key() == Qt::Key_Tab && !m_rawInput)
    {
        completeWord();
        return;
    }
    if (event->key() == Qt::Key_Home)
    {
        auto cursor = textCursor();
        cursor.setPosition(std::max(m_inputStart, cursor.block().position()),
                           event->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                                          : QTextCursor::MoveAnchor);
        setTextCursor(cursor);
        return;
    }
    if (event->key() == Qt::Key_Left && textCursor().position() <= m_inputStart)
    {
        return;
    }
    if (event->key() == Qt::Key_Backspace)
    {
        editableCursor();
        auto cursor = textCursor();
        if (!cursor.hasSelection())
        {
            const int end = cursor.position();
            cursor.movePosition(control ? QTextCursor::PreviousWord : QTextCursor::PreviousCharacter);
            const int begin = std::max(m_inputStart, cursor.position());
            cursor.setPosition(end);
            cursor.setPosition(begin, QTextCursor::KeepAnchor);
        }
        cursor.removeSelectedText();
        setTextCursor(cursor);
        return;
    }
    if (!event->text().isEmpty() || event->key() == Qt::Key_Delete || event->matches(QKeySequence::Cut) ||
        event->matches(QKeySequence::Paste))
    {
        editableCursor();
    }
    QPlainTextEdit::keyPressEvent(event);
}
void ConsoleEditor::inputMethodEvent(QInputMethodEvent *event)
{
    if (m_accepting)
    {
        editableCursor();
        QPlainTextEdit::inputMethodEvent(event);
    }
}
void ConsoleEditor::insertFromMimeData(const QMimeData *source)
{
    if (m_accepting && source->hasText())
    {
        editableCursor();
        auto value = source->text().replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        if (m_rawInput)
        {
            value.replace('\n', ' ');
        }
        insertPlainText(value);
    }
}
void ConsoleEditor::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    menu.addAction(QStringLiteral("复制"), this, &QPlainTextEdit::copy)->setEnabled(textCursor().hasSelection());
    menu.addAction(QStringLiteral("全选输出"), this, &QPlainTextEdit::selectAll);
    menu.addAction(QStringLiteral("粘贴"), this, &QPlainTextEdit::paste)->setEnabled(m_accepting);
    menu.addSeparator();
    menu.addAction(QStringLiteral("清屏"), this, &ConsoleEditor::clearScreen);
    menu.exec(event->globalPos());
}
} // namespace reverse::workbench
