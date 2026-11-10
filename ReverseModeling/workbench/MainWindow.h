/*
 * 文件描述：
 * 定义 Qt 桌面工作台入口，界面通过命令服务访问点云、模型和后台任务。
 */
#pragma once

#include <memory>

#include <SARibbonMainWindow.h>

#include "commands/StartupArguments.h"

namespace reverse::workbench
{

enum class ActionId;

class MainWindow final : public SARibbonMainWindow
{
  public:
    explicit MainWindow(QWidget *parent = nullptr, StartupRequest startup = {});
    ~MainWindow() override;

  protected:
    void closeEvent(QCloseEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> state_;
    void initializeActions();
    void initializeViewport();
    void initializeInspector();
    void initializeCoreTools();
    void initializePlateInteraction();
    void initializeReconstructionTools();
    void connectThicknessEditing();
    void connectEditEscape();
    void initializeDockPanels();
    void connectPanelClosing();
    void initializeRibbonAndStatus();
    void connectFileActions();
    void connectProjectActions();
    void connectCloudSelection();
    void connectCloudContextMenu();
    void connectViewActions();
    void cancelModelTools();
    void connectSteelActions();
    void connectCreationActions();
    void connectAdjustmentActions();
    void connectHoleTransformActions();
    void connectPanelControls();
    void initializePolling();
    void modelContextMenu(const QPoint &position);
    void beginPlateEdit(const std::string &operation, bool all = false);
    void importCloud();
    void importCloud(const QString &path);
    void restoreRecognition();
    void cancelTask();
    void refreshProjectPanels();
    void restoreCommittedParameters();
    void reconstruct();
    void exportStep();
    void exportCwrm();
    void exportModelCloud();
    void exportAndExit();
    void continueStartup();
    void exportCloud();
    void evaluateDeviation();
    void pollTask();
    void updateActions();
    void refreshDisplay(bool fitView = true);
    void editCloud(ActionId action);
    void appendLog(const QString &message);
    void reportError(const QString &message, bool writeLog = true);
};

} // namespace reverse::workbench
