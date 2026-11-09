/* 文件描述：封板拾取、独立双边参数、模型标记、异步预览及原子应用面板。 */
#pragma once
#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
#include <QWidget>
class QLabel;
class QPushButton;
class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
namespace reverse::workbench
{
class EndCapTool final : public QWidget
{
  public:
    EndCapTool(CommandService &service, OccViewport &viewport, QWidget *parent);
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
        return m_previewShown;
    }
    bool keepActiveOnRefresh() const
    {
        return m_active && (!m_applyTask.empty() || m_commitRefresh);
    }
    std::function<void()> onChanged, onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    struct Draft
    {
        bool convex{}, automatic{true};
        double inset{15}, outset{50};
    };
    void selectEdge(int edge);
    void loadDraft();
    void saveDraft();
    void inspect();
    void preview();
    void apply();
    void invalidate();
    void showModel(bool original);
    void markers(bool enabled);
    void enableInputs();
    CommandService &m_service;
    OccViewport &m_viewport;
    std::array<Draft, 2> m_drafts;
    std::optional<EndCapInspection> m_info;
    std::string m_part, m_inspectTask, m_task, m_applyTask;
    std::uint64_t m_revision{};
    int m_edge{1};
    bool m_active{}, m_loading{}, m_discard{}, m_previewShown{}, m_commitRefresh{};
    PlateEdit m_previewRequest;
    std::shared_ptr<const Model> m_preview;
    QLabel *m_hint, *m_distanceInfo, *m_modeLabel;
    QPushButton *m_pick, *m_edges[2], *m_modes[2], *m_previewButton, *m_apply;
    QComboBox *m_distanceMode;
    QDoubleSpinBox *m_distance;
    QCheckBox *m_original;
    QWidget *m_diagram;
};
} // namespace reverse::workbench
