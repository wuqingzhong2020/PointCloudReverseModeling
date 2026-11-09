/* 文件描述：交叉板 A/B 选择、高度差、异步预览及统一命令应用面板。 */
#pragma once
#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
#include <QWidget>
class QLabel;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
namespace reverse::workbench
{
class CrossPlateTool final : public QWidget
{
  public:
    CrossPlateTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin();
    void cancel();
    void selectionChanged(const std::vector<ModelSelection> &selections);
    void sync(const ProjectSnapshot &project, bool idle);
    bool active() const
    {
        return m_active;
    }
    bool showingPreview() const
    {
        return m_previewShown;
    }
    std::function<void()> onChanged, onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void choose(bool continuous);
    void invalidate();
    void preview();
    void apply();
    void showModel(bool original);
    void enableInputs(bool enabled);
    void updateSelection();
    CommandService &m_service;
    OccViewport &m_viewport;
    PlateEdit m_request, m_previewRequest;
    std::shared_ptr<const Model> m_preview;
    std::string m_task, m_applyTask;
    std::uint64_t m_revision{};
    bool m_active{}, m_previewShown{}, m_loading{}, m_discard{}, m_continuous{true};
    QLabel *m_hint;
    QPushButton *m_a, *m_b, *m_clear, *m_previewButton, *m_apply;
    QCheckBox *m_adjustHeight, *m_original;
    QDoubleSpinBox *m_height;
};
} // namespace reverse::workbench
