/*
 * 文件描述：
 * 定义有效建模参数面板与尚未接入几何操作的表单容器。
 */
#pragma once

#include <array>

#include <QWidget>

#include "core/Types.h"
#include "workbench/WorkbenchActions.h"

class QCheckBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;

namespace reverse::workbench
{
class ReconstructionPanel final : public QWidget
{
    Q_OBJECT
  public:
    explicit ReconstructionPanel(QWidget *parent = nullptr);
    BeamParameters values() const;
    void setValues(const BeamParameters &parameters);
    void acceptEdits();
    bool hasEdits() const;
    void setEditable(bool editable, bool canRestore);
    void setSourceText(const QString &text);
    void setFrameText(const QString &text);
    void setHint(const QString &text);
    void setModelText(const QString &text);

  signals:
    void edited();
    void applyRequested();
    void restoreRequested();
    void closeRequested();

  private:
    std::array<QDoubleSpinBox *, 6> m_dimensions{};
    QLineEdit *m_stations;
    QCheckBox *m_swap;
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
    explicit OperationPanel(QWidget *parent = nullptr);
    void showOperation(ActionId id, const QString &title);

  signals:
    void closeRequested();

  private:
    QLabel *m_title;
    QStackedWidget *m_stack;
    QWidget *m_form{};
    ActionId m_current{ActionId::Count};
    QString m_currentTitle;
};
} // namespace reverse::workbench
