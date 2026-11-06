/* 文件描述：添加过焊孔/底边孔的参数面板，后台预览及显式应用。 */
#pragma once
#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
#include <QWidget>
class QComboBox;
class QTabBar;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QCheckBox;
namespace reverse::workbench
{
class HoleTool final : public QWidget
{
  public:
    HoleTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin(PlateHoleKind kind);
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
    std::function<void()> onChanged;
    std::function<void()> onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void invalidate();
    void showModel(bool original);
    void updateFields();
    PlateHole currentRequest() const;
    void updateGuide();
    void preview();
    void apply();
    void setInputsEnabled(bool enabled);
    CommandService &m_service;
    OccViewport &m_viewport;
    PlateHole m_request, m_previewRequest;
    std::vector<PlateHoleBase> m_bases;
    std::vector<ModelSelection> m_selections;
    std::shared_ptr<const Model> m_preview;
    std::uint64_t m_revision{};
    std::string m_task, m_inspectTask, m_applyTask;
    bool m_active{}, m_loading{}, m_previewShown{}, m_discard{};
    QLabel *m_hint, *m_selection, *m_sizeLabel, *m_widthLabel, *m_heightLabel, *m_referenceLabel, *m_offsetLabel;
    QTabBar *m_kind, *m_shape;
    QComboBox *m_edge, *m_reference;
    QDoubleSpinBox *m_size, *m_width, *m_height, *m_offset;
    QCheckBox *m_first, *m_second, *m_original;
    QPushButton *m_previewButton, *m_apply;
};
} // namespace reverse::workbench
