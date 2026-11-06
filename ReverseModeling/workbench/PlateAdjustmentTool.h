/* 文件描述：等厚板件的板厚、外轮廓及镜像补全，共用后台计算、预览及显式应用流程。 */
#pragma once

#include <QString>
#include <QWidget>

#include "commands/CommandService.h"
#include "workbench/OccViewport.h"

class QComboBox;
class QCheckBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
namespace reverse::workbench
{
enum class PlateAdjustmentKind
{
    Thickness,
    Contour,
    Mirror
};
class PlateAdjustmentTool final : public QWidget
{
  public:
    PlateAdjustmentTool(CommandService &service, OccViewport &viewport, PlateAdjustmentKind kind, QWidget *parent);
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
    void setInputsEnabled(bool enabled);
    void preview();
    void apply();
    CommandService &m_service;
    OccViewport &m_viewport;
    struct Request
    {
        std::vector<std::string> partIds;
        double valueMm{};
        std::string planePartId;
    };
    const PlateAdjustmentKind m_kind;
    Request m_request, m_previewRequest;
    std::shared_ptr<const Model> m_preview;
    std::uint64_t m_revision{};
    std::string m_task, m_applyTask, m_inspectTask;
    QString m_selectionError;
    bool m_active{}, m_loading{}, m_previewShown{}, m_discard{};
    QLabel *m_selection, *m_current, *m_hint;
    QDoubleSpinBox *m_size;
    QComboBox *m_plane;
    QCheckBox *m_original;
    QPushButton *m_previewButton, *m_apply;
};
} // namespace reverse::workbench
