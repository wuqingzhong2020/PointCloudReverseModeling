/* 文件描述：局部重建参数、所选部件、异步预览与统一命令提交界面。 */
#pragma once

#include <QWidget>
#include <functional>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QCheckBox;
class QFormLayout;

namespace reverse::workbench
{
class LocalReconstructionTool final : public QWidget
{
  public:
    LocalReconstructionTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin();
    void cancel();
    void selectionChanged(const std::vector<ModelSelection> &selection);
    void sync(const ProjectSnapshot &project, bool idle);
    bool active() const
    {
        return m_active;
    }
    bool showingPreview() const
    {
        return m_showing;
    }
    std::function<void()> onChanged, onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void invalidate();
    void showModel(bool original);
    void preview();
    void apply();
    void updateInputs(bool idle);
    CommandService &m_service;
    OccViewport &m_viewport;
    LocalReconstruction m_request;
    std::shared_ptr<const Model> m_preview;
    std::uint64_t m_revision{};
    std::string m_task, m_applyTask;
    bool m_active{}, m_showing{}, m_loading{};
    QComboBox *m_type{}, *m_contour{};
    QDoubleSpinBox *m_corner{};
    QLabel *m_selection{}, *m_hint{}, *m_cornerLabel{}, *m_info{};
    QFormLayout *m_form{};
    QPushButton *m_previewButton{}, *m_apply{};
    QCheckBox *m_original{}, *m_mirrorBottom{};
};
} // namespace reverse::workbench
