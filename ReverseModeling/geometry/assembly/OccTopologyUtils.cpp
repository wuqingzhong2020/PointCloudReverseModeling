/* 文件描述：直接移植原板件操作所用拓扑查询，保留原算法。 */
#include "geometry/assembly/OccTopologyUtils.h"
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepExtrema_DistShapeShape.hxx>
#include <BRepGProp.hxx>
#include <BRep_Builder.hxx>
#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeVertex.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <Extrema_ExtAlgo.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Geom_Surface.hxx>
#include <GeomAbs_SurfaceType.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_Orientation.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <algorithm>
#include <cmath>
#include <vector>
namespace
{
    gp_Pnt faceCenter(const TopoDS_Face &face)
    {
        BRepAdaptor_Surface surface(face);
        const double u = (surface.FirstUParameter() + surface.LastUParameter()) * 0.5;
        const double v = (surface.FirstVParameter() + surface.LastVParameter()) * 0.5;
        gp_Pnt center;
        gp_Vec firstDerivative;
        gp_Vec secondDerivative;
        surface.D1(u, v, center, firstDerivative, secondDerivative);
        return center;
    }
}
namespace reverse::detail::assembly
{
bool OccTopologyUtils::twoLargestFaces(const TopoDS_Shape &shape, TopoDS_Face &largest, TopoDS_Face &second)
{
    double largestArea = 0.0;
    double secondArea = 0.0;
    for (TopExp_Explorer explorer(shape, TopAbs_FACE); explorer.More(); explorer.Next()) {
        const TopoDS_Face face = TopoDS::Face(explorer.Current());
        const double area = OccTopologyUtils::faceArea(face);
        if (area > largestArea) {
            secondArea = largestArea;
            second = largest;
            largestArea = area;
            largest = face;
        } else if (area > secondArea) {
            secondArea = area;
            second = face;
        }
    }
    return !largest.IsNull() && !second.IsNull();
}

bool OccTopologyUtils::tryCalculateThickness(const TopoDS_Shape &shape, double &thickness)
{
    if (shape.IsNull()) return false;
    try {
        TopoDS_Face largest;
        TopoDS_Face second;
        if (!twoLargestFaces(shape, largest, second)) return false;
        const Handle(Geom_Surface) surface = BRep_Tool::Surface(second);
        if (surface.IsNull()) return false;

        GeomAPI_ProjectPointOnSurf projection(faceCenter(largest), surface, Extrema_ExtAlgo_Tree);
        if (projection.NbPoints() > 0) {
            thickness = projection.LowerDistance();
        } else {
            BRepExtrema_DistShapeShape distance(largest, second);
            distance.Perform();
            if (!distance.IsDone()) return false;
            thickness = distance.Value();
        }
        return std::isfinite(thickness) && thickness > 0.0;
    } catch (const Standard_Failure &) {
        return false;
    }
}

bool OccTopologyUtils::tryFaceNormal(const TopoDS_Face &face, gp_Vec &normal)
{
    if (face.IsNull()) return false;
    try {
        BRepAdaptor_Surface surface(face, true);
        const double u = (surface.FirstUParameter() + surface.LastUParameter()) * 0.5;
        const double v = (surface.FirstVParameter() + surface.LastVParameter()) * 0.5;
        gp_Pnt center;
        gp_Vec derivativeU;
        gp_Vec derivativeV;
        surface.D1(u, v, center, derivativeU, derivativeV);
        normal = derivativeU.Crossed(derivativeV);
        if (normal.Magnitude() <= 1e-12) return false;
        normal.Normalize();
        if (face.Orientation() == TopAbs_REVERSED) normal.Reverse();
        return true;
    } catch (const Standard_Failure &) {
        return false;
    }
}

double OccTopologyUtils::faceArea(const TopoDS_Face &face)
{
    if (face.IsNull()) return 0.0;
    GProp_GProps properties;
    BRepGProp::SurfaceProperties(face, properties);
    return properties.Mass();
}
}
