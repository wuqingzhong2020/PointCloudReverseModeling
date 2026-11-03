/* 文件描述：带受保护历史区的终端式编辑器；只处理 Qt 输入与显示。 */
#pragma once
#include <QPlainTextEdit>
#include <QStringList>
#include <functional>

namespace reverse::workbench
{
class ConsoleEditor final : public QPlainTextEdit
{
  public:
    explicit ConsoleEditor(QWidget *parent = nullptr);
    void showPrompt(const QString &prompt, const QString &initial = {}, bool rawInput = false);
    QString takeInput();
    QString input() const;
    void replaceInput(const QString &value);
    void writeOutput(const QString &value);
    void clearScreen();
    bool acceptingInput() const;
    void setCompletions(QStringList values);
    std::function<void(const QString &)> onSubmit;
    std::function<void()> onInterrupt;
    std::function<void()> onEof;
    std::function<void(int)> onHistory;

  protected:
    void keyPressEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void insertFromMimeData(const QMimeData *source) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

  private:
    void editableCursor();
    void completeWord();
    int m_promptStart{};
    int m_inputStart{};
    bool m_accepting{};
    bool m_rawInput{};
    QString m_prompt;
    QStringList m_completions;
};
} // namespace reverse::workbench
