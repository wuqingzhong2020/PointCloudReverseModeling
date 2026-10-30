/*
 * 文件描述：
 * 定义 Qt 桌面工作台入口，界面通过命令服务访问点云、模型和后台任务。
 */
#pragma once

#include <memory>

#include <SARibbonMainWindow.h>

namespace reverse::workbench {

enum class ActionId;

class MainWindow final : public SARibbonMainWindow {
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct State;
    std::unique_ptr<State> state_;
    void importCloud();
    void importCloud(const QString& path);
    void restoreRecognition();
    bool applyParameters();
    void cancelTask();
    void refreshProjectPanels();
    void reconstruct();
    void exportStep();
    void exportCloud();
    void evaluateDeviation();
    void pollTask();
    void updateActions();
    void refreshDisplay(bool fitView = true);
    void editCloud(ActionId action);
    void appendLog(const QString& message);
    void reportError(const QString& message);
};

} // namespace reverse::workbench
