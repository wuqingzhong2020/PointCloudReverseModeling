/* 文件描述：命令交互窗口、输入历史和宏操作；不在 GUI 执行业务。 */
#pragma once
#include <QWidget>
#include <functional>
#include <memory>
#include <string>
namespace reverse
{
class CommandService;
}
namespace reverse::workbench
{
class WorkbenchActions;
class CommandConsoleWidget final : public QWidget
{
  public:
    explicit CommandConsoleWidget(CommandService &service, WorkbenchActions &actions, QWidget *parent = nullptr);
    ~CommandConsoleWidget() override;
    std::function<void()> onProjectChanged;
    std::function<void()> onShowRequested;
    void setWorkbenchBusy(bool busy);

  private:
    struct State;
    std::unique_ptr<State> m_state;
    void initializeMenu();
    void replayScript();
    void startRecording();
    void stopRecording();
    void refreshRecordingState();
    void submit(const QString &line);
    void interrupt();
    void finishPrompt();
    void beginTask(const std::string &id);
    void poll();
    void loadHistory();
    void saveHistory();
    void recall(int direction);
    void rememberInput(const QString &input);
    void pollUiCommands();
};
} // namespace reverse::workbench
