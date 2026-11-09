/*
 * 文件描述：
 * 定义建模参数面板和操作容器；模型调整下拉框与 Ribbon 复用动作入口。
 */
#pragma once

#include <QWidget>

#include "core/Types.h"
#include "workbench/WorkbenchActions.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;

namespace reverse::workbench
{
class ReconstructionPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit ReconstructionPanel(QWidget *parent = nullptr);
    PlateBuildOptions options() const;
    void setOptions(const PlateBuildOptions &options);
    void acceptEdits();
    bool hasEdits() const;
    void setEditable(bool editable);

  signals:
    void edited();
    void restoreRequested();
    void closeRequested();

  private:
    QComboBox *m_template{}, *m_tbarPreset{}, *m_support{};
    QWidget *m_hOptions{}, *m_tbarOptions{}, *m_otherOptions{};
    QCheckBox *m_regular{}, *m_projection{}, *m_perpendicular{};
    QDoubleSpinBox *m_slotWidth{}, *m_baseWidth{};
    void updateTBarPreset();
    QCheckBox *m_webHoles;
    QDoubleSpinBox *m_minimumHoleRadius;
    QDoubleSpinBox *m_endOverhangTolerance;
    QPushButton *m_restore;
    bool m_hasEdits{};
    bool m_settingValues{};
    void markEdited();
};

class OperationPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit OperationPanel(WorkbenchActions &actions, QWidget *parent = nullptr);
    void showOperation(ActionId id, const QString &title);
    void showCustom(const QString &title, QWidget *content, ActionId id = ActionId::Count);

  signals:
    void closeRequested();

  private:
    QLabel *m_title;
    QComboBox *m_adjustment;
    QStackedWidget *m_stack;
    QWidget *m_intro;
    QWidget *m_footer;
    QWidget *m_form{};
    ActionId m_current{ActionId::Count};
    QString m_currentTitle;
    void setOperationTitle(ActionId id, const QString &title);
};
} // namespace reverse::workbench
