/* 文件描述：处理鼠标/键盘拾取和矩形框选；保持屏幕坐标换算与业务选择回调的边界。 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTimer>
#include <QWheelEvent>

#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>

#include "geometry/ReferencePlane.h"
#include "workbench/ModelFocus.h"
#include "workbench/OccMeasurementPicking.h"
#include "workbench/OccPicking.h"
#include "workbench/OccSelectionBand.h"
#include "workbench/OccViewportState.h"
#include "workbench/ReferencePlanePicking.h"
#include "workbench/ThicknessLabelEditor.h"

namespace reverse::workbench
{
void OccViewport::setThicknessEditingEnabled(bool enabled)
{
    state_->thicknessEditingEnabled = enabled;
    if (!enabled)
    {
        state_->thicknessEditor->hide();
    }
}

void OccViewport::setModelPickingEnabled(bool enabled)
{
    if (state_->modelPickingEnabled == enabled)
        return;
    state_->modelPickingEnabled = enabled;
    try
    {
        updateMeasurementPicking();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::setModelPickMode(ModelPickMode mode)
{
    if (state_->pickMode == mode)
        return;
    state_->pickMode = mode;
    try
    {
        clearSelectionRectangle();
        setSelectedPart({});
        updateMeasurementPicking();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::detectObject(const QPoint &pixel)
{
    state_->context->MoveTo(pixel.x(), pixel.y(), state_->view, false);
    const auto owner = state_->context->DetectedOwner();
    if (!state_->model || state_->model->detectedPartId(owner).empty())
        return;
    const auto selector = state_->context->MainSelector();
    std::optional<gp_Pnt> position;
    for (int rank = 1; rank <= selector->NbPicked(); ++rank)
    {
        if (selector->Picked(rank) == owner)
        {
            position = selector->PickedPoint(rank);
            break;
        }
    }
    if (position && !isMeasurementPointVisible(*position, state_->context, state_->view, [this](const auto &candidate)
                                               { return state_->model->isSurfaceOwner(candidate); }))
    {
        state_->context->ClearDetected(false);
    }
}

void OccViewport::setSelectedPart(const std::string &id)
{
    setSelectedParts(id.empty() ? std::vector<std::string>{} : std::vector<std::string>{id});
}

std::vector<std::string> OccViewport::selectedParts() const
{
    std::vector<std::string> ids;
    if (state_->pickMode == ModelPickMode::Body)
        for (const auto &selection : state_->selectedObjects)
            ids.push_back(selection.partId);
    return ids;
}

const std::vector<ModelSelection> &OccViewport::selectedObjects() const
{
    return state_->selectedObjects;
}

void OccViewport::setSelectedParts(const std::vector<std::string> &ids)
{
    std::vector<ModelSelection> selections;
    for (const auto &id : ids)
        selections.push_back({id, ModelPickMode::Body, 0});
    setSelectedObjects(selections);
}

void OccViewport::setSelectedObjects(const std::vector<ModelSelection> &selections)
{
    std::vector<ModelSelection> valid;
    if (state_->modelVisible)
        for (const auto &selection : selections)
            if (selection.kind == state_->pickMode &&
                std::any_of(state_->parts.begin(), state_->parts.end(),
                            [&](const auto &part) { return isValidModelSelection(part, selection); }) &&
                std::find(valid.begin(), valid.end(), selection) == valid.end())
                valid.push_back(selection);
    state_->selectedObjects = std::move(valid);
    if (std::none_of(state_->selectedObjects.begin(), state_->selectedObjects.end(),
                     [this](const auto &s) { return s.partId == state_->focusPartId; }))
        state_->focusPartId.clear();
    try
    {
        if (state_->model)
            state_->model->selectObjects(state_->selectedObjects);
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}
void OccViewport::setCloudEditingEnabled(bool enabled)
{
    state_->cloudEditingEnabled = enabled;
    if (!enabled && !canSelectRectangle())
    {
        state_->rectangle.finish();
        state_->selectionDragged = false;
    }
    updateSelectionCursor(state_->lastMouse);
}

void OccViewport::setCloudSelectionMode(CloudSelectionMode mode)
{
    if (state_->selectionMode == mode)
        return;
    clearSelectionRectangle();
    state_->selectionMode = mode;
    update();
}

void OccViewport::setCloudSelection(std::shared_ptr<const CloudSelection> selection, bool preserveRectangle)
{
    if (!preserveRectangle)
    {
        clearSelectionRectangle();
        update();
    }
    if (state_->selection == selection)
        return;
    state_->selection = std::move(selection);
    try
    {
        rebuildCloud();
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    catch (const std::exception &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::clearSelectionRectangle()
{
    state_->rectangle.clear();
    state_->selectionDragged = false;
    updateSelectionCursor(state_->lastMouse);
    if (!state_->selectionBand.IsNull() && !state_->context.IsNull())
    {
        state_->context->Erase(state_->selectionBand, false);
    }
}

bool OccViewport::canSelectRectangle() const
{
    return !state_->transform && state_->referencePickMode == ReferencePlanePickMode::None &&
           !state_->measurementEnabled &&
           (state_->plateCropEnabled || (state_->cloudEditingEnabled && state_->cloudVisible));
}

void OccViewport::updateSelectionCursor(const QPoint &current)
{
    if (state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None)
        return;
    const auto handle =
        canSelectRectangle() ? state_->rectangle.hitTest(current, 6.0 * devicePixelRatioF()) : RectangleHandle::None;
    setCursor(state_->plateCropEnabled && handle == RectangleHandle::None ? Qt::CrossCursor
                                                                          : SelectionRectangle::cursor(handle));
}

void OccViewport::drawSelectionRectangle()
{
    if (state_->selectionBand.IsNull())
    {
        state_->selectionBand = new AIS_RubberBand(Quantity_Color(1.0, 0.72, 0.05, Quantity_TOC_sRGB), Aspect_TOL_SOLID,
                                                   2.0 * devicePixelRatioF());
        state_->selectionBand->SetInfiniteState(true);
    }
    setSelectionBandRectangle(*state_->selectionBand, state_->rectangle.rect(),
                              {qRound(width() * devicePixelRatioF()), qRound(height() * devicePixelRatioF())});
    state_->context->Display(state_->selectionBand, 0, -1, false);
    state_->context->Redisplay(state_->selectionBand, false);
    state_->view->Redraw();
}

CloudSelectionRect OccViewport::selectionRectangle() const
{
    const auto &camera = state_->view->Camera();
    const auto matrix = camera->ProjectionMatrix() * camera->OrientationMatrix();
    CloudSelectionRect result;
    for (std::size_t row = 0; row < 4; ++row)
    {
        for (std::size_t col = 0; col < 4; ++col)
        {
            result.viewProjection[row * 4 + col] = matrix.GetValue(row, col);
        }
    }
    result.zeroToOneDepth = camera->IsZeroToOneDepth();
    const double w = qRound(width() * devicePixelRatioF());
    const double h = qRound(height() * devicePixelRatioF());
    const auto &rectangle = state_->rectangle.rect();
    result.minX = std::clamp(2.0 * rectangle.left() / w - 1, -1.0, 1.0);
    result.maxX = std::clamp(2.0 * rectangle.right() / w - 1, -1.0, 1.0);
    result.minY = std::clamp(1 - 2.0 * rectangle.bottom() / h, -1.0, 1.0);
    result.maxY = std::clamp(1 - 2.0 * rectangle.top() / h, -1.0, 1.0);
    return result;
}

bool OccViewport::finishSelectionRectangle(const QPoint &current)
{
    if (!state_->rectangle.dragging())
        return false;
    const bool editing = state_->rectangle.editing();
    const double ratio = devicePixelRatioF();
    const double dragThreshold = editing ? 0 : QApplication::startDragDistance() * ratio;
    const bool dragged = state_->selectionDragged || (current - state_->leftPress).manhattanLength() > dragThreshold;
    if (dragged)
        state_->rectangle.update(current, {double(qRound(width() * ratio)), double(qRound(height() * ratio))},
                                 4 * ratio);
    state_->rectangle.finish();
    state_->selectionDragged = false;
    updateSelectionCursor(current);
    if (dragged && state_->rectangle.rect().isValid())
    {
        drawSelectionRectangle();
        const auto rectangle = selectionRectangle();
        if (state_->plateCropEnabled)
        {
            if (onPlateCrop)
                onPlateCrop(rectangle);
        }
        else if (onCloudSelection)
        {
            onCloudSelection(rectangle, state_->selectionMode, editing);
        }
        return true;
    }
    if (editing)
        return true; // 单击框内或边缘保留选区，不穿透拾取模型。
    clearSelectionRectangle();
    if (dragged || state_->plateCropEnabled)
        return true;
    if (!state_->modelPickingEnabled)
    {
        if (state_->selectionMode == CloudSelectionMode::Replace && onCloudSelection)
            onCloudSelection({}, CloudSelectionMode::Clear, false);
        return true;
    }
    return false;
}

void OccViewport::mousePressEvent(QMouseEvent *event)
{
    state_->pressedThicknessLabel = false;
    setFocus(Qt::MouseFocusReason);
    const auto ratio = devicePixelRatioF();
    state_->lastMouse = QPoint(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (state_->view.IsNull())
        return;
    try
    {
        stopNavigationAnimation();
        if (event->button() == Qt::LeftButton)
        {
            state_->leftPress = state_->lastMouse;
            state_->context->MoveTo(state_->lastMouse.x(), state_->lastMouse.y(), state_->view, false);
            const bool overCube = isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube);
            if (canSelectRectangle() && !overCube && state_->rectangle.beginEdit(state_->lastMouse, 6 * ratio))
            {
                state_->selectionDragged = false;
                state_->context->ClearDetected(false);
                updateSelectionCursor(state_->lastMouse);
                event->accept();
                update();
                return;
            }
            clearSelectionRectangle();
            if (!state_->plateCropEnabled && state_->thicknessEditingEnabled && state_->model &&
                state_->model->labelAt(event->position(), ratio))
            {
                state_->pressedThicknessLabel = true;
                event->accept();
                return; // 标签不能穿透到背后的模型或启动点云框选。
            }
            if (state_->transform)
            {
                state_->transform->startDrag(state_->lastMouse.x(), state_->lastMouse.y());
                event->accept();
                return;
            }
            if (canSelectRectangle() && !overCube)
                state_->rectangle.beginCreate(state_->lastMouse);
            if (state_->measurementEnabled)
            {
                state_->context->ClearDetected(false);
            }
        }
        if (event->button() == Qt::RightButton)
        {
            state_->rightPress = state_->lastMouse;
            state_->rightDragged = false;
            state_->view->StartRotation(state_->lastMouse.x(), state_->lastMouse.y());
        }
        update();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::mouseMoveEvent(QMouseEvent *event)
{
    if (state_->view.IsNull())
        return;
    const auto ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    try
    {
        if (event->buttons().testFlag(Qt::LeftButton) && state_->transform && state_->transform->dragging())
        {
            if (const auto request = state_->transform->drag(current.x(), current.y()); request && onTransformDragged)
                onTransformDragged(*request);
            redraw();
        }
        else if (event->buttons().testFlag(Qt::RightButton))
        {
            state_->rightDragged = state_->rightDragged || (current - state_->rightPress).manhattanLength() >
                                                               QApplication::startDragDistance() * ratio;
            if (state_->rightDragged)
            {
                clearSelectionRectangle();
                state_->view->Rotation(current.x(), current.y());
                redraw();
            }
        }
        else if (event->buttons().testFlag(Qt::MiddleButton))
        {
            clearSelectionRectangle();
            state_->view->Pan(current.x() - state_->lastMouse.x(), state_->lastMouse.y() - current.y());
            redraw();
        }
        else if (event->buttons().testFlag(Qt::LeftButton) && state_->rectangle.dragging())
        {
            const double dragThreshold = state_->rectangle.editing() ? 0 : QApplication::startDragDistance() * ratio;
            if (state_->selectionDragged || (current - state_->leftPress).manhattanLength() > dragThreshold)
            {
                state_->selectionDragged = true;
                state_->rectangle.update(current, {double(qRound(width() * ratio)), double(qRound(height() * ratio))},
                                         4 * ratio);
                drawSelectionRectangle();
            }
        }
        else if (event->buttons() == Qt::NoButton)
        {
            if (state_->measurementEnabled || (state_->referencePickMode == ReferencePlanePickMode::ThreePoints ||
                                               state_->referencePickMode == ReferencePlanePickMode::OrderedPoints))
            {
                state_->measurementHover = measurementPoint(current);
                redraw();
            }
            else
            {
                detectObject(current);
                updateSelectionCursor(current);
                if (isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube))
                    setCursor(Qt::ArrowCursor);
                else if (canSelectRectangle() && state_->rectangle.hitTest(current, 6 * ratio) != RectangleHandle::None)
                    state_->context->ClearDetected(false);
                state_->view->Redraw();
            }
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
    state_->lastMouse = current;
}

void OccViewport::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && state_->pressedThicknessLabel)
    {
        state_->pressedThicknessLabel = false;
        event->accept();
        return;
    }
    if (state_->view.IsNull())
    {
        return;
    }
    const double ratio = devicePixelRatioF();
    const QPoint current(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
    if (event->button() == Qt::RightButton)
    {
        if (!state_->rightDragged &&
            (state_->pickMode == ModelPickMode::Body || state_->transform || state_->cloudEditingEnabled ||
             state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None ||
             (hasEditContextMenu && hasEditContextMenu())) &&
            onCloudContextMenu)
        {
            if (state_->selectedObjects.empty() && state_->modelPickingEnabled &&
                state_->pickMode == ModelPickMode::Body && state_->model &&
                !(hasEditContextMenu && hasEditContextMenu()))
            {
                detectObject(current);
                const auto hit = state_->model->detectedSelection(state_->context->DetectedOwner());
                if (!hit.partId.empty())
                {
                    setSelectedObjects({hit});
                    if (onModelObjectsSelected)
                        onModelObjectsSelected(state_->selectedObjects);
                }
            }
            onCloudContextMenu(event->globalPosition().toPoint());
        }
        return;
    }
    if (event->button() != Qt::LeftButton)
    {
        return;
    }
    try
    {
        if (state_->transform)
        {
            state_->transform->finishDrag();
            redraw();
            event->accept();
            return;
        }
        if (finishSelectionRectangle(current))
        {
            event->accept();
            return;
        }
        if ((current - state_->leftPress).manhattanLength() > QApplication::startDragDistance() * ratio)
        {
            return;
        }
        detectObject(current);
        const auto owner = Handle(AIS_ViewCubeOwner)::DownCast(state_->context->DetectedOwner());
        if (!owner.IsNull() && isDetectedInteractive(owner, state_->viewCube))
        {
            clearSelectionRectangle();
            state_->viewCube->StartAnimation(owner);
            state_->navigationTimer->start();
            event->accept();
            return;
        }
        if (state_->referencePickMode != ReferencePlanePickMode::None)
        {
            pickReference(current);
            event->accept();
            return;
        }
        if (state_->measurementEnabled)
        {
            state_->context->ClearDetected(false);
            state_->measurementHover = measurementPoint(current);
            if (state_->measurementHover &&
                state_->measurement.pick(state_->measurementHover->position, state_->measurementHover->snapped))
            {
                if (const auto result = state_->measurement.result(); result && onMeasurementCompleted)
                {
                    onMeasurementCompleted(*result);
                }
            }
            redraw();
            event->accept();
            return;
        }
        if (state_->modelPickingEnabled && state_->model)
        {
            const auto selectedOwner = state_->context->DetectedOwner();
            const auto selected = state_->model->detectedSelection(selectedOwner);
            const auto modifiers = event->modifiers();
            const bool toggle = modifiers.testFlag(Qt::ShiftModifier) || modifiers.testFlag(Qt::ControlModifier);
            if (selected.partId.empty() && state_->cloudEditingEnabled && !toggle &&
                state_->selectionMode == CloudSelectionMode::Replace && onCloudSelection)
                onCloudSelection({}, CloudSelectionMode::Clear, false);
            setSelectedObjects(updateModelSelection(state_->selectedObjects, selected, state_->selectionMode, toggle));
            if (onModelObjectsSelected)
                onModelObjectsSelected(state_->selectedObjects);
            redraw();
            event->accept();
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
    {
        if (onEditEscape && onEditEscape())
        {
            event->accept();
            return;
        }
        if (state_->transform)
        {
            if (onTransformCancelled)
                onTransformCancelled();
            event->accept();
            return;
        }
        if (state_->referencePickMode != ReferencePlanePickMode::None)
        {
            if (onReferenceCancelled)
                onReferenceCancelled();
            event->accept();
            return;
        }
        if (state_->measurementEnabled)
        {
            setMeasurementEnabled(false);
            event->accept();
            return;
        }
        clearSelectionRectangle();
        setSelectedPart({});
        if (onModelObjectsSelected)
            onModelObjectsSelected({});
        if (state_->cloudEditingEnabled && onCloudSelection)
        {
            onCloudSelection({}, CloudSelectionMode::Clear, false);
        }
        update();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void OccViewport::leaveEvent(QEvent *event)
{
    state_->measurementHover.reset();
    QWidget::leaveEvent(event);
    if (!state_->context.IsNull())
    {
        state_->context->ClearDetected(false);
        update();
    }
}

void OccViewport::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (state_->plateCropEnabled)
    {
        mousePressEvent(event);
        return;
    }
    if (event->button() == Qt::LeftButton && canSelectRectangle())
    {
        const double ratio = devicePixelRatioF();
        const QPoint pixel(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio));
        if (!state_->context.IsNull() && !isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube) &&
            state_->rectangle.beginEdit(pixel, 6 * ratio))
        {
            state_->leftPress = pixel;
            state_->selectionDragged = false;
            event->accept();
            return;
        }
    }
    if (event->button() == Qt::LeftButton && state_->thicknessEditingEnabled && state_->model)
    {
        const double ratio = devicePixelRatioF();
        if (const auto label = state_->model->labelAt(event->position(), ratio))
        {
            clearSelectionRectangle();
            state_->pressedThicknessLabel = true;
            if (onThicknessLabelEditRequested && onThicknessLabelEditRequested(label->partId))
            {
                const QPoint center(qRound(label->pixelRect.center().x() / ratio),
                                    qRound(label->pixelRect.center().y() / ratio));
                state_->thicknessEditor->open(label->partId, label->thicknessMm, mapToGlobal(center),
                                              label->unresolved);
            }
            event->accept();
            return;
        }
    }
    if (state_->transform || state_->measurementEnabled || state_->referencePickMode != ReferencePlanePickMode::None)
    {
        event->accept();
        return;
    }
    if (!state_->context.IsNull())
    {
        const double ratio = devicePixelRatioF();
        state_->context->MoveTo(qRound(event->position().x() * ratio), qRound(event->position().y() * ratio),
                                state_->view, false);
        if (isDetectedInteractive(state_->context->DetectedOwner(), state_->viewCube))
        {
            event->accept();
            return;
        }
    }
    fitAll();
}

void OccViewport::wheelEvent(QWheelEvent *event)
{
    if (state_->view.IsNull())
        return;
    try
    {
        stopNavigationAnimation();
        clearSelectionRectangle();
        const auto steps = std::clamp(event->angleDelta().y() / 120.0, -5.0, 5.0);
        state_->view->SetZoom(std::pow(1.15, steps));
        redraw();
        event->accept();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

} // namespace reverse::workbench
