/* 文件描述：将有序选点投影到参考平面方向，生成可验证的精确拉伸实体。 */
#include "geometry/PointModeling.h"
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <gp_Ax2.hxx>
#include <algorithm>
#include <cmath>
#include <limits>

namespace reverse
{
namespace
{
using V = Eigen::Vector3d;
V v(Vec3 p) { return {p.x, p.y, p.z}; }
Vec3 out(const V &p) { return {p.x(), p.y(), p.z()}; }
void require(bool valid, const char *message)
{
    if (!valid) throw GeometryError("invalid_point_model", message);
}
V unit(const V &p)
{
    require(p.allFinite() && p.norm() > 1e-7, "选点重合或方向无效，请重新选点。");
    return p.normalized();
}
void positive(double value)
{
    require(std::isfinite(value) && value > 1e-6 && value <= 1e7, "尺寸必须大于 0 且不超过 10000000 mm。");
}
}
PointProfile pointModelProfile(const PointModeling &r)
{
    for (const auto size : {r.width,r.length,r.diameter,r.height})
        require(std::isfinite(size) && size >= 0 && size <= 1e7, "尺寸必须为有限非负数且不超过 10000000 mm。");
    require(r.points.size() >= 2, "请先选择足够的点。");
    require(r.points.size() <= 5000000, "选点数量超过限制。");
    const V n = unit(v(r.normal)), origin = v(r.origin);
    require(origin.allFinite(), "参考面坐标无效。");
    V x = v(r.xAxis) - n * n.dot(v(r.xAxis));
    if (x.norm() < 1e-7) x = n.unitOrthogonal();
    x = unit(x);
    V y = n.cross(x);
    std::vector<V> p;
    double level = -std::numeric_limits<double>::infinity();
    for (const auto &point : r.points)
    {
        const V q = v(point);
        require(q.allFinite(), "选点包含无效坐标。");
        const double distance = n.dot(q-origin);
        if (r.toPlane)
        {
            if (!std::isfinite(level) || std::abs(distance)>std::abs(level)) level=distance;
        }
        else level=std::max(level,distance);
        p.push_back(q);
    }
    PointProfile result;
    if (r.shape == "box")
    {
        require(std::isfinite(r.offset), "剖视偏移无效。");
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        double minZ = 0, maxZ = 0;
        std::size_t count = 0;
        for (const auto &point : p)
        {
            const V q = point - origin;
            const double z = n.dot(q);
            if (r.section && (r.reversed ? z > r.offset : z < r.offset)) continue;
            minX = std::min(minX, x.dot(q)); maxX = std::max(maxX, x.dot(q));
            minY = std::min(minY, y.dot(q)); maxY = std::max(maxY, y.dot(q));
            minZ = std::min(minZ, z); maxZ = std::max(maxZ, z); ++count;
        }
        if (count < 3)
        {
            throw GeometryError("invalid_point_model", "框选了 " + std::to_string(p.size()) +
                " 个点云点，剖视过滤后剩余 " + std::to_string(count) +
                " 个，无法生成障碍物。请关闭点云剖视，或调整剖视偏移 / 反向；无需逐个点击三个点。");
        }
        positive(maxX - minX); positive(maxY - minY); positive(maxZ - minZ);
        for (const auto &xy : std::vector<Eigen::Vector2d>{{minX,minY},{maxX,minY},{maxX,maxY},{minX,maxY}})
            result.contour.push_back(out(origin + x * xy.x() + y * xy.y() + n * minZ));
        result.extrusion = out(n * (maxZ - minZ));
        result.length = maxX - minX; result.width = maxY - minY; result.height = maxZ - minZ;
        return result;
    }
    for (auto &q : p) q += n * (level - n.dot(q - origin));
    const double height = r.toPlane ? level : r.height;
    require(std::isfinite(height) && std::abs(height) > 1e-6 && std::abs(height) <= 1e7,
        "拉伸高度为零或无效；选点不能全部位于目标参考面上。");
    result.height = std::abs(height);
    result.extrusion = out(-n * height);
    std::vector<V> contour;
    if (r.shape == "rectangle")
    {
        V anchor = p[0];
        double length = 0, width = r.width;
        if (r.method == "diagonal")
        {
            const V d = p[1] - p[0];
            length = std::abs(d.dot(x)); width = std::abs(d.dot(y));
            if (d.dot(x) < 0) x = -x;
            if (d.dot(y) < 0) y = -y;
        }
        else
        {
            require(r.method == "axis" || r.method == "corner", "矩形选点方式无效。");
            x = unit(p[1] - p[0]); y = n.cross(x); length = (p[1] - p[0]).norm();
            if (r.method == "corner")
            {
                require(p.size() == 3, "三点边角需要选择矩形的三个角点。");
                struct Edge { double length; int a,b; };
                std::array<Edge,3> edges{{{(p[1]-p[0]).norm(),0,1},{(p[2]-p[1]).norm(),1,2},{(p[0]-p[2]).norm(),2,0}}};
                std::sort(edges.begin(),edges.end(),[](const auto &a,const auto &b) { return a.length<b.length; });
                const auto &longEdge=edges[1], &shortEdge=edges[0];
                const int corner=(longEdge.a==shortEdge.a || longEdge.a==shortEdge.b) ? longEdge.a : longEdge.b;
                const int end=longEdge.a==corner ? longEdge.b : longEdge.a;
                const int side=shortEdge.a==corner ? shortEdge.b : shortEdge.a;
                anchor=p[corner]; x=unit(p[end]-anchor); y=n.cross(x);
                require(std::abs((p[side]-anchor).dot(y))>1e-6, "三个角点共线，无法构造矩形。");
                length=longEdge.length; width=shortEdge.length;
                if ((p[side]-anchor).dot(y)<0) y=-y;
            }
        }
        if (r.length > 0) length = r.length;
        // 对角线/三点的宽度由选点确定，尺寸阶段用显式 width 覆盖。
        if (r.method != "axis" && r.width > 0) width = r.width;
        positive(length); positive(width);
        if (r.method == "axis") anchor -= y * width / 2;
        contour = {anchor, anchor+x*length, anchor+x*length+y*width, anchor+y*width};
        result.length = length; result.width = width;
    }
    else if (r.shape == "circle")
    {
        V center = p[0]; double radius = (p[1] - p[0]).norm();
        if (r.method == "three")
        {
            require(p.size() == 3, "三点圆弧需要 3 个不共线的点。");
            const V a = p[1]-p[0], b = p[2]-p[0], c = a.cross(b);
            require(c.squaredNorm() > 1e-12 * a.squaredNorm() * b.squaredNorm(), "三个圆周点共线，无法建圆。");
            center += (a.squaredNorm()*b.cross(c) + b.squaredNorm()*c.cross(a))/(2*c.squaredNorm());
            radius = (center-p[0]).norm();
        }
        else require(r.method == "center", "圆形选点方式无效。");
        if (r.diameter > 0) radius = r.diameter / 2;
        positive(radius); result.circle = true; result.center = out(center); result.diameter = radius*2;
    }
    else if (r.shape == "polygon")
    {
        require(p.size() >= 3, "多边形至少需要 3 个点。"); contour = p;
    }
    else if (r.shape == "polyline")
    {
        positive(r.width);
        std::vector<V> left, right, tangents;
        for (std::size_t i=1; i<p.size(); ++i) tangents.push_back(unit(p[i]-p[i-1]));
        for (std::size_t i=0; i<p.size(); ++i)
        {
            V offset = n.cross(tangents[i ? i-1 : 0]);
            if (i && i+1<p.size())
            {
                const V next = n.cross(tangents[i]);
                const double denom = 1 + offset.dot(next);
                require(denom > 1e-4, "多段矩形折返角过小，请重新选点。");
                offset = (offset+next)/denom;
            }
            left.push_back(p[i]+offset*r.width/2); right.push_back(p[i]-offset*r.width/2);
        }
        contour = left; contour.insert(contour.end(), right.rbegin(), right.rend()); result.width=r.width;
    }
    else throw GeometryError("invalid_point_model", "不支持的选点模型类型。");
    for (const auto &q : contour) result.contour.push_back(out(q));
    return result;
}
TopoDS_Shape detail::pointModelShape(const PointModeling &r)
{
    const auto profile = pointModelProfile(r);
    const auto e = profile.extrusion;
    TopoDS_Shape shape;
    if (profile.circle)
    {
        const auto c=profile.center;
        shape = BRepPrimAPI_MakeCylinder(gp_Ax2(gp_Pnt(c.x,c.y,c.z),gp_Dir(e.x,e.y,e.z)),
            profile.diameter/2,profile.height).Shape();
    }
    else
    {
        BRepBuilderAPI_MakePolygon polygon;
        for (const auto &q : profile.contour) polygon.Add(gp_Pnt(q.x,q.y,q.z));
        polygon.Close();
        require(polygon.IsDone(), "轮廓无法闭合，请检查重复点。");
        BRepBuilderAPI_MakeFace face(polygon.Wire(), true);
        require(face.IsDone() && BRepCheck_Analyzer(face.Face()).IsValid(), "轮廓自交或退化，请重新选点。");
        BRepPrimAPI_MakePrism prism(face.Face(),gp_Vec(e.x,e.y,e.z));
        require(prism.IsDone(), "轮廓拉伸失败。"); shape=prism.Shape();
    }
    require(!shape.IsNull() && BRepCheck_Analyzer(shape).IsValid(), "生成的实体无效，未修改模型。");
    return shape;
}
}
