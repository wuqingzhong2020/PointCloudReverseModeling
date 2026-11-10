/* 文件描述：有序选点、尺寸调整、切除范围和参考面障碍物向导。 */
#pragma once
#include <QWidget>
#include "commands/CommandService.h"
#include "workbench/OccViewport.h"
class QComboBox;
class QDoubleSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QFormLayout;
class QTimer;
namespace reverse::workbench
{
class PointModelingTool final : public QWidget
{
public:
    PointModelingTool(CommandService &, OccViewport &, QWidget *parent);
    void begin(bool obstacle);
    void cancel();
    void sync(const ProjectSnapshot &, bool idle);
    bool active() const { return m_active; }
    bool showingPreview() const { return bool(m_preview); }
    bool selectingObstaclePoints() const { return m_active && m_obstacle && !m_facePicking && m_step == 1; }
    ModelPickMode inputPickMode() const
    {
        return m_facePicking ? ModelPickMode::Face : (m_obstacle || m_step==0) ? ModelPickMode::Vertex : ModelPickMode::Body;
    }
    std::function<void()> onChanged, onCommitted;
protected:
    void hideEvent(QHideEvent *) override;
private:
    void picked(const std::vector<Vec3> &, const QString &);
    void updateUi();
    void restart();
    void next();
    void invalidate();
    void preview();
    void apply();
    void showModel(bool original);
    void changed();
    void selectReference(bool clear);
    void selectObstaclePoints();
    void updateObstacleSelection();
    void syncSelection(const ProjectSnapshot &project);
    void finishPreviewTask(const ProjectSnapshot &project);
    void finishApplyTask();
    void traceState(const char *event, const QString &detail = {}) const noexcept;
    PointModeling request(bool inferred = false) const;
    Vec3 displayOrigin() const;
    CommandService &m_service;
    OccViewport &m_viewport;
    bool m_active{}, m_obstacle{}, m_loading{}, m_facePicking{}, m_pendingValid{}, m_clipDirty{true}, m_clipApplied{};
    int m_step{};
    std::vector<Vec3> m_points;
    std::optional<ReferencePlane> m_plane;
    std::shared_ptr<const Model> m_preview;
    std::shared_ptr<const CloudSelection> m_selection;
    std::shared_ptr<const CloudSelection> m_obstacleSelection;
    std::size_t m_retainedPoints{};
    std::vector<ModelSelection> m_referenceSelection;
    std::string m_task, m_applyTask, m_cloudId, m_modelId;
    std::uint64_t m_revision{};
    PointModeling m_submitted;
    QComboBox *m_shape{}, *m_method{}, *m_direction{}, *m_operation{}, *m_scope{};
    QDoubleSpinBox *m_width{}, *m_length{}, *m_diameter{}, *m_height{}, *m_offset{};
    QCheckBox *m_toPlane{}, *m_section{}, *m_reverse{}, *m_original{};
    QLabel *m_stage{}, *m_hint{}, *m_instructions{}, *m_description{}, *m_selectionInfo{};
    QWidget *m_referenceRow{}, *m_obstacleGroup{}, *m_referenceHelp{};
    QFormLayout *m_form{};
    QPushButton *m_next{}, *m_back{}, *m_redraw{}, *m_apply{}, *m_previewButton{}, *m_face{};
    QPushButton *m_clearReference{}, *m_selectCloud{}, *m_previewAllPoints{};
    QTimer *m_debounce{};
};
}
