/* 文件描述：用半透明矩形、法线箭头和编号点显示基准面；预览与已提交状态共用表现。
 * 平面范围由场景包围盒投影确定，所有辅助对象均不参与拾取或 FitAll。
 */
#include <algorithm>
#include <limits>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Line.hxx>
#include <AIS_Point.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_Triangulation.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_MaterialAspect.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>

#include "workbench/OccReferencePlanePresentation.h"

namespace reverse::workbench
{
struct OccReferencePlanePresentation::State
{
    Handle(AIS_InteractiveContext) context;
    std::vector<Handle(AIS_InteractiveObject)> objects;
    void add(const Handle(AIS_InteractiveObject) &object)
    {
        object->SetInfiniteState(true);
        context->Display(object, 0, -1, false);
        objects.push_back(object);
    }
};
OccReferencePlanePresentation::OccReferencePlanePresentation(const Handle(AIS_InteractiveContext) &context)
    : m_state(std::make_unique<State>())
{
    m_state->context = context;
}
OccReferencePlanePresentation::~OccReferencePlanePresentation() { clear(); }
void OccReferencePlanePresentation::clear()
{
    for (const auto &object : m_state->objects) m_state->context->Remove(object, false);
    m_state->objects.clear();
}
void OccReferencePlanePresentation::update(const std::vector<Vec3> &points,
    const std::optional<ReferencePlane> &plane, const Vec3 &low, const Vec3 &high, double offset, double ratio)
{
    clear();
    const Quantity_Color color(0.08, 0.75, 0.97, Quantity_TOC_sRGB);
    if (plane)
    {
        const auto &n = plane->normal, &x = plane->xAxis;
        const Vec3 y{n.y * x.z - n.z * x.y, n.z * x.x - n.x * x.z, n.x * x.y - n.y * x.x};
        const Vec3 origin{plane->pointsMm[0].x + n.x * offset, plane->pointsMm[0].y + n.y * offset,
                          plane->pointsMm[0].z + n.z * offset};
        double umin = 0, umax = 0, vmin = 0, vmax = 0;
        for (int i = 0; i < 8; ++i)
        {
            const Vec3 d{(i & 1 ? high.x : low.x) - origin.x,
                         (i & 2 ? high.y : low.y) - origin.y, (i & 4 ? high.z : low.z) - origin.z};
            const double u = d.x * x.x + d.y * x.y + d.z * x.z;
            const double v = d.x * y.x + d.y * y.y + d.z * y.z;
            umin = std::min(umin, u); umax = std::max(umax, u);
            vmin = std::min(vmin, v); vmax = std::max(vmax, v);
        }
        const double margin = std::max(1.0, 0.04 * std::max(umax - umin, vmax - vmin));
        umin -= margin; umax += margin; vmin -= margin; vmax += margin;
        const auto at = [&](double u, double v) {
            return gp_Pnt(origin.x + x.x * u + y.x * v, origin.y + x.y * u + y.y * v,
                          origin.z + x.z * u + y.z * v);
        };
        const std::array corners{at(umin, vmin), at(umax, vmin), at(umax, vmax), at(umin, vmax)};
        Handle(Poly_Triangulation) mesh = new Poly_Triangulation(4, 2, false);
        for (int i = 0; i < 4; ++i) mesh->SetNode(i + 1, corners[i]);
        mesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
        mesh->SetTriangle(2, Poly_Triangle(1, 3, 4));
        Handle(AIS_Triangulation) surface = new AIS_Triangulation(mesh);
        Handle(Prs3d_ShadingAspect) shading = new Prs3d_ShadingAspect();
        shading->SetMaterial(Graphic3d_MaterialAspect(Graphic3d_NameOfMaterial_Plastified));
        shading->SetColor(color);
        shading->SetTransparency(0.88);
        surface->Attributes()->SetShadingAspect(shading);
        surface->SetTransparency(0.88);
        m_state->add(surface);
        for (int i = 0; i < 4; ++i)
        {
            Handle(AIS_Line) edge = new AIS_Line(new Geom_CartesianPoint(corners[i]),
                new Geom_CartesianPoint(corners[(i + 1) % 4]));
            edge->SetColor(color);
            m_state->add(edge);
        }
        const auto center = at((umin + umax) / 2, (vmin + vmax) / 2);
        const double length = margin * 2;
        const gp_Pnt tip(center.X() + n.x * length, center.Y() + n.y * length, center.Z() + n.z * length);
        Handle(AIS_Line) normal = new AIS_Line(new Geom_CartesianPoint(center), new Geom_CartesianPoint(tip));
        normal->SetColor(Quantity_NOC_ORANGE);
        m_state->add(normal);
        Handle(AIS_TextLabel) label = new AIS_TextLabel();
        label->SetText(TCollection_ExtendedString("N"));
        label->SetPosition(tip);
        label->SetColor(Quantity_NOC_ORANGE);
        label->SetHeight(14 * ratio);
        m_state->add(label);
    }
    for (std::size_t i = 0; i < points.size(); ++i)
    {
        const auto &p = points[i];
        Handle(AIS_Point) marker = new AIS_Point(new Geom_CartesianPoint(p.x, p.y, p.z));
        marker->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_POINT, Quantity_NOC_ORANGE, 5 * ratio));
        m_state->add(marker);
        Handle(AIS_TextLabel) label = new AIS_TextLabel();
        label->SetText(TCollection_ExtendedString(static_cast<int>(i + 1)));
        label->SetPosition(gp_Pnt(p.x, p.y, p.z));
        label->SetColor(Quantity_NOC_WHITE);
        label->SetHeight(16 * ratio);
        m_state->add(label);
    }
}
}
