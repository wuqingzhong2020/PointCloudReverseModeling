/* 文件描述：剖视下拉面板，选择底板面/基准面、偏移及保留侧，通过命令服务同步显示状态。 */
#pragma once

#include <functional>

#include <QMenu>

#include "commands/CommandService.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

namespace reverse::workbench
{
class SectionTool final : public QMenu
{
  public:
    explicit SectionTool(CommandService &service, QWidget *parent);
    void sync(const ProjectSnapshot &project, bool idle);
    void setSectionEnabled(bool enabled);
    std::function<void()> onChanged;

  private:
    void apply();
    void submit(PlaneSection section);
    void updateControls();
    CommandService &m_service;
    ProjectSnapshot m_project;
    std::string m_task;
    bool m_idle{};
    QComboBox *m_plane;
    QDoubleSpinBox *m_offset;
    QCheckBox *m_reverse;
    QLabel *m_hint;
};
} // namespace reverse::workbench
