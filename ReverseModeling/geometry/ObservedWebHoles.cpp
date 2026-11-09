/*
 * 文件描述：
 * 把俯视点云投影到已识别腹板，以有环周支持的封闭空白区检测圆孔和直线加双半圆的长圆孔。
 * 高于腹板的点标记为遮挡，外边界连通空白、零散缺点及拟合残差过大的区域不生成孔。
 * 参数控制在建模阶段过滤，不破坏原始候选；使用解析圆弧刀具批量切孔，保留取消和几何失败语义。
 */
#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <numeric>

#include <Eigen/Dense>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakeWire.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <GC_MakeArcOfCircle.hxx>
#include <Geom_TrimmedCurve.hxx>
#include <Message_ProgressIndicator.hxx>
#include <NCollection_List.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <gp_Circ.hxx>

#include "geometry/Geometry.h"
#include "geometry/ObservedWebHoles.h"

namespace reverse::detail
{
namespace
{
using P = Eigen::Vector2d;
void check(std::stop_token stop)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "腹板孔识别/建模已取消。");
}
double dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
struct Fit
{
    P center = P::Zero();
    double angle{}, halfLength{}, radius{}, rms{}, maximum{};
};
double residual(const Fit &f, P p)
{
    p -= f.center;
    const double u = std::cos(f.angle)*p.x()+std::sin(f.angle)*p.y();
    const double v = -std::sin(f.angle)*p.x()+std::cos(f.angle)*p.y();
    return std::hypot(std::max(std::abs(u)-f.halfLength,0.0),v)-f.radius;
}
void errors(Fit &fit, const std::vector<P> &boundary)
{
    double sum=0; fit.maximum=0;
    for (auto p:boundary) { const double e=std::abs(residual(fit,p));sum+=e*e;fit.maximum=std::max(fit.maximum,e); }
    fit.rms=std::sqrt(sum/boundary.size());
}
std::optional<Fit> fitHole(const std::vector<P> &boundary, double cell, double area)
{
    P origin=P::Zero(); for(auto p:boundary) origin+=p; origin/=static_cast<double>(boundary.size());
    Eigen::Matrix3d matrix=Eigen::Matrix3d::Zero();
    Eigen::Vector3d right=Eigen::Vector3d::Zero();
    Eigen::Matrix2d covariance=Eigen::Matrix2d::Zero();
    for(auto p:boundary)
    {
        p-=origin; const Eigen::Vector3d row{2*p.x(),2*p.y(),1};
        matrix+=row*row.transpose(); right+=row*p.squaredNorm(); covariance+=p*p.transpose();
    }
    if(!matrix.fullPivLu().isInvertible()) return {};
    const Eigen::Vector3d value=matrix.ldlt().solve(right);
    Fit circle;circle.center=origin+value.head<2>();circle.radius=std::sqrt(value.z()+value.head<2>().squaredNorm());
    if(!std::isfinite(circle.radius)) return {};
    errors(circle,boundary);
    Fit best=circle;
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix2d> axes(covariance);
    if(axes.info()==Eigen::Success && axes.eigenvalues()[1]>axes.eigenvalues()[0]*1.4)
    {
        const P axis=axes.eigenvectors().col(1); const P perpendicular{-axis.y(),axis.x()};
        double u0=1e100,u1=-u0,v0=u0,v1=-u0;
        for(auto p:boundary)
        { p-=origin;const double u=p.dot(axis),v=p.dot(perpendicular);u0=std::min(u0,u);u1=std::max(u1,u);v0=std::min(v0,v);v1=std::max(v1,v); }
        Fit slot;slot.center=origin+axis*((u0+u1)/2)+perpendicular*((v0+v1)/2);
        slot.angle=std::atan2(axis.y(),axis.x());slot.radius=(v1-v0)/2;
        slot.halfLength=std::max(0.0,(u1-u0)/2-slot.radius);
        // 五参数有界最小二乘。参数与点同单位，角度用弧度，不依赖外部优化库。
        for(int iteration=0;iteration<30;++iteration)
        {
            Eigen::Matrix<double,5,5> h=Eigen::Matrix<double,5,5>::Zero();
            Eigen::Matrix<double,5,1> gradient=Eigen::Matrix<double,5,1>::Zero();
            for(auto p:boundary)
            {
                const double e=residual(slot,p);
                Eigen::Matrix<double,5,1> jacobian;
                for(int j=0;j<5;++j)
                {
                    Fit perturbed=slot; const double step=j==2?1e-5:1e-3;
                    if(j<2) perturbed.center[j]+=step;
                    else if(j==2) perturbed.angle+=step;
                    else if(j==3) perturbed.halfLength+=step;
                    else perturbed.radius+=step;
                    jacobian[j]=(residual(perturbed,p)-e)/step;
                }
                h+=jacobian*jacobian.transpose();gradient+=jacobian*e;
            }
            h.diagonal().array()+=1e-3;
            const Eigen::Matrix<double,5,1> delta=h.ldlt().solve(-gradient);
            if(!delta.allFinite()) break;
            errors(slot,boundary);Fit trial=slot;
            trial.center+=delta.head<2>();trial.angle+=delta[2];
            trial.halfLength=std::max(0.0,slot.halfLength+delta[3]);trial.radius=std::max(cell,slot.radius+delta[4]);
            errors(trial,boundary);
            if(trial.rms>=slot.rms) break;
            slot=trial;if(delta.norm()<1e-4) break;
        }
        errors(slot,boundary);
        if(slot.halfLength>cell && slot.rms<circle.rms*0.8) best=slot;
    }
    const double tolerance=std::max(cell*0.65,best.radius*0.04);
    const double expectedArea=std::numbers::pi*best.radius*best.radius+4*best.halfLength*best.radius;
    if(best.radius<cell*1.5 || best.rms>tolerance || best.rms>best.radius*0.2 ||
        best.maximum>tolerance*2.5 || area<expectedArea*0.75 || area>expectedArea*1.25) return {};
    return best;
}
class HoleProgress final : public Message_ProgressIndicator
{
public:
    explicit HoleProgress(std::stop_token stop):m_stop(stop){}
    bool UserBreak() override {return m_stop.stop_requested();}
    void Show(const Message_ProgressScope &, bool) override {}
private:
    std::stop_token m_stop;
};
}

