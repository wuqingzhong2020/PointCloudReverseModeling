/* 文件描述：统一命令输入、界面操作回显和宏脚本交互；执行和 stdin 等待均由命令工作线程承担。 */
#include <algorithm>

#include <QAction>
#include <QDir>
#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QMenu>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include "commands/CommandService.h"
#include "commands/StartupArguments.h"
#include "commands/CommandFeedback.h"
#include "workbench/CommandConsoleWidget.h"
#include "workbench/ConsoleEditor.h"
#include "workbench/WorkbenchActions.h"

namespace reverse::workbench
{
namespace
{
QString text(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}
} // namespace
struct CommandConsoleWidget::State
{
    CommandService &service;
    WorkbenchActions &actions;
    bool workbenchBusy{};
    bool moreInput{};
    QLabel *recordingStatus{};
    QString recordingError;
    ConsoleEditor *console{};
    QLabel *status{};
    QToolButton *cancel{};
    QToolButton *moreMenu{};
    QAction *helpAction{};
    QString taskId;
    std::size_t outputSize{};
    std::uint64_t inputRequest{};
    QStringList history;
    int historyIndex{};
    QString draft;
    State(CommandService &service, WorkbenchActions &actions) : service(service), actions(actions)
    {
    }
};
CommandConsoleWidget::CommandConsoleWidget(CommandService &service, WorkbenchActions &actions, QWidget *parent)
    : QWidget(parent), m_state(std::make_unique<State>(service, actions))
{
    setObjectName(QStringLiteral("panel.commandConsole"));
    setMinimumHeight(180);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(6, 4, 6, 4);
    auto *toolbar = new QHBoxLayout;
    m_state->status = new QLabel(this);
    toolbar->addWidget(m_state->status, 1);
    m_state->cancel = new QToolButton(this);
    m_state->cancel->setText(QStringLiteral("中断 Ctrl+C"));
    connect(m_state->cancel, &QToolButton::clicked, this, [this] { interrupt(); });
    toolbar->addWidget(m_state->cancel);
    m_state->moreMenu = new QToolButton(this);
    m_state->moreMenu->setText(QStringLiteral("更多"));
    m_state->moreMenu->setPopupMode(QToolButton::InstantPopup);
    toolbar->addWidget(m_state->moreMenu);
    layout->addLayout(toolbar);
    m_state->recordingStatus = new QLabel(this);
    m_state->recordingStatus->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_state->recordingStatus->setStyleSheet(QStringLiteral("color: #ed6267;"));
    layout->addWidget(m_state->recordingStatus);
    connect(actions.get(ActionId::StartRecording), &QAction::triggered, this, [this] { startRecording(); });
    connect(actions.get(ActionId::StopRecording), &QAction::triggered, this, [this] { stopRecording(); });
    connect(actions.get(ActionId::ExecuteScript), &QAction::triggered, this, [this] { replayScript(); });
    m_state->console = new ConsoleEditor(this);
    layout->addWidget(m_state->console, 1);
    m_state->console->onSubmit = [this](const QString &line) { submit(line); };
    m_state->console->onInterrupt = [this] { interrupt(); };
    m_state->console->onHistory = [this](int direction) { recall(direction); };
    m_state->console->onEof = [this] {
        if (!m_state->taskId.isEmpty() &&
            m_state->service.provideInput(m_state->taskId.toStdString(), m_state->inputRequest, std::nullopt))
        {
            m_state->console->takeInput();
        }
    };
    initializeMenu();
    loadHistory();
    m_state->console->writeOutput(QStringLiteral("命令窗口 · 输入 help 查看命令用法\n"
                                                 "支持 Python，如 print(\"aaaa\")；多行语句以空行结束。\n"
                                                 "Enter 执行，↑↓ 历史，Tab 补全。\n"));
    finishPrompt();
    auto *timer = new QTimer(this);
    timer->setInterval(50);
    connect(timer, &QTimer::timeout, this, [this] { poll(); });
    timer->start();
}
CommandConsoleWidget::~CommandConsoleWidget()
{
    saveHistory();
}
void CommandConsoleWidget::initializeMenu()
{
    auto *menu = new QMenu(m_state->moreMenu);
    m_state->moreMenu->setMenu(menu);
    m_state->helpAction = menu->addAction(QStringLiteral("命令帮助"), this, [this] {
        m_state->console->replaceInput(QStringLiteral("help"));
        submit(m_state->console->takeInput());
    });
    menu->addSeparator();
    auto *copy = menu->addAction(QStringLiteral("复制所选 Ctrl+C"), m_state->console, &ConsoleEditor::copy);
    copy->setObjectName(QStringLiteral("command.copy"));
    copy->setEnabled(false);
    connect(m_state->console, &ConsoleEditor::copyAvailable, copy, &QAction::setEnabled);
    auto *clear = menu->addAction(QStringLiteral("清空输出 Ctrl+L"), this, [this] { m_state->console->clearScreen(); });
    clear->setObjectName(QStringLiteral("command.clear"));
    auto *tools = new QHBoxLayout;
    for (auto *action : {copy, clear})
    {
        auto *button = new QToolButton(this);
        button->setDefaultAction(action);
        tools->addWidget(button);
    }
    tools->addStretch();
    static_cast<QVBoxLayout *>(layout())->insertLayout(1, tools);
    clear->setToolTip(QStringLiteral("清空命令和日志显示，保留当前输入、输入历史及录制脚本。"));
}
void CommandConsoleWidget::replayScript()
{
    const auto path =
        QFileDialog::getOpenFileName(this, QStringLiteral("执行脚本"), {}, QStringLiteral("命令脚本 (*.py)"));
    if (path.isEmpty())
    {
        return;
    }
    try
    {
        const auto id = m_state->service.submitScriptFile(std::filesystem::path(path.toStdWString()));
        if (onShowRequested)
        {
            onShowRequested();
        }
        m_state->console->takeInput();
        m_state->console->writeOutput(QStringLiteral("执行脚本：%1\n").arg(path));
        beginTask(id);
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
    }
}
void CommandConsoleWidget::executeStartup(const StartupRequest &request)
{
    if (request.mode == StartupMode::None) return;
    if (onShowRequested) onShowRequested();
    m_state->console->takeInput();
    if (request.mode == StartupMode::Error || request.mode == StartupMode::Help)
    {
        if (!request.error.empty()) m_state->console->writeOutput(text(request.error) + '\n');
        m_state->console->writeOutput(text(startupUsage()));
        finishPrompt();
        return;
    }
    try
    {
        if (request.mode == StartupMode::Command)
        {
            const auto line = request.arguments.size() == 1 ? request.arguments.front()
                                                           : commandArgumentsText(request.arguments);
            m_state->console->writeCommand(text(line));
            beginTask(m_state->service.submitCommandArguments(request.arguments));
        }
        else
        {
            const auto &value = request.arguments.front();
            const auto path = std::filesystem::path(std::u8string(value.begin(), value.end()));
            m_state->console->writeOutput(QStringLiteral("执行脚本：") +
                text(commandArgumentsText(request.arguments)) + '\n');
            beginTask(m_state->service.submitScriptFile(path,
                {request.arguments.begin() + 1, request.arguments.end()}));
        }
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
        finishPrompt();
    }
}
void CommandConsoleWidget::beginTask(const std::string &id)
{
    m_state->taskId = text(id);
    m_state->outputSize = 0;
    m_state->inputRequest = 0;
    m_state->helpAction->setEnabled(false);
    m_state->cancel->setEnabled(true);
    m_state->status->setText(QStringLiteral("执行中…"));
    refreshRecordingState();
}
void CommandConsoleWidget::finishPrompt()
{
    m_state->helpAction->setEnabled(true);
    m_state->cancel->setEnabled(false);
    m_state->status->setText(m_state->moreInput ? QStringLiteral("继续输入 · 空行结束 · Ctrl+C 取消")
                                             : QStringLiteral("Enter 执行 · Ctrl+L 清屏"));
    QStringList completions;
    for (const auto &value : m_state->service.completions())
    {
        completions.push_back(text(value));
    }
    m_state->console->setCompletions(std::move(completions));
    m_state->console->showPrompt(m_state->moreInput ? QStringLiteral("... ") : QStringLiteral("rm> "));
    refreshRecordingState();
}
void CommandConsoleWidget::submit(const QString &line)
{
    if (!m_state->taskId.isEmpty())
    {
        if (!m_state->service.provideInput(m_state->taskId.toStdString(), m_state->inputRequest,
                                           line.toUtf8().toStdString()))
        {
            m_state->console->writeOutput(QStringLiteral("输入请求已失效，或输入超过 1 MiB。\n"));
            const auto task = m_state->service.task(m_state->taskId.toStdString());
            if (task.waitingForInput)
            {
                m_state->console->showPrompt({}, {}, true);
            }
        }
        return;
    }
    if (m_state->workbenchBusy)
    {
        m_state->console->writeOutput(QStringLiteral("请等待当前工作台任务结束。\n"));
        finishPrompt();
        m_state->console->replaceInput(line);
        return;
    }
    const auto value = line.trimmed().isEmpty() ? QString{} : line;
    try
    {
        const auto id = m_state->service.submitLine(value.toUtf8().toStdString());
        beginTask(id);
        if (!value.isEmpty())
        {
            rememberInput(value);
            saveHistory();
        }
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
        finishPrompt();
        m_state->console->replaceInput(value);
    }
}
void CommandConsoleWidget::interrupt()
{
    if (m_state->console->acceptingInput())
    {
        m_state->console->takeInput();
    }
    if (!m_state->taskId.isEmpty())
    {
        (void)m_state->service.cancel(m_state->taskId.toStdString());
        m_state->status->setText(QStringLiteral("正在中断…"));
        return;
    }
    m_state->console->writeOutput(QStringLiteral("已取消当前输入。\n"));
    if (m_state->moreInput)
    {
        m_state->moreInput = false;
        beginTask(m_state->service.resetPythonConsole());
        return;
    }
    finishPrompt();
}
void CommandConsoleWidget::poll()
{
    pollUiCommands();
    refreshRecordingState();
    if (m_state->taskId.isEmpty())
    {
        return;
    }
    TaskSnapshot task;
    try
    {
        task = m_state->service.task(m_state->taskId.toStdString());
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
        m_state->taskId.clear();
        finishPrompt();
        return;
    }
    if (task.output.size() > m_state->outputSize)
    {
        m_state->console->writeOutput(text(task.output.substr(m_state->outputSize)));
        m_state->outputSize = task.output.size();
    }
    if (!task.finished())
    {
        if (task.waitingForInput && task.inputRequest != m_state->inputRequest)
        {
            m_state->inputRequest = task.inputRequest;
            m_state->console->showPrompt({}, {}, true);
            m_state->console->setFocus();
        }
        m_state->status->setText(task.waitingForInput ? QStringLiteral("等待回答 · Enter 发送 · Ctrl+D 结束输入")
                                 : task.state == TaskState::Queued
                                     ? QStringLiteral("排队中…")
                                     : QStringLiteral("执行中 %1% · Ctrl+C 中断").arg(task.progress * 100, 0, 'f', 0));
        return;
    }
    if (m_state->console->acceptingInput())
    {
        m_state->console->takeInput();
    }
    const auto &value = task.result.value;
    const bool consoleResult = task.operation == "Command" && value.is<rm::Arguments>() &&
                               value.as<rm::Arguments>().contains("more") &&
                               value.as<rm::Arguments>().at("more").is<bool>();
    m_state->moreInput = task.result.ok() && consoleResult && value.as<rm::Arguments>().at("more").as<bool>();
    if (!task.result.ok())
    {
        // A script command error is already printed alongside the offending command.
        const bool reported = task.operation == "ExecuteScript" && !task.result.message.empty() &&
                              task.output.ends_with("未完成：" + task.result.message + '\n');
        if (!reported)
        {
            m_state->console->writeOutput(task.state == TaskState::Cancelled ? QStringLiteral("任务已取消。\n")
                                                                             : text(commandFeedback(task.result)) + '\n');
        }
    }
    else if (!consoleResult)
    {
        const auto feedback = commandFeedback(task.result);
        if (!feedback.empty())
        {
            m_state->console->writeOutput(text(feedback) + '\n');
        }
    }
    if (task.result.committed && onProjectChanged)
    {
        onProjectChanged();
    }
    m_state->taskId.clear();
    finishPrompt();
}
void CommandConsoleWidget::rememberInput(const QString &input)
{
    auto &history = m_state->history;
    if (history.isEmpty() || history.back() != input)
    {
        history.push_back(input);
    }
    while (history.size() > 200)
    {
        history.removeFirst();
    }
    m_state->historyIndex = static_cast<int>(history.size());
}
void CommandConsoleWidget::appendLog(const QString &message)
{
    // Drain earlier worker events before adding a GUI message, without consuming its input draft.
    pollUiCommands();
    m_state->console->writeOutput(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss  ")) +
                                  message + '\n');
}
void CommandConsoleWidget::flushUiCommands()
{
    poll();
}
void CommandConsoleWidget::pollUiCommands()
{
    const auto events = m_state->service.takeUiCommandEvents();
    bool historyChanged = false;
    for (const auto &event : events)
    {
        if (event.kind == UiCommandEventKind::Started)
        {
            const auto native = text(event.commandLine);
            m_state->console->writeCommand(native);
            m_state->console->writeOutput(text(commandExecutionEcho(event.commandLine)));
            rememberInput(native);
            historyChanged = true;
        }
        else if (event.kind == UiCommandEventKind::Output)
        {
            m_state->console->writeOutput(text(event.message));
        }
        else
        {
            m_state->console->writeOutput((event.succeeded ? QString{} : QStringLiteral("未完成：")) +
                                          text(event.message) + '\n');
        }
    }
    if (historyChanged) saveHistory();
}
void CommandConsoleWidget::startRecording()
{
    QFileDialog dialog(this, QStringLiteral("选择录制脚本"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setOption(QFileDialog::DontConfirmOverwrite, true);
    dialog.setNameFilter(QStringLiteral("命令脚本 (*.py)"));
    dialog.setDefaultSuffix(QStringLiteral("py"));
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty())
    {
        return;
    }
    const auto path = dialog.selectedFiles().front();
    try
    {
        // Existing scripts are overwritten; the recorder reports this in the command window.
        const auto id = m_state->service.submitStartRecording(std::filesystem::path(path.toStdWString()), true);
        if (onShowRequested)
        {
            onShowRequested();
        }
        m_state->console->takeInput();
        beginTask(id);
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
    }
}
void CommandConsoleWidget::stopRecording()
{
    try
    {
        const auto id = m_state->service.submitStopRecording();
        if (onShowRequested)
        {
            onShowRequested();
        }
        m_state->console->takeInput();
        beginTask(id);
    }
    catch (const std::exception &error)
    {
        m_state->console->writeOutput(text(error.what()) + '\n');
    }
}
void CommandConsoleWidget::setWorkbenchBusy(bool busy)
{
    m_state->workbenchBusy = busy;
    refreshRecordingState();
}
void CommandConsoleWidget::refreshRecordingState()
{
    const auto recording = m_state->service.recordingState();
    const bool idle = !m_state->workbenchBusy && m_state->taskId.isEmpty();
    m_state->actions.get(ActionId::StartRecording)->setEnabled(idle && !recording.active);
    m_state->actions.get(ActionId::StopRecording)->setEnabled(idle && recording.active);
    m_state->actions.get(ActionId::ExecuteScript)->setEnabled(idle);
    const auto path = text(recording.pathUtf8);
    m_state->recordingStatus->setVisible(recording.active);
    m_state->recordingStatus->setText(
        QStringLiteral("● 正在录制：%1（%2 条）").arg(QFileInfo(path).fileName()).arg(recording.commandCount));
    m_state->recordingStatus->setToolTip(path);
    const auto error = text(recording.error);
    if (!error.isEmpty() && error != m_state->recordingError)
    {
        if (onShowRequested)
        {
            onShowRequested();
        }
        m_state->console->writeOutput(error + '\n');
    }
    m_state->recordingError = error;
}
void CommandConsoleWidget::recall(int direction)
{
    const auto &history = m_state->history;
    auto &index = m_state->historyIndex;
    if (index == history.size())
    {
        m_state->draft = m_state->console->input();
    }
    index = std::clamp(index + direction, 0, static_cast<int>(history.size()));
    m_state->console->replaceInput(index == history.size() ? m_state->draft : history[index]);
}
void CommandConsoleWidget::loadHistory()
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
               QStringLiteral("/command-history.json"));
    if (!file.open(QIODevice::ReadOnly) || file.size() > 4 * 1024 * 1024)
    {
        return;
    }
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(QStringLiteral("version")).toInt() != 1)
    {
        return;
    }
    for (const auto entry : json.value(QStringLiteral("command")).toArray())
    {
        if (entry.isString())
        {
            m_state->history.push_back(entry.toString());
        }
    }
    while (m_state->history.size() > 200)
    {
        m_state->history.removeFirst();
    }
    m_state->historyIndex = static_cast<int>(m_state->history.size());
}
void CommandConsoleWidget::saveHistory()
{
    const auto directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!QDir().mkpath(directory))
    {
        return;
    }
    QSaveFile file(directory + QStringLiteral("/command-history.json"));
    if (!file.open(QIODevice::WriteOnly))
    {
        return;
    }
    const QJsonObject json{{QStringLiteral("version"), 1},
                           {QStringLiteral("command"), QJsonArray::fromStringList(m_state->history)}};
    const auto contents = QJsonDocument(json).toJson();
    if (file.write(contents) == contents.size())
    {
        (void)file.commit();
    }
}
} // namespace reverse::workbench
