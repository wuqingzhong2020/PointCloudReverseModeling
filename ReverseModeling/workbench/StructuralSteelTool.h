/* 文件描述：槽钢和角钢的选件、后台预览及可录制应用面板。 */
#pragma once
#include <QWidget>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
class QLabel;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QGroupBox;
namespace reverse::workbench
{
class StructuralSteelDiagram;
class StructuralSteelTool final : public QWidget
{
  public:
    StructuralSteelTool(CommandService &service, OccViewport &viewport, QWidget *parent);
    void begin(StructuralSteelKind kind);
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
    ModelPickMode pickMode() const
    {
        return ModelPickMode::Face;
    }
    std::function<void()> onChanged;
    std::function<void()> onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void invalidate();
    void preview();
    void apply();
    void showModel(bool original);
    void enableInputs(bool enabled);
    void updateDescription();
    CommandService &m_service;
    OccViewport &m_viewport;
    StructuralSteel m_request, m_previewRequest;
    std::vector<ModelSelection> m_selection;
    std::shared_ptr<const Model> m_preview;
    std::string m_task, m_applyTask;
    std::uint64_t m_revision{};
    bool m_active{}, m_previewShown{}, m_loading{}, m_discard{};
    QLabel *m_selected, *m_description, *m_hint;
    QGroupBox *m_parameterGroup;
    StructuralSteelDiagram *m_diagram;
    QDoubleSpinBox *m_height, *m_thickness;
    QCheckBox *m_autoHeight, *m_original;
    QPushButton *m_previewButton, *m_apply;
};
} // namespace reverse::workbench