void recognizeWebHoles(const CloudData &cloud, PlateRecognition &recognition, std::stop_token stop)
{
    auto found=std::find_if(recognition.plates.begin(),recognition.plates.end(),[](const auto &p){return p.id=="web";});
    if(found==recognition.plates.end() || (!found->thicknessMm && recognition.type != "other")) return;
    auto &web=*found;const double x0=web.minimumMm.x,z0=web.minimumMm.z,base=web.maximumMm.y;
    const double length=web.maximumMm.x-x0,width=web.maximumMm.z-z0;
    // 最多约二百万格，分辨率随扫描密度变化；不按固定毫米值直接把小孔填掉。
    const double spacing=std::sqrt(length*width/std::max<std::uint64_t>(1,web.supportPoints));
    const double cell=std::max({0.5,spacing*1.5,std::sqrt(length*width/2000000.0),length/8192,width/8192});
    const int nx=static_cast<int>(std::ceil(length/cell)),nz=static_cast<int>(std::ceil(width/cell));
    if(nx<3 || nz<3) return;
    std::vector<unsigned char> occupied(nx*nz),occluded(nx*nz),visited(nx*nz);
    const double band=std::max(3.0,recognition.surfaceNoiseMm*5);
    const auto &f=recognition.frame;
    for(std::size_t i=0;i<cloud.points.size();++i)
    {
        if(i%16384==0) check(stop);
        auto p=cloud.points[i].position;
        p={p.x+cloud.originMm.x-f.originMm.x,p.y+cloud.originMm.y-f.originMm.y,p.z+cloud.originMm.z-f.originMm.z};
        const double x=dot(p,f.xAxis),y=dot(p,f.yAxis),z=dot(p,f.zAxis);
        const int ix=static_cast<int>(std::floor((x-x0)/cell)),iz=static_cast<int>(std::floor((z-z0)/cell));
        if(ix<0 || ix>=nx || iz<0 || iz>=nz || y<base-band) continue;
        occupied[ix*nz+iz]=1;
        if(y>base+band) occluded[ix*nz+iz]=1;
    }
    const std::array<std::pair<int,int>,4> steps{{{1,0},{-1,0},{0,1},{0,-1}}};
    for(int index=0;index<nx*nz;++index)
    {
        if(index%4096==0) check(stop);
        if(occupied[index] || visited[index]) continue;
        std::vector<int> region{index};visited[index]=1;
        std::vector<P> boundary;
        bool touchesEdge=false,hasOcclusion=false;
        for(std::size_t j=0;j<region.size();++j)
        {
            if(j%4096==0) check(stop);
            const int x=region[j]/nz,z=region[j]%nz;
            if(x==0 || z==0 || x==nx-1 || z==nz-1) touchesEdge=true;
            for(auto [dx,dz]:steps)
            {
                const int xx=x+dx,zz=z+dz;
                if(xx<0 || xx>=nx || zz<0 || zz>=nz) continue;
                const int neighbor=xx*nz+zz;
                if(occupied[neighbor])
                {
                    boundary.emplace_back(x0+(x+0.5+dx*0.5)*cell,z0+(z+0.5+dz*0.5)*cell);
                    hasOcclusion|=occluded[neighbor]!=0;
                }
                else if(!visited[neighbor]) {visited[neighbor]=1;region.push_back(neighbor);}
            }
        }
        if(touchesEdge || hasOcclusion || region.size()<6 || boundary.size()<12 || region.size()>occupied.size()/5) continue;
        if(const auto fit=fitHole(boundary,cell,region.size()*cell*cell))
        {
            // 孔须完整位于观测腹板内；长圆孔半径指两端圆弧半径，即孔宽的一半。
            const double extentX=std::abs(std::cos(fit->angle))*fit->halfLength+fit->radius;
            const double extentZ=std::abs(std::sin(fit->angle))*fit->halfLength+fit->radius;
            if(fit->center.x()-extentX<=x0 || fit->center.x()+extentX>=web.maximumMm.x ||
                fit->center.y()-extentZ<=z0 || fit->center.y()+extentZ>=web.maximumMm.z) continue;
            web.holes.push_back({{fit->center.x(),base,fit->center.y()},
                {std::cos(fit->angle),0,std::sin(fit->angle)},fit->radius,2*fit->halfLength,fit->rms,boundary.size()});
        }
    }
    std::sort(web.holes.begin(),web.holes.end(),[](const auto &a,const auto &b){
        return a.centerMm.x<b.centerMm.x || (a.centerMm.x==b.centerMm.x && a.centerMm.z<b.centerMm.z);});
}

