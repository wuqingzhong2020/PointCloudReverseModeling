/*
 * 文件描述：
 * 管理 OCCT 分部件着色对象与随视角移动的板厚标注，封装显示对象的创建、显隐和释放。
 */
#pragma once

#include <memory>
#include <vector>

#include <Standard_Handle.hxx>

#include "workbench/ModelDisplayData.h"

class AIS_InteractiveContext;
class V3d_View;

namespace reverse::workbench
{
class OccModelPresentation
{
public:
    OccModelPresentation(const Handle(AIS_InteractiveContext) &context, const Handle(V3d_View) &view);
    ~OccModelPresentation();
    void rebuild(const std::vector<ModelDisplayPart> &parts);
    void setVisible(bool modelVisible, bool labelsVisible);
    void updateLabels(int pixelWidth, int pixelHeight, double pixelRatio);

private:
    struct State;
    std::unique_ptr<State> m_state;
};
}
