/* 文件描述：异步圆角/倒角选边与预览面板；应用才提交实体和命令。 */
#pragma once
#include <functional>

#include <QWidget>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QCheckBox;
namespace reverse::workbench
{
class EdgeTreatmentTool final : public QWidget
{
  public:
    EdgeTreatmentTool(CommandService &service, OccViewport &viewport, QWidget *parent);
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
    std::function<void()> onChanged;
    std::function<void()> onCommitted;

  protected:
    void hideEvent(QHideEvent *event) override;

  private:
    void invalidate();
    void showModel(bool original);
    void updateSelectionText();
    void preview();
    void apply();
    void setInputsEnabled(bool enabled);
    CommandService &m_service;
    OccViewport &m_viewport;
    EdgeTreatment m_request, m_previewRequest;
    std::uint64_t m_revision{};
    std::vector<ModelSelection> m_selections;
    std::shared_ptr<const Model> m_preview;
    std::string m_task, m_applyTask;
    QString m_selectionError;
    bool m_active{}, m_loading{}, m_previewShown{}, m_discard{};
    QLabel *m_hint, *m_selection, *m_sizeLabel;
    QComboBox *m_kind;
    QDoubleSpinBox *m_size;
    QPushButton *m_previewButton, *m_apply;
    QCheckBox *m_original;
};
} // namespace reverse::workbench
