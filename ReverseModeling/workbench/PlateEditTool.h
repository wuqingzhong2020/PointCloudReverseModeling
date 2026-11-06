/* 文件描述：多选板件关系编辑、孔洞检查和批量过焊孔的后台预览面板。 */
#pragma once
#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
#include <QWidget>
class QComboBox;
class QLabel;
class QPushButton;
class QCheckBox;
class QDoubleSpinBox;
class QTableWidget;
class QFormLayout;
class QHideEvent;
namespace reverse::workbench
{
class PlateEditTool final : public QWidget
{
  public:
    PlateEditTool(CommandService &, OccViewport &, QWidget *parent);
    void begin(const std::string &operation, const std::vector<std::string> &ids, bool all = false);
    void cancel();
    void selectionChanged(const std::vector<ModelSelection> &selection);
    void crop(const CloudSelectionRect &rectangle);
    void sync(const ProjectSnapshot &, bool idle);
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
    void hideEvent(QHideEvent *) override;

  private:
    void setup(const ProjectSnapshot &, bool all);
    void targetChanged();
    void invalidate();
    void showModel(bool original);
    void preview();
    void apply();
    void review();
    void updateInputs();
    void updateGuide();
    PlateEdit request() const;
    void fail(const QString &message);
    CommandService &m_service;
    OccViewport &m_viewport;
    bool m_active{}, m_loading{}, m_showing{};
    std::uint64_t m_revision{};
    std::string m_operation, m_previewTask, m_applyTask, m_reviewTask;
    std::vector<std::string> m_sources;
    std::map<std::string, std::vector<PlateHoleBase>> m_bases;
    std::optional<CloudSelectionRect> m_rectangle;
    PlateEdit m_prepared;
    std::shared_ptr<const Model> m_preview;
    QWidget *m_fields{};
    QFormLayout *m_form{};
    QComboBox *m_target{}, *m_shape{};
    QTableWidget *m_table{};
    QLabel *m_hint{}, *m_description{};
    QCheckBox *m_original{}, *m_boundary{};
    QPushButton *m_previewButton{}, *m_apply{}, *m_review{};
    std::map<std::string, QDoubleSpinBox *> m_numbers;
};
} // namespace reverse::workbench
