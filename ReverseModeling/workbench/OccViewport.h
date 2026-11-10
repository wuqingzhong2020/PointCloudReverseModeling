/*
 * 文件描述：
 * 定义仅属于 Qt 桌面层的 OCCT 视图，接收与几何内核无关的显示数据。
 */
#pragma once

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include <QWidget>

#include "workbench/ModelDisplayData.h"
#include "workbench/PointCloudDisplayData.h"
#include "workbench/PointMeasurement.h"
#include "core/ReferencePlane.h"
#include "geometry/ModelTransform.h"

namespace reverse::workbench
{

enum class ReferencePlanePickMode
{
    None,
    ThreePoints,
    Face,
    OrderedPoints
};

class OccViewport final : public QWidget
{
  public:
    explicit OccViewport(QWidget *parent = nullptr);
    ~OccViewport() override;

    void setPointCloud(std::vector<CloudDisplayPoint> points, std::shared_ptr<const CloudSelection> selection = {});
    void setCloudHeightColors(bool enabled);
    void setCloudEditingEnabled(bool enabled);
    void setCloudSelectionMode(CloudSelectionMode mode);
    void setCloudSelection(std::shared_ptr<const CloudSelection> selection, bool preserveRectangle = false);
    void setModel(std::vector<ModelDisplayPart> parts);
    void setModelPickingEnabled(bool enabled);
    void setModelPickMode(ModelPickMode mode);
    void setSelectedPart(const std::string &id);
    void setSelectedParts(const std::vector<std::string> &ids);
    [[nodiscard]] std::vector<std::string> selectedParts() const;
    void setSelectedObjects(const std::vector<ModelSelection> &selections);
    [[nodiscard]] const std::vector<ModelSelection> &selectedObjects() const;
    void setCloudVisible(bool visible);
    void setModelVisible(bool visible);
    void setModelWireframe(bool wireframe);
    void setThicknessVisible(bool visible);
    void setThicknessEditingEnabled(bool enabled);
    std::function<bool(const std::string &)> onThicknessLabelEditRequested;
    std::function<void(const std::string &, double)> onThicknessLabelSubmitted;
    void setMeasurementEnabled(bool enabled);
    void setHoleGuide(const std::vector<Vec3> &points); // 视图局部坐标中的底边端点 1、中点、端点 2。
    void setEndCapEdges(const std::vector<Vec3> &points, int activeEdge, bool enabled = true);
    std::function<void(int)> onEndCapEdgeSelected;
    void setHolePreview(const PlateHoleBase &base, const PlateHole &request, const Vec3 &origin);
    void clearHolePreview();
    void setBatchHolePreview(const std::vector<std::pair<PlateHoleBase, PlateHole>> &items, const Vec3 &origin);
    void setPlateCropEnabled(bool enabled, bool preserveRectangle = false);
    void setPlateEditReference(const std::vector<ModelDisplayPart> &original, const std::vector<std::string> &sources,
                               const std::string &target, bool removedMaterial);
    void clearPlateEditReference();
    std::function<void(const CloudSelectionRect &)> onPlateCrop;
    void setReferencePlane(std::shared_ptr<const ReferencePlane> plane, PlaneSection section, const Vec3 &origin,
                           std::optional<ReferencePlane> sectionPlane);
    void setSectionPreviewVisible(bool visible);
    void setReferencePlanePicking(ReferencePlanePickMode mode);
    void setPointModelingPicking(bool enabled, bool face = false);
    std::function<void(const std::vector<Vec3> &, const QString &)> onModelingPicked;
    void setReferencePreview(std::vector<Vec3> points);
    std::function<void(const std::vector<Vec3> &, const QString &)> onReferencePicked;
    std::function<void()> onReferenceCancelled;
    bool beginTransformPreview(const std::vector<std::string> &ids, Vec3 origin, const ModelTransform &request);
    void updateTransformPreview(const ModelTransform &request);
    void endTransformPreview();
    std::function<void(const ModelTransform &)> onTransformDragged;
    std::function<void()> onTransformCancelled;
    std::function<bool()> onEditEscape;
    std::function<bool()> hasEditContextMenu;
    void fitAll();
    void focusPart(const std::string &id);
    void showBottomView(const Vec3 &normal, const Vec3 &longitudinal);
    std::function<void(const QString &)> onError;
    std::function<void(const CloudSelectionRect &, CloudSelectionMode, bool editLast)> onCloudSelection;
    std::function<void(const QPoint &)> onCloudContextMenu;
    std::function<void(const std::vector<ModelSelection> &)> onModelObjectsSelected;
    std::function<void(bool)> onMeasurementModeChanged;
    std::function<void(const PointMeasurementResult &)> onMeasurementCompleted;

  protected:
    QPaintEngine *paintEngine() const override;
    bool event(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

  private:
    struct State;
    std::unique_ptr<State> state_;
    void initialize();
    void release() noexcept;
    void rebuildCloud();
    void rebuildMesh();
    void redraw();
    void updateFocusLocator();
    void updateEndCapEdges();
    void createViewCube();
    void updateViewCubeScale();
    void advanceNavigationAnimation();
    void stopNavigationAnimation();
    void fail(const QString &message);
    void clearSelectionRectangle();
    void updateMeasurementPicking();
    void updateReferencePresentation();
    void updateCloudClip();
    void rebuildHolePreview();
    void pickReference(const QPoint &pixel);
    void detectObject(const QPoint &pixel);
    std::optional<MeasurementPoint> measurementPoint(const QPoint &pixel) const;
    bool canSelectRectangle() const;
    void updateSelectionCursor(const QPoint &current);
    bool finishSelectionRectangle(const QPoint &current);
    void drawSelectionRectangle();
    CloudSelectionRect selectionRectangle() const;
};

} // namespace reverse::workbench
