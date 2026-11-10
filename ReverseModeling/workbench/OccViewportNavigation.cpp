/* 文件描述：处理视图立方体、相机动画、部件定位和视图适合，不改变模型坐标。 */
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include <QTimer>

#include <AIS_AnimationCamera.hxx>
#include <Graphic3d_TransformPers.hxx>
#include <Prs3d_DatumAspect.hxx>
#include <Quantity_Color.hxx>
#include <Standard_Failure.hxx>
#include <gp_Dir.hxx>

#include "workbench/ModelFocus.h"
#include "workbench/OccViewportState.h"

namespace reverse::workbench
{
void OccViewport::createViewCube()
{
    // 参考 CamAssemblyWelding 的原生 AIS 导航，始终位于右上角且不计入工件包围盒。
    state_->viewCube = new AIS_ViewCube();
    auto &cube = state_->viewCube;
    cube->SetInfiniteState(true);
    cube->SetBoxColor(Quantity_Color(0.57, 0.61, 0.66, Quantity_TOC_sRGB));
    cube->SetTextColor(Quantity_Color(0.15, 0.18, 0.21, Quantity_TOC_sRGB));
    cube->SetFont("Microsoft YaHei");
    cube->SetBoxSideLabel(V3d_Xpos, QStringLiteral("右").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Xneg, QStringLiteral("左").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Ypos, QStringLiteral("前").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Yneg, QStringLiteral("后").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Zpos, QStringLiteral("上").toUtf8().constData());
    cube->SetBoxSideLabel(V3d_Zneg, QStringLiteral("下").toUtf8().constData());
    cube->Attributes()->SetDatumAspect(new Prs3d_DatumAspect());
    const auto &datum = cube->Attributes()->DatumAspect();
    const std::array axes{Prs3d_DatumParts_XAxis, Prs3d_DatumParts_YAxis, Prs3d_DatumParts_ZAxis};
    const std::array colors{Quantity_NOC_RED, Quantity_NOC_GREEN, Quantity_NOC_BLUE};
    for (std::size_t index = 0; index < axes.size(); ++index)
    {
        datum->ShadingAspect(axes[index])->SetColor(colors[index]);
        datum->TextAspect(axes[index])->SetColor(colors[index]);
    }
    cube->SetAutoStartAnimation(false);
    cube->SetFixedAnimationLoop(false);
    cube->SetResetCamera(true);
    cube->SetFitSelected(false);
    cube->SetDuration(0.25);
    cube->ViewAnimation()->SetView(state_->view);
    state_->cubePixelRatio = 0;
    updateViewCubeScale();
    state_->context->Display(cube, 0, 0, false);
}

void OccViewport::updateViewCubeScale()
{
    const double ratio = devicePixelRatioF();
    if (state_->viewCube.IsNull() || state_->cubePixelRatio == ratio)
    {
        return;
    }
    state_->cubePixelRatio = ratio;
    state_->viewCube->SetSize(60 * ratio);
    state_->viewCube->SetAxesPadding(4 * ratio);
    state_->viewCube->SetFontHeight(18 * ratio);
    state_->viewCube->SetTransformPersistence(
        new Graphic3d_TransformPers(Graphic3d_TMF_TriedronPers, Aspect_TOTP_RIGHT_UPPER,
                                    NCollection_Vec2<int>(qRound(90 * ratio), qRound(90 * ratio))));
    if (state_->context->IsDisplayed(state_->viewCube))
    {
        state_->context->Redisplay(state_->viewCube, false);
    }
}

void OccViewport::stopNavigationAnimation()
{
    state_->navigationTimer->stop();
    if (!state_->viewCube.IsNull())
    {
        state_->viewCube->ViewAnimation()->Stop();
    }
}

void OccViewport::advanceNavigationAnimation()
{
    if (state_->viewCube.IsNull() || state_->view.IsNull())
    {
        state_->navigationTimer->stop();
        return;
    }
    try
    {
        state_->viewCube->UpdateAnimation(false);
        redraw(); // 每一帧同步板厚遮挡与标签位置。
        if (!state_->viewCube->HasAnimation())
        {
            state_->navigationTimer->stop();
        }
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

void OccViewport::focusPart(const std::string &id)
{
    if (state_->view.IsNull())
        return;
    try
    {
        const auto camera = state_->view->Camera();
        const auto eye = camera->Direction().Reversed(), up = camera->Up();
        const auto focus = modelFocusView(state_->parts, id, {eye.X(), eye.Y(), eye.Z()}, {up.X(), up.Y(), up.Z()},
                                          double(std::max(1, width())) / std::max(1, height()));
        if (!focus)
            return;
        stopNavigationAnimation();
        clearSelectionRectangle();
        const double distance = std::max(camera->Distance(), focus->scale * 4);
        const auto c = focus->center, d = focus->towardEye, u = focus->up;
        camera->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        camera->SetEyeAndCenter(gp_Pnt(c.x + d.x * distance, c.y + d.y * distance, c.z + d.z * distance),
                                gp_Pnt(c.x, c.y, c.z));
        camera->SetUp(gp_Dir(u.x, u.y, u.z));
        camera->SetScale(focus->scale);
        state_->focusPartId = id;
        state_->view->ZFitAll();
        state_->context->ClearDetected(false);
        redraw();
    }
    catch (const Standard_Failure &error)
    {
        if (onError)
            onError(QString::fromUtf8(error.what()));
    }
}

void OccViewport::showBottomView(const Vec3 &normal, const Vec3 &longitudinal)
{
    clearSelectionRectangle();
    if (state_->view.IsNull())
    {
        return;
    }
    try
    {
        stopNavigationAnimation();
        const gp_Dir towardEye(normal.x, normal.y, normal.z);
        const gp_Dir along(longitudinal.x, longitudinal.y, longitudinal.z);
        // Up = 法线 × 长轴，使相机的屏幕右方向沿工件长轴，而不是按世界 XY 猜测。
        const gp_Dir up = towardEye.Crossed(along);
        state_->view->Camera()->SetProjectionType(Graphic3d_Camera::Projection_Orthographic);
        state_->view->SetProj(towardEye.X(), towardEye.Y(), towardEye.Z());
        state_->view->SetUp(up.X(), up.Y(), up.Z());
        state_->context->ClearDetected(false);
        redraw();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}
void OccViewport::fitAll()
{
    if (state_->view.IsNull())
        return;
    try
    {
        stopNavigationAnimation();
        clearSelectionRectangle();
        if (!state_->points.empty() || !state_->parts.empty())
            state_->view->FitAll(0.05, false);
        redraw();
    }
    catch (const Standard_Failure &error)
    {
        fail(QString::fromUtf8(error.what()));
    }
}

} // namespace reverse::workbench
