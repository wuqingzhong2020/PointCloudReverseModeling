/* 文件描述：视口各实现文件共用的私有显示状态；只在 GUI 线程访问。 */
#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <QPoint>

#include <AIS_InteractiveContext.hxx>
#include <AIS_PointCloud.hxx>
#include <AIS_Point.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_ViewCube.hxx>
#include <AIS_RubberBand.hxx>
#include <Aspect_DisplayConnection.hxx>
#include <OpenGl_GraphicDriver.hxx>
#include <V3d_View.hxx>
#include <V3d_Viewer.hxx>
#include <Graphic3d_ClipPlane.hxx>
#include <SelectMgr_Filter.hxx>

#include "workbench/OccViewport.h"
#include "workbench/OccPartMesh.h"
#include "workbench/MeasurementSnap.h"
#include "workbench/SelectionRectangle.h"
#include "workbench/OccModelPresentation.h"
#include "workbench/OccTransformPreview.h"
#include "workbench/OccMeasurementPresentation.h"
#include "workbench/OccReferencePlanePresentation.h"

class QLabel;
class QTimer;
class QPushButton;

namespace reverse::workbench
{
class ThicknessLabelEditor;

class ModelSelectionFilter final : public SelectMgr_Filter
{
  public:
    ModelPickMode mode{ModelPickMode::Body};
    bool IsOk(const Handle(SelectMgr_EntityOwner) & owner) const override
    {
        const auto *object = dynamic_cast<const OccMeshOwner *>(owner.get());
        return !object || object->kind == mode;
    }
};

struct OccViewport::State
{
    std::vector<Vec3> capAnchors;
    std::array<QPushButton *, 2> capButtons{};
    Handle(Aspect_DisplayConnection) display;
    Handle(OpenGl_GraphicDriver) driver;
    Handle(V3d_Viewer) viewer;
    Handle(V3d_View) view;
    Handle(AIS_InteractiveContext) context;
    Handle(AIS_PointCloud) cloud;
    Handle(AIS_ViewCube) viewCube;
    Handle(AIS_RubberBand) selectionBand;
    Handle(AIS_Point) focusMarker;
    Handle(AIS_TextLabel) focusLabel;
    std::string focusPartId;
    std::vector<Handle(AIS_InteractiveObject)> holeGuide;
    std::vector<Handle(AIS_InteractiveObject)> holePreview;
    std::vector<Handle(AIS_InteractiveObject)> plateEditReference;
    std::vector<std::pair<PlateHoleBase, PlateHole>> batchHolePreview;
    bool plateCropEnabled{};
    std::optional<PlateHoleBase> holePreviewBase;
    PlateHole holePreviewRequest;
    Vec3 holePreviewOrigin;
    std::shared_ptr<const CloudSelection> selection;
    CloudSelectionMode selectionMode{CloudSelectionMode::Replace};
    bool cloudEditingEnabled{};
    bool modelPickingEnabled{};
    bool thicknessEditingEnabled{};
    bool pressedThicknessLabel{};
    ThicknessLabelEditor *thicknessEditor{};
    ModelPickMode pickMode{ModelPickMode::Body};
    Handle(ModelSelectionFilter) pickFilter = new ModelSelectionFilter();
    ReferencePlanePickMode referencePickMode{ReferencePlanePickMode::None};
    std::vector<Vec3> referencePoints;
    bool pointModelingPicking{};
    std::optional<ReferencePlane> referencePlane;
    std::optional<ReferencePlane> sectionPlane;
    bool sectionPreviewVisible{};
    PlaneSection section;
    Handle(Graphic3d_ClipPlane) cloudClip;
    std::unique_ptr<OccReferencePlanePresentation> referencePresentation;
    bool measurementEnabled{};
    PointMeasurement measurement;
    std::optional<MeasurementPoint> measurementHover;
    MeasurementSnap measurementSnap;
    std::unique_ptr<OccMeasurementPresentation> measurementPresentation;
    std::vector<ModelSelection> selectedObjects;
    SelectionRectangle rectangle;
    bool selectionDragged{};
    bool rightDragged{};
    QPoint rightPress;
    QTimer *navigationTimer{};
    double cubePixelRatio{};
    std::unique_ptr<OccModelPresentation> model;
    std::unique_ptr<OccTransformPreview> transform;
    std::vector<CloudDisplayPoint> points;
    std::vector<ModelDisplayPart> parts;
    QPoint lastMouse;
    QPoint leftPress;
    bool cloudVisible = true;
    bool cloudHeightColors = true;
    bool modelVisible = true;
    bool modelWireframe{};
    bool thicknessVisible = true;
    bool initializing = false;
    bool failed = false;
    QLabel *failureLabel = nullptr;
};
} // namespace reverse::workbench