TopoDS_Shape cutObservedWebHoles(const TopoDS_Shape &shape, const RecognizedPlate &plate, std::stop_token stop)
{
    if(plate.holes.empty()) return shape;
    if(plate.normalAxis!=1) throw GeometryError("invalid_hole","腹板孔需要水平板或观测面。");
    NCollection_List<TopoDS_Shape> tools;
    for(const auto &hole:plate.holes)
    {
        check(stop);
        const double r=hole.radiusMm,a=hole.straightLengthMm/2;
        const double dx=hole.direction.x,dz=hole.direction.z;
        if(!std::isfinite(r) || !std::isfinite(a) || !std::isfinite(dx) || !std::isfinite(dz) ||
            !std::isfinite(hole.centerMm.x) || !std::isfinite(hole.centerMm.z) ||
            r<=0 || a<0 || std::abs(hole.direction.y)>1e-6 || std::abs(dx*dx+dz*dz-1)>1e-6)
            throw GeometryError("invalid_hole","圆孔/长圆孔尺寸或方向无效。");
        const double ex=std::abs(dx)*a+r,ez=std::abs(dz)*a+r;
        if(hole.centerMm.x-ex<=plate.minimumMm.x || hole.centerMm.x+ex>=plate.maximumMm.x ||
            hole.centerMm.z-ez<=plate.minimumMm.z || hole.centerMm.z+ez>=plate.maximumMm.z)
            throw GeometryError("invalid_hole","腹板端部收齐会截断已识别孔，请减小端部突出容差。");
        const gp_Pnt center(hole.centerMm.x,plate.minimumMm.y-1,hole.centerMm.z);
        BRepBuilderAPI_MakeWire wire;
        if(a<1e-8) wire.Add(BRepBuilderAPI_MakeEdge(gp_Circ(gp_Ax2(center,gp_Dir(0,1,0)),r)));
        else
        {
            const gp_Vec along(dx,0,dz),across(-dz,0,dx);
            const auto left=center.Translated(along*(-a)),right=center.Translated(along*a);
            const auto p=left.Translated(across*r),q=right.Translated(across*r);
            const auto u=right.Translated(across*(-r)),v=left.Translated(across*(-r));
            wire.Add(BRepBuilderAPI_MakeEdge(p,q));
            wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(q,right.Translated(along*r),u).Value()));
            wire.Add(BRepBuilderAPI_MakeEdge(u,v));
            wire.Add(BRepBuilderAPI_MakeEdge(GC_MakeArcOfCircle(v,left.Translated(along*(-r)),p).Value()));
        }
        BRepBuilderAPI_MakeFace face(wire.Wire(),true);
        if(!face.IsDone()) throw GeometryError("invalid_hole","孔轮廓不能构成有效平面。");
        tools.Append(BRepPrimAPI_MakePrism(face.Face(),gp_Vec(0,plate.thicknessMm.value_or(0)+2,0)).Shape());
    }
    NCollection_List<TopoDS_Shape> arguments;arguments.Append(shape);
    BRepAlgoAPI_Cut cut;cut.SetArguments(arguments);cut.SetTools(tools);cut.SetNonDestructive(true);
    occ::handle<HoleProgress> progress=new HoleProgress(stop);cut.Build(progress->Start());check(stop);
    if(!cut.IsDone() || cut.HasErrors() || cut.Shape().IsNull()) throw GeometryError("hole_failed","腹板识别孔切除失败，原模型未改变。");
    if (!plate.thicknessMm)
    {
        TopExp_Explorer faces(cut.Shape(), TopAbs_FACE);
        if (!faces.More()) throw GeometryError("hole_failed", "开孔后没有有效底板观测面。");
        const auto result = faces.Current();
        faces.Next();
        if (faces.More()) throw GeometryError("hole_failed", "识别孔将底板观测面分裂，未提交结果。");
        return result;
    }
    TopExp_Explorer solids(cut.Shape(),TopAbs_SOLID);
    if(!solids.More()) throw GeometryError("hole_failed","腹板开孔未保留有效实体。");
    const auto result=TopoDS::Solid(solids.Current());solids.Next();
    if(solids.More()) throw GeometryError("hole_failed","识别孔将腹板分裂成多块，未提交结果。");
    return result;
}
}
