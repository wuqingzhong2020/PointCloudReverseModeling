/*
 * 文件描述：在 OCCT 场景绘制测量端点、总距离线及 XYZ 分解线；按屏幕尺寸放置结果卡片。
 * 使用不可选择的 AIS 对象和像素字号，旋转、缩放及 DPI 改变后重新定位，避免挡住鼠标输入。
 */
#include <algorithm>
#include <array>
#include <vector>

#include <QFont>
#include <QFontMetrics>
#include <QPoint>
#include <QRect>
#include <QString>

#include <AIS_InteractiveContext.hxx>
#include <AIS_Line.hxx>
#include <AIS_Point.hxx>
#include <AIS_TextLabel.hxx>
#include <AIS_Triangulation.hxx>
#include <Geom_CartesianPoint.hxx>
#include <Graphic3d_ZLayerId.hxx>
#include <Poly_Triangulation.hxx>
#include <Prs3d_Drawer.hxx>
#include <Prs3d_PointAspect.hxx>
#include <Prs3d_ShadingAspect.hxx>
#include <V3d_View.hxx>
#include <gp_Vec.hxx>

#include "workbench/OccMeasurementPresentation.h"

namespace reverse::workbench
{
namespace
{
gp_Pnt point(const Vec3 &value)
{
    return {value.x, value.y, value.z};
}

gp_Pnt atPixel(const Handle(V3d_View) &view, const gp_Pnt &anchor, const QPoint &pixel)
{
    int x, y;
    view->Convert(anchor.X(), anchor.Y(), anchor.Z(), x, y);
    double ax, ay, az, bx, by, bz;
    view->Convert(x, y, ax, ay, az);
    view->Convert(pixel.x(), pixel.y(), bx, by, bz);
    return anchor.Translated(gp_Vec(bx - ax, by - ay, bz - az));
}
}

struct OccMeasurementPresentation::State
{
    Handle(AIS_InteractiveContext) context;
    Handle(V3d_View) view;
    std::array<Handle(AIS_Line), 4> lines;
    std::array<Handle(AIS_Point), 3> markers;
    std::array<Handle(AIS_TextLabel), 5> labels;
    Handle(Poly_Triangulation) cardMesh;
    Handle(AIS_Triangulation) card;
    std::vector<Handle(AIS_InteractiveObject)> objects;

