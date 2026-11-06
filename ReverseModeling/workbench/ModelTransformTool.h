/* 文件描述：选择部件后移动/旋转的数值面板；临时预览只在确定时提交一次可录制命令。 */
#pragma once
#include <array>
#include <functional>

#include <QWidget>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QPushButton;
namespace reverse::workbench
{
class ModelTransformTool final : public QWidget
{
  public:
    ModelTransformTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin(ModelTransformKind kind);
    void cancel();
    bool active() const
    {
        return m_active;
    }
    void sync(const ProjectSnapshot &project, bool idle, bool visible);
    std::function<void()> onChanged;
    std::function<void()> onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void updatePreview();
    void fillControls();
    void confirm();
    CommandService &m_service;
    OccViewport &m_viewport;
    ModelTransform m_request;
    Vec3 m_defaultCenter;
    std::vector<std::string> m_ids;
    std::uint64_t m_revision{};
    std::string m_task;
    bool m_active{}, m_loading{};
    QGroupBox *m_move, *m_rotate;
    QLabel *m_hint;
    std::array<QDoubleSpinBox *, 3> m_offset, m_center;
    QDoubleSpinBox *m_angle;
    QComboBox *m_axis;
    QPushButton *m_confirm;
};
} // namespace reverse::workbench
