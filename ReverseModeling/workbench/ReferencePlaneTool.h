/* 文件描述：基准面的三点/特征面交互、预览确认和点云剖视面板；所有提交走命令服务。 */
#pragma once

#include <functional>
#include <QWidget>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"

class QLabel;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
namespace reverse::workbench
{
class ReferencePlaneTool final : public QWidget
{
public:
    ReferencePlaneTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin();
    void cancel();
    void sync(const ProjectSnapshot &project, bool idle, bool cloudVisible, bool modelVisible);
    bool picking() const { return m_mode != ReferencePlanePickMode::None; }
    std::function<void()> onChanged;
protected:
    void hideEvent(QHideEvent *event) override;
private:
    void choose(ReferencePlanePickMode mode);
    void picked(const std::vector<Vec3> &points, const QString &error);
    void submit(bool clear);
    void updateControls();
    CommandService &m_service;
    OccViewport &m_viewport;
    ProjectSnapshot m_project;
    Vec3 m_origin;
    std::string m_task;
    ReferencePlanePickMode m_mode{ReferencePlanePickMode::None};
    std::vector<Vec3> m_points;
    bool m_idle{}, m_cloudVisible{}, m_modelVisible{};
    QLabel *m_hint;
    QLabel *m_coordinates;
    QPushButton *m_three;
    QPushButton *m_face;
    QPushButton *m_flip;
    QPushButton *m_confirm;
    QPushButton *m_clear;
    QPushButton *m_front;
    QPushButton *m_sectionApply;
    QCheckBox *m_section;
    QCheckBox *m_reverse;
    QDoubleSpinBox *m_offset;
};
}