    void show(const Handle(AIS_InteractiveObject) &object, bool visible)
    {
        if (visible)
        {
            context->Display(object, 0, -1, false);
            context->Redisplay(object, false);
        }
        else
        {
            context->Erase(object, false);
        }
    }
};

OccMeasurementPresentation::OccMeasurementPresentation(
    const Handle(AIS_InteractiveContext) &context, const Handle(V3d_View) &view)
    : m_state(std::make_unique<State>())
{
    m_state->context = context;
    m_state->view = view;
    const std::array colors{Quantity_Color(0.94, 0.96, 1.0, Quantity_TOC_sRGB),
        Quantity_Color(0.92, 0.23, 0.21, Quantity_TOC_sRGB),
        Quantity_Color(0.10, 0.72, 0.30, Quantity_TOC_sRGB),
        Quantity_Color(0.24, 0.43, 1.0, Quantity_TOC_sRGB)};
    for (std::size_t i = 0; i < m_state->lines.size(); ++i)
    {
        Handle(AIS_Line) line = new AIS_Line(new Geom_CartesianPoint(0, 0, 0),
                                            new Geom_CartesianPoint(1, 0, 0));
        line->SetColor(colors[i]);
        m_state->lines[i] = line;
        m_state->objects.push_back(line);
    }
    for (auto &marker : m_state->markers)
    {
        marker = new AIS_Point(new Geom_CartesianPoint(0, 0, 0));
        m_state->objects.push_back(marker);
    }
    m_state->cardMesh = new Poly_Triangulation(4, 2, false);
    m_state->cardMesh->SetNode(1, gp_Pnt(0, 0, 0));
    m_state->cardMesh->SetNode(2, gp_Pnt(1, 0, 0));
    m_state->cardMesh->SetNode(3, gp_Pnt(1, 1, 0));
    m_state->cardMesh->SetNode(4, gp_Pnt(0, 1, 0));
    m_state->cardMesh->SetTriangle(1, Poly_Triangle(1, 2, 3));
    m_state->cardMesh->SetTriangle(2, Poly_Triangle(1, 3, 4));
    m_state->card = new AIS_Triangulation(m_state->cardMesh);
    Handle(Prs3d_ShadingAspect) shading = new Prs3d_ShadingAspect();
    shading->SetColor(Quantity_Color(0.97, 0.98, 1.0, Quantity_TOC_sRGB));
    shading->Aspect()->SetShadingModel(Graphic3d_TypeOfShadingModel_Unlit);
    m_state->card->Attributes()->SetShadingAspect(shading);
    m_state->objects.push_back(m_state->card);
    for (std::size_t i = 0; i < m_state->labels.size(); ++i)
    {
        auto &label = m_state->labels[i];
        label = new AIS_TextLabel();
        label->SetFont("Microsoft YaHei");
        label->SetFontAspect(i == 0 ? Font_FontAspect_Bold : Font_FontAspect_Regular);
        label->SetZoomable(false);
        label->SetOwnAnchorPoint(true);
        label->SetHJustification(i == 4 ? Graphic3d_HTA_CENTER : Graphic3d_HTA_LEFT);
        label->SetVJustification(Graphic3d_VTA_CENTER);
        label->SetColor(i == 0 || i == 4 ? Quantity_Color(0.10, 0.14, 0.20, Quantity_TOC_sRGB) : colors[i]);
        if (i == 4)
        {
            label->SetDisplayType(Aspect_TODT_SUBTITLE);
            label->SetColorSubTitle(Quantity_Color(0.97, 0.98, 1.0, Quantity_TOC_sRGB));
        }
        m_state->objects.push_back(label);
    }
    for (const auto &object : m_state->objects)
    {
        object->SetInfiniteState(true);
        object->SetZLayer(Graphic3d_ZLayerId_Topmost);
    }
    for (const auto &label : m_state->labels)
    {
        label->SetZLayer(Graphic3d_ZLayerId_TopOSD);
    }
}

OccMeasurementPresentation::~OccMeasurementPresentation()
{
    for (const auto &object : m_state->objects)
    {
        m_state->context->Remove(object, false);
    }
}

void OccMeasurementPresentation::clear()
{
    for (const auto &object : m_state->objects)
    {
        m_state->context->Erase(object, false);
    }
}

void OccMeasurementPresentation::update(const PointMeasurement &measurement, std::optional<MeasurementPoint> hover,
                                       int pixelWidth, int pixelHeight, double pixelRatio)
{
    clear();
    const std::optional<Vec3> hoverPosition = hover ? std::optional<Vec3>(hover->position) : std::nullopt;
    const auto first = measurement.first() ? measurement.first() : hoverPosition;
    if (!first || pixelWidth < 1 || pixelHeight < 1)
    {
        return;
    }
    const auto result = measurement.result(hoverPosition);
    const std::array endpoints{first, measurement.second() ? measurement.second() : hoverPosition,
        measurement.second() ? hoverPosition : std::nullopt};
    const std::array captured{measurement.first() ? measurement.firstSnapped() : (hover && hover->snapped),
        measurement.second() ? measurement.secondSnapped() : (hover && hover->snapped), hover && hover->snapped};
    for (std::size_t i = 0; i < endpoints.size(); ++i)
    {
        if (!endpoints[i] || (i == 1 && !result))
        {
            continue;
        }
        auto &marker = m_state->markers[i];
        marker->SetComponent(new Geom_CartesianPoint(point(*endpoints[i])));
        marker->Attributes()->SetPointAspect(new Prs3d_PointAspect(Aspect_TOM_O_POINT,
            captured[i] ? Quantity_Color(1.0, 0.44, 0.04, Quantity_TOC_sRGB)
                        : Quantity_Color(0.95, 0.98, 1.0, Quantity_TOC_sRGB),
            (captured[i] ? 4.5 : 3.0) * pixelRatio));
        m_state->show(marker, true);
    }
    if (!result)
    {
        return;
    }
    const auto &path = result->componentPath;
    for (std::size_t i = 0; i < m_state->lines.size(); ++i)
    {
        const gp_Pnt a = point(i == 0 ? path.front() : path[i - 1]);
        const gp_Pnt b = point(i == 0 ? path.back() : path[i]);
        if (a.SquareDistance(b) < 1e-16)
        {
            continue;
        }
        m_state->lines[i]->SetPoints(new Geom_CartesianPoint(a), new Geom_CartesianPoint(b));
        m_state->lines[i]->SetWidth(2.0 * pixelRatio);
        m_state->show(m_state->lines[i], true);
    }
    const auto &components = result->componentsMm;
    const std::array texts{
        QStringLiteral("距离：%1 mm").arg(result->distanceMm, 0, 'f', 2),
        QStringLiteral("X 距离：%1 mm").arg(components.x, 0, 'f', 2),
        QStringLiteral("Y 距离：%1 mm").arg(components.y, 0, 'f', 2),
        QStringLiteral("Z 距离：%1 mm").arg(components.z, 0, 'f', 2),
        QStringLiteral("%1 mm").arg(result->distanceMm, 0, 'f', 2)};
    QFont font(QStringLiteral("Microsoft YaHei"));
    font.setPixelSize(qRound(14 * pixelRatio));
    font.setBold(true);
    const QFontMetrics metrics(font);
    const int margin = qRound(12 * pixelRatio);
    const int rowHeight = std::max(metrics.height() + qRound(5 * pixelRatio), qRound(24 * pixelRatio));
    int cardWidth = qRound(180 * pixelRatio);
    for (std::size_t i = 0; i < 4; ++i)
    {
        cardWidth = std::max(cardWidth, metrics.horizontalAdvance(texts[i]) + 2 * margin);
    }
    const int cardHeight = 4 * rowHeight + 2 * margin;
    const gp_Pnt anchor = point(path.back());
    int x, y;
    m_state->view->Convert(anchor.X(), anchor.Y(), anchor.Z(), x, y);
    if (x < 0 || x >= pixelWidth || y < 0 || y >= pixelHeight)
    {
        return; // 测量点移出屏幕后不把结果卡片钉在屏幕边缘。
    }
    int left = x + 2 * margin;
    if (left + cardWidth > pixelWidth - margin)
    {
        left = x - 2 * margin - cardWidth;
    }
    left = std::clamp(left, margin, std::max(margin, pixelWidth - cardWidth - margin));
    int top = std::clamp(y - cardHeight / 2, margin, std::max(margin, pixelHeight - cardHeight - margin));
    const QRect cube(pixelWidth - qRound(180 * pixelRatio), 0, qRound(180 * pixelRatio), qRound(180 * pixelRatio));
    if (QRect(left, top, cardWidth, cardHeight).intersects(cube))
    {
        top = std::min(cube.bottom() + margin, std::max(margin, pixelHeight - cardHeight - margin));
    }
    const QRect box(left, top, cardWidth, cardHeight);
    const std::array corners{box.topLeft(), box.bottomLeft(), box.bottomRight(), box.topRight()};
    for (std::size_t i = 0; i < corners.size(); ++i)
    {
        m_state->cardMesh->SetNode(static_cast<int>(i + 1), atPixel(m_state->view, anchor, corners[i]));
    }
    m_state->cardMesh->UpdateCachedMinMax();
    m_state->show(m_state->card, true);
    for (std::size_t i = 0; i < m_state->labels.size(); ++i)
    {
        const auto utf8 = texts[i].toUtf8();
        m_state->labels[i]->SetText(TCollection_ExtendedString(utf8.constData(), true));
        m_state->labels[i]->SetHeight(14 * pixelRatio);
        if (i < 4)
        {
            m_state->labels[i]->SetPosition(atPixel(m_state->view, anchor,
                QPoint(left + margin, top + margin + static_cast<int>(i) * rowHeight + rowHeight / 2)));
        }
        else
        {
            const gp_Pnt middle = point(path.front()).Translated(gp_Vec(point(path.front()), anchor) * 0.5);
            int mx, my;
            m_state->view->Convert(middle.X(), middle.Y(), middle.Z(), mx, my);
            m_state->labels[i]->SetPosition(atPixel(m_state->view, middle, QPoint(mx, my - rowHeight)));
        }
        m_state->show(m_state->labels[i], true);
    }
}
}
