/*
 * 文件描述：
 * 定义建模参数面板和操作容器；模型调整下拉框与 Ribbon 复用动作入口。
 */
#pragma once

#include <array>

#include <QWidget>

#include "core/Types.h"
#include "workbench/WorkbenchActions.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;

namespace reverse::workbench
{
class ReconstructionPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit ReconstructionPanel(QWidget *parent = nullptr);
    BeamParameters values() const;
    void setValues(const BeamParameters &parameters);
    void setModelValues(const ModelSummary &model);
    void acceptEdits();
    bool hasEdits() const;
    void setEditable(bool editable, bool canRestore);
    void setSourceText(const QString &text);
    void setFrameText(const QString &text);
    void setHint(const QString &text);
    void setModelText(const QString &text);
    void setRecognition(const std::optional<PlateRecognition> &recognition);
    bool usePointCloud() const;
    bool mirrorSymmetry() const;
    double endOverhangToleranceMm() const;

  signals:
    void edited();
    void applyRequested();
    void restoreRequested();
    void closeRequested();

  private:
    std::array<QDoubleSpinBox *, 6> m_dimensions{};
    QLineEdit *m_stations;
    QCheckBox *m_swap;
    QCheckBox *m_mirror;
    QDoubleSpinBox *m_endOverhangTolerance;
    QComboBox *m_mode;
    QTableWidget *m_recognition;
    QGroupBox *m_basic;
    QLabel *m_source;
    QLabel *m_frame;
    QLabel *m_hint;
    QLabel *m_model;
    QPushButton *m_apply;
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
