/* 文件描述：校验实体、生成网格并发布不可变模型快照，统一维护导入板厚。 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GProp_GProps.hxx>
#include <NCollection_IndexedMap.hxx>
#include <Standard_Failure.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>

#include "geometry/ModelState.h"
#include "geometry/PlateThickness.h"

namespace reverse::detail::model
{

void checkpoint(std::stop_token stop, const ProgressCallback &progress, double fraction, std::string_view stage)
{
    if (stop.stop_requested())
        throw GeometryError("cancelled", "操作已取消");
    if (progress)
        progress(fraction, stage);
}
double checkedVolume(const TopoDS_Shape &shape)
{
    if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        throw GeometryError("invalid_solid", "OCCT 实体有效性检查未通过");
    GProp_GProps properties;
    // 样条轮廓及厚度仿射变换需要自适应积分，避免几何未变而摘要体积漂移。
    BRepGProp::VolumeProperties(shape, properties, 1e-9);
    const double volume = properties.Mass();
    if (!std::isfinite(volume) || volume <= 0)
        throw GeometryError("invalid_solid", "实体体积不是有效正数");
    return volume;
}

std::array<double, 6> bounds(const TopoDS_Shape &shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (box.IsVoid())
        throw GeometryError("invalid_solid", "形状包围盒为空");
    std::array<double, 6> values;
    box.Get(values[0], values[1], values[2], values[3], values[4], values[5]);
    return values;
}

void appendMesh(const TopoDS_Shape &shape, std::uint32_t partIndex, MeshData &mesh)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (int index = 1; index <= vertices.Extent(); ++index)
    {
        const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices(index)));
        mesh.keyPoints.push_back({{point.X(), point.Y(), point.Z()}, partIndex});
    }
    BRepMesh_IncrementalMesh triangulate(shape, 0.1, false, 0.5, false);
    if (!triangulate.IsDone())
        throw GeometryError("mesh_failed", "OCCT 显示三角化失败");
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> edges;
    TopExp::MapShapes(shape, TopAbs_EDGE, edges);
    for (int index = 1; index <= edges.Extent(); ++index)
    {
        const auto edge = TopoDS::Edge(edges(index));
        if (BRep_Tool::Degenerated(edge))
        {
            continue;
        }
        const BRepAdaptor_Curve curve(edge);
        GCPnts_QuasiUniformDeflection samples(curve, 0.1);
        if (!samples.IsDone() || samples.NbPoints() < 2)
        {
            continue;
        }
        ModelEdge displayEdge;
        displayEdge.topologyIndex = static_cast<std::uint32_t>(index);
        displayEdge.partIndex = partIndex;
        for (int pointIndex = 1; pointIndex <= samples.NbPoints(); ++pointIndex)
        {
            const auto point = samples.Value(pointIndex);
            displayEdge.pointsMm.push_back({point.X(), point.Y(), point.Z()});
        }
        mesh.edges.push_back(std::move(displayEdge));
    }
    std::uint32_t faceIndex = 0;
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next(), ++faceIndex)
    {
        const auto face = TopoDS::Face(faces.Current());
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane)
            mesh.planarFaces.push_back({partIndex, faceIndex});
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull())
            throw GeometryError("mesh_failed", "实体面缺少显示三角网格");
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int index = 1; index <= triangulation->NbNodes(); ++index)
        {
            const auto vertex = triangulation->Node(index).Transformed(location.Transformation());
            mesh.vertices.push_back({vertex.X(), vertex.Y(), vertex.Z()});
        }
        for (int index = 1; index <= triangulation->NbTriangles(); ++index)
        {
            int a, b, c;
            triangulation->Triangle(index).Get(a, b, c);
            if (face.Orientation() == TopAbs_REVERSED)
                std::swap(b, c);
            mesh.triangles.push_back({base + static_cast<std::uint32_t>(a - 1),
                                      base + static_cast<std::uint32_t>(b - 1),
                                      base + static_cast<std::uint32_t>(c - 1)});
            mesh.trianglePartIndices.push_back(partIndex);
            mesh.triangleFaceIndices.push_back(faceIndex);
        }
    }
}

// 在后台发布几何快照前统一更新导入板厚，避免镜像、拆分、开孔和刚体变换后出现旧标签。
// 不把自动识别结果写入手动厚度记录；不能确定等厚主面的部件仍可正常导入和显示。
std::shared_ptr<const Model> publishModel(std::shared_ptr<Model::Impl> result, std::stop_token stop)
{
    if (!result->summary.importedFormat.empty())
    {
        auto &thicknesses = result->summary.importedThicknesses;
        thicknesses.clear();
        for (const auto &part : result->plates)
        {
            checkpoint(stop, {}, 0, {});
            if (part.shape.ShapeType() != TopAbs_SOLID || result->summary.obstacleParts.contains(part.id))
                continue;
            try
            {
                thicknesses.emplace(part.id, detail::inspectPlateThickness(part.shape));
            }
            catch (const GeometryError &error)
            {
                if (error.code() == "cancelled")
                    throw;
            }
            catch (const Standard_Failure &)
            {
            } // 非等厚或不支持的 CAD 面型不阻塞整个文件。
        }
    }
    checkpoint(stop, {}, 0, {});
    return std::make_shared<const Model>(std::move(result));
}

} // namespace reverse::detail::model

namespace reverse
{
Model::Model(std::shared_ptr<const Impl> implementation) : implementation_(std::move(implementation))
{
    if (!implementation_)
        throw GeometryError("invalid_model", "模型实现不能为空");
}

const ModelSummary &Model::summary() const noexcept
{
    return implementation_->summary;
}
const MeshData &Model::mesh() const noexcept
{
    return implementation_->mesh;
}
} // namespace reverse
