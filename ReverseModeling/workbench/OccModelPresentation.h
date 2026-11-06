/*
 * 文件描述：
 * 管理 OCCT 分部件着色对象与随视角移动的板厚标注，封装显示对象的创建、显隐和释放。
 */
#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Standard_Handle.hxx>

#include "workbench/ModelDisplayData.h"
#include "workbench/ThicknessLabel.h"

class AIS_InteractiveContext;
class AIS_InteractiveObject;
class SelectMgr_EntityOwner;
class V3d_View;

namespace reverse::workbench
{
class OccModelPresentation
{
public:
    OccModelPresentation(const Handle(AIS_InteractiveContext) &context, const Handle(V3d_View) &view);
    ~OccModelPresentation();
    void setTransformPreview(bool active);
    std::vector<Handle(AIS_InteractiveObject)> objectsForParts(const std::vector<std::string> &ids) const;
    void rebuild(const std::vector<ModelDisplayPart> &parts);
    void setVisible(bool modelVisible, bool labelsVisible);
    void setSelectable(bool enabled, ModelPickMode mode = ModelPickMode::Body);
    void selectOwner(const Handle(SelectMgr_EntityOwner) &owner);
    [[nodiscard]] ModelSelection detectedSelection(const Handle(SelectMgr_EntityOwner) &owner) const;
    [[nodiscard]] bool isSurfaceOwner(const Handle(SelectMgr_EntityOwner) &owner) const;
    void selectPart(const std::string &id);
    void selectParts(const std::vector<std::string> &ids);
    void selectObjects(const std::vector<ModelSelection> &selections);
    [[nodiscard]] std::string detectedPartId(const Handle(SelectMgr_EntityOwner) &owner) const;
    [[nodiscard]] std::optional<ThicknessLabelTarget> labelAt(const QPointF &position, double pixelRatio) const;
    void updateLabels(int pixelWidth, int pixelHeight, double pixelRatio);

private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
