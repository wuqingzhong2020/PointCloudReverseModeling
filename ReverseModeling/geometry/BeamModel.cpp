/*
 * 文件描述：
 * 根据提交的 H 型钢及肋板参数创建独立 OCCT 实体、显示网格和 XCAF 装配。
 * 实现 STEP 临时写出、独立回读核验及不覆盖提交，并计算点到有限矩形板件表面的距离。
 */
#include "geometry/Geometry.h"
#include "geometry/PlateEditing.h"
#include "geometry/ModelImport.h"
#include "geometry/LocalReconstruction.h"
#include "geometry/PlateOutlineTreatment.h"
#include "geometry/PlateHoles.h"
#include "geometry/PlateThickness.h"
#include "geometry/PlateContour.h"
#include "geometry/PlateGeneration.h"
#include "geometry/MirrorCompletion.h"
#include "geometry/StagedFile.h"

#include <Message_ProgressIndicator.hxx>
#include <BRepFilletAPI_MakeFillet.hxx>
#include <BRepFilletAPI_MakeChamfer.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepAdaptor_Curve.hxx>
#include <BRepAdaptor_Surface.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepBuilderAPI_Copy.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <GProp_GProps.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopExp.hxx>
#include <NCollection_IndexedMap.hxx>
#include <TopTools_ShapeMapHasher.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include "geometry/ModelExchange.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <set>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace reverse {
namespace {

struct Plate {
    std::string id;
    Vec3 minimum;
    Vec3 maximum;
    TopoDS_Shape shape;
};

void checkpoint(std::stop_token stop, const ProgressCallback& progress,
                double fraction, std::string_view stage)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "操作已取消");
    if (progress) progress(fraction, stage);
}

Vec3 subtract(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
Vec3 negate(Vec3 value) { return {-value.x, -value.y, -value.z}; }
double dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool finite(Vec3 value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z); }

void validateFrame(const BeamFrame& frame)
{
    if (!finite(frame.originMm) || !finite(frame.xAxis) || !finite(frame.yAxis) || !finite(frame.zAxis))
        throw GeometryError("invalid_frame", "梁框架必须包含有限坐标");
    constexpr double tolerance = 1e-8;
    if (std::abs(dot(frame.xAxis, frame.xAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.yAxis, frame.yAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.zAxis, frame.zAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.xAxis, frame.yAxis)) > tolerance ||
        std::abs(dot(frame.xAxis, frame.zAxis)) > tolerance ||
        std::abs(dot(frame.yAxis, frame.zAxis)) > tolerance ||
        dot(cross(frame.xAxis, frame.yAxis), frame.zAxis) < 1.0 - tolerance)
        throw GeometryError("invalid_frame", "梁框架必须是正交的右手单位坐标系");
}

void validateParameters(const BeamParameters& parameters)
{
    for (const auto value : {parameters.length, parameters.width, parameters.height,
                            parameters.webThickness, parameters.flangeThickness}) {
        if (!std::isfinite(value) || value <= 1e-5 || value > 1e9)
            throw GeometryError("invalid_dimensions", "长度、宽度、高度及板厚必须是有效正数（mm）");
    }
    if (parameters.webThickness >= parameters.width ||
        2.0 * parameters.flangeThickness >= parameters.height)
        throw GeometryError("invalid_dimensions", "腹板厚度必须小于翼板宽度，两倍翼板厚度必须小于截面高度");
    if (parameters.ribStations.size() > 1000)
        throw GeometryError("invalid_ribs", "当前最多支持 1000 个肋板站位");
    if (!std::isfinite(parameters.ribThickness) || parameters.ribThickness < 0)
        throw GeometryError("invalid_ribs", "肋板厚度必须是有限的非负数");
    if (!parameters.ribStations.empty() &&
        (!std::isfinite(parameters.ribThickness) || parameters.ribThickness <= 1e-5 ||
         parameters.ribThickness >= parameters.length))
        throw GeometryError("invalid_ribs", "有肋板时必须指定有效肋板厚度");
    auto stations = parameters.ribStations;
    for (const double station : stations) {
        if (!std::isfinite(station) || station < parameters.ribThickness / 2.0 ||
            station > parameters.length - parameters.ribThickness / 2.0)
            throw GeometryError("invalid_ribs", "肋板站位必须使整块肋板位于梁长范围内");
    }
    std::sort(stations.begin(), stations.end());
    for (std::size_t index = 1; index < stations.size(); ++index) {
        if (stations[index] - stations[index - 1] < parameters.ribThickness + 1e-6)
            throw GeometryError("overlapping_ribs", "肋板站位重叠或重合，请修正站位/厚度");
    }
}

double checkedVolume(const TopoDS_Shape& shape)
{
    if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        throw GeometryError("invalid_solid", "OCCT 实体有效性检查未通过");
    GProp_GProps properties;
    BRepGProp::VolumeProperties(shape, properties);
    const double volume = properties.Mass();
    if (!std::isfinite(volume) || volume <= 0)
        throw GeometryError("invalid_solid", "实体体积不是有效正数");
    return volume;
}

std::array<double, 6> bounds(const TopoDS_Shape& shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    if (box.IsVoid()) throw GeometryError("invalid_solid", "形状包围盒为空");
    std::array<double, 6> values;
    box.Get(values[0], values[1], values[2], values[3], values[4], values[5]);
    return values;
}

void appendMesh(const TopoDS_Shape& shape, std::uint32_t partIndex, MeshData& mesh)
{
    NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> vertices;
    TopExp::MapShapes(shape, TopAbs_VERTEX, vertices);
    for (int index = 1; index <= vertices.Extent(); ++index)
    {
        const auto point = BRep_Tool::Pnt(TopoDS::Vertex(vertices(index)));
        mesh.keyPoints.push_back({{point.X(), point.Y(), point.Z()}, partIndex});
    }
    BRepMesh_IncrementalMesh triangulate(shape, 0.1, false, 0.5, false);
    if (!triangulate.IsDone()) throw GeometryError("mesh_failed", "OCCT 显示三角化失败");
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
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next(), ++faceIndex) {
        const auto face = TopoDS::Face(faces.Current());
        if (BRepAdaptor_Surface(face).GetType() == GeomAbs_Plane)
            mesh.planarFaces.push_back({partIndex, faceIndex});
        TopLoc_Location location;
        const auto triangulation = BRep_Tool::Triangulation(face, location);
        if (triangulation.IsNull()) throw GeometryError("mesh_failed", "实体面缺少显示三角网格");
        const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
        for (int index = 1; index <= triangulation->NbNodes(); ++index) {
            const auto vertex = triangulation->Node(index).Transformed(location.Transformation());
            mesh.vertices.push_back({vertex.X(), vertex.Y(), vertex.Z()});
        }
        for (int index = 1; index <= triangulation->NbTriangles(); ++index) {
            int a, b, c;
            triangulation->Triangle(index).Get(a, b, c);
            if (face.Orientation() == TopAbs_REVERSED) std::swap(b, c);
            mesh.triangles.push_back({base + static_cast<std::uint32_t>(a - 1),
                                      base + static_cast<std::uint32_t>(b - 1),
                                      base + static_cast<std::uint32_t>(c - 1)});
            mesh.trianglePartIndices.push_back(partIndex);
            mesh.triangleFaceIndices.push_back(faceIndex);
        }
    }
}

double boxSurfaceDistance(Vec3 point, const Plate& plate)
{
    const double dx = std::max({plate.minimum.x - point.x, 0.0, point.x - plate.maximum.x});
    const double dy = std::max({plate.minimum.y - point.y, 0.0, point.y - plate.maximum.y});
    const double dz = std::max({plate.minimum.z - point.z, 0.0, point.z - plate.maximum.z});
    if (dx > 0 || dy > 0 || dz > 0) return std::hypot(dx, dy, dz);
    return std::min({point.x - plate.minimum.x, plate.maximum.x - point.x,
                     point.y - plate.minimum.y, plate.maximum.y - point.y,
                     point.z - plate.minimum.z, plate.maximum.z - point.z});
}

TopoDS_Shape recognizedShape(const RecognizedPlate &plate)
{
    const auto &low = plate.minimumMm;
    const auto &high = plate.maximumMm;
    for (const double value : {low.x, low.y, low.z, high.x, high.y, high.z})
    {
        if (!std::isfinite(value))
        {
            throw GeometryError("invalid_recognition", "识别板件包含非有限坐标。");
        }
    }
    if (plate.normalAxis < 0 || plate.normalAxis > 2 || plate.id.empty())
    {
        throw GeometryError("invalid_recognition", "识别板件的方向或标识无效。");
    }
    if (plate.thicknessMm)
    {
        const std::array sizes{high.x - low.x, high.y - low.y, high.z - low.z};
        if (!std::isfinite(*plate.thicknessMm) || *plate.thicknessMm <= 0 ||
            std::abs(sizes[plate.normalAxis] - *plate.thicknessMm) > 1e-6 ||
            !(high.x > low.x && high.y > low.y && high.z > low.z))
        {
            throw GeometryError("invalid_recognition", "识别板件的厚度与尺寸不一致。");
        }
        return BRepPrimAPI_MakeBox(gp_Pnt(low.x, low.y, low.z), sizes[0], sizes[1], sizes[2]);
    }
    if (plate.normalAxis != 1 || high.y != low.y || high.x <= low.x || high.z <= low.z)
    {
        throw GeometryError("invalid_recognition", "未识别板厚的观测面尺寸无效。");
    }
    BRepBuilderAPI_MakePolygon boundary;
    boundary.Add(gp_Pnt(low.x, low.y, low.z));
    boundary.Add(gp_Pnt(high.x, low.y, low.z));
    boundary.Add(gp_Pnt(high.x, low.y, high.z));
    boundary.Add(gp_Pnt(low.x, low.y, high.z));
    boundary.Close();
    return BRepBuilderAPI_MakeFace(boundary.Wire());
}

std::vector<RecognizedPlate> modelPlates(const PlateRecognition &recognition, const PlateBuildOptions &options,
                                       std::vector<std::string> &warnings)
{
    auto plates = recognition.plates;
    const auto base = std::find_if(plates.begin(), plates.end(),
        [](const auto &plate) { return plate.id == "web"; });
    const auto first = std::find_if(plates.begin(), plates.end(),
        [](const auto &plate) { return plate.id == "flange-bottom"; });
    const auto second = std::find_if(plates.begin(), plates.end(),
        [](const auto &plate) { return plate.id == "flange-top"; });
    if (base != plates.end() && first != plates.end() && second != plates.end() &&
        options.endOverhangToleranceMm > 0)
    {
        const double commonStart = std::max(first->minimumMm.x, second->minimumMm.x);
        const double commonEnd = std::min(first->maximumMm.x, second->maximumMm.x);
        if (commonEnd <= commonStart)
        {
            throw GeometryError("invalid_recognition", "两侧翼板没有共同纵向范围，无法确定底板端部。");
        }
        const double startOverhang = commonStart - base->minimumMm.x;
        const double endOverhang = base->maximumMm.x - commonEnd;
        if (startOverhang > 0 && startOverhang <= options.endOverhangToleranceMm && commonStart < base->maximumMm.x)
        {
            base->minimumMm.x = commonStart;
        }
        if (endOverhang > 0 && endOverhang <= options.endOverhangToleranceMm && commonEnd > base->minimumMm.x)
        {
            base->maximumMm.x = commonEnd;
        }
        if (startOverhang > options.endOverhangToleranceMm || endOverhang > options.endOverhangToleranceMm)
        {
            warnings.emplace_back("底板端部突出超过当前容差，已保留观测长度；请核对点云或调整端部突出容差。");
        }
    }
    if (!options.mirrorSymmetry)
    {
        return plates;
    }
    if (base == plates.end())
    {
        throw GeometryError("no_mirror_plane", "未识别到底板，无法确定镜像基准面。");
    }
    const double middle = (base->minimumMm.y + base->maximumMm.y) / 2;
    const auto original = plates;
    for (std::size_t index = 0; index < original.size(); ++index)
    {
        const auto &plate = original[index];
        if (plate.id == "web")
        {
            continue;
        }
        auto mirrored = plate;
        mirrored.id += "-mirror";
        mirrored.minimumMm.y = 2 * middle - plate.maximumMm.y;
        mirrored.maximumMm.y = 2 * middle - plate.minimumMm.y;
        if (plate.normalAxis == 2)
        {
            // 两半翼板在底板中面接合，避免补齐底板厚度后镜像产生重叠实体。
            plates[index].minimumMm.y = middle;
            mirrored.maximumMm.y = middle;
        }
        mirrored.method = "explicit_mirror_of_" + plate.method;
        plates.push_back(std::move(mirrored));
    }
    return plates;
}

} // namespace

struct Model::Impl {
    std::map<std::string, std::shared_ptr<const detail::LocalPlateSource>> cloudSources;
    ModelSummary summary;
    MeshData mesh;
    std::vector<Plate> plates;
    TopoDS_Compound compound;
};

std::shared_ptr<const Model> detail::modelFromShape(const TopoDS_Shape& shape, const std::string& format,
    std::stop_token stop, const ProgressCallback& progress)
{
    if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        throw GeometryError("invalid_model", "导入模型未通过几何有效性检查");
    auto result = std::make_shared<Model::Impl>();
    result->summary.importedFormat = format;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    std::vector<TopoDS_Shape> parts;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next())
        parts.push_back(it.Current());
    for (TopExp_Explorer it(shape, TopAbs_FACE, TopAbs_SOLID); it.More(); it.Next())
        parts.push_back(it.Current());
    if (parts.empty()) throw GeometryError("empty_model", "模型不包含可显示的实体或曲面");
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        checkpoint(stop, progress, static_cast<double>(index) / parts.size(), "生成导入部件网格");
        const auto& part = parts[index];
        const auto box = bounds(part);
        if (!std::all_of(box.begin(), box.end(), [](double v) { return std::isfinite(v); }))
            throw GeometryError("invalid_model", "模型包含非有限坐标");
        const auto id = "imported-part-" + std::to_string(index + 1);
        result->plates.push_back({id, {box[0], box[1], box[2]}, {box[3], box[4], box[5]}, part});
        result->summary.partIds.push_back(id);
        result->mesh.partIds.push_back(id);
        if (part.ShapeType() == TopAbs_SOLID)
        {
            result->summary.volumeMm3 += checkedVolume(part);
            ++result->summary.solidCount;
        }
        else ++result->summary.surfaceCount;
        builder.Add(result->compound, part);
        appendMesh(part, static_cast<std::uint32_t>(index), result->mesh);
    }
    const auto box = bounds(result->compound);
    result->summary.frame.originMm = {(box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2};
    result->summary.warnings.push_back("导入几何按实体/独立面分组；不包含原始点云、重建参数或板厚识别结果。");
    checkpoint(stop, progress, 1, "模型导入完成");
    return std::make_shared<const Model>(std::move(result));
}

Model::Model(std::shared_ptr<const Impl> implementation)
    : implementation_(std::move(implementation))
{
    if (!implementation_) throw GeometryError("invalid_model", "模型实现不能为空");
}

const ModelSummary& Model::summary() const noexcept { return implementation_->summary; }
const MeshData& Model::mesh() const noexcept { return implementation_->mesh; }

namespace
{
class EdgeProgress final : public Message_ProgressIndicator
{
  public:
    explicit EdgeProgress(std::stop_token stop) : m_stop(stop)
    {
    }
    bool UserBreak() override
    {
        return m_stop.stop_requested();
    }
    void Show(const Message_ProgressScope &, bool) override
    {
    }

  private:
    std::stop_token m_stop;
};

TopoDS_Shape treatSolidEdges(const TopoDS_Shape &source, const EdgeTreatmentTarget &target,
                            EdgeTreatmentKind kind, double sizeMm, std::stop_token stop)
{
    if (source.ShapeType() != TopAbs_SOLID)
        throw GeometryError("not_solid", "圆角 / 倒角需要实体部件，观测面或开放曲面不支持此操作。");
    if (target.edges.empty())
        throw GeometryError("empty_edges", "参数 --edges 至少需要一条边。");
    // 在当前实体副本上处理，任何失败都不会触碰预览源和撤销快照。
    auto shape = BRepBuilderAPI_Copy(source, true, false).Shape();
    const auto selected = detail::plateOutlineCornerEdges(shape, target.edges);
    Handle(EdgeProgress) indicator = new EdgeProgress(stop);
    const auto build = [&](auto &operation) {
        for (const auto &edge : selected) operation.Add(sizeMm, edge);
        operation.Build(indicator->Start());
        checkpoint(stop, {}, 0, {});
        if (!operation.IsDone() || operation.Shape().IsNull())
        {
            std::ostringstream message;
            message << "板件轮廓" << (kind == EdgeTreatmentKind::Fillet ? "圆角 R=" : "倒角 D=")
                    << sizeMm << " mm 构建失败；所选板角的轮廓空间不足或相邻圆角相交，整批未应用。";
            throw GeometryError("edge_treatment_failed", message.str());
        }
        shape = operation.Shape();
        (void)checkedVolume(shape);
        NCollection_IndexedMap<TopoDS_Shape, TopTools_ShapeMapHasher> solids;
        TopExp::MapShapes(shape, TopAbs_SOLID, solids);
        if (solids.Extent() != 1 || TopExp_Explorer(shape, TopAbs_FACE, TopAbs_SOLID).More())
            throw GeometryError("invalid_solid", "未生成单个完整实体，整批未应用。");
        shape = solids(1);
    };
    if (kind == EdgeTreatmentKind::Fillet)
    {
        BRepFilletAPI_MakeFillet operation(shape);
        build(operation);
    }
    else
    {
        BRepFilletAPI_MakeChamfer operation(shape);
        build(operation);
    }
    return shape;
}
} // namespace

std::shared_ptr<const Model> treatModelEdges(const Model &model, const EdgeTreatment &request, std::stop_token stop,
                                           const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查圆角 / 倒角参数");
    if (!std::isfinite(request.sizeMm) || request.sizeMm <= 0)
        throw GeometryError("invalid_size", "参数 --size 必须为大于 0 的有限毫米值。");
    if (request.kind != EdgeTreatmentKind::Fillet && request.kind != EdgeTreatmentKind::Chamfer)
        throw GeometryError("invalid_kind", "仅支持圆角和等距倒角。");
    if (request.targets.empty())
        throw GeometryError("empty_selection", "请先选择实体边线。");
    const auto &source = *model.implementation_;
    auto result = std::make_shared<Model::Impl>(source);
    std::set<std::string> processed;
    std::string currentPart;
    try
    {
        for (const auto &target : request.targets)
        {
            currentPart = target.partId;
            checkpoint(stop, progress, 0.1 + 0.6 * processed.size() / request.targets.size(),
                       "计算圆角 / 倒角：" + currentPart);
            if (!processed.insert(currentPart).second)
                throw GeometryError("duplicate_part", "同一部件的边应合并在一个分组中。");
            const auto part = std::find_if(result->plates.begin(), result->plates.end(),
                [&](const auto &p) { return p.id == currentPart; });
            if (part == result->plates.end())
                throw GeometryError("unknown_part", "部件不存在。");
            part->shape = treatSolidEdges(part->shape, target, request.kind, request.sizeMm, stop);
            auto &treated = result->summary.edgeTreatedParts;
            if (std::find(treated.begin(), treated.end(), currentPart) == treated.end()) treated.push_back(currentPart);
        }
        result->mesh = {};
        result->mesh.partIds = result->summary.partIds;
        result->summary.volumeMm3 = 0;
        BRep_Builder builder;
        builder.MakeCompound(result->compound);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            auto &p = result->plates[i];
            currentPart = p.id;
            checkpoint(stop, progress, 0.75 + 0.24 * i / result->plates.size(), "更新圆角 / 倒角显示网格");
            // 三角化也会修改 TShape；未编辑部件同样复制，避免触碰历史快照。
            p.shape = BRepBuilderAPI_Copy(p.shape, true, false).Shape();
            builder.Add(result->compound, p.shape);
            if (p.shape.ShapeType() == TopAbs_SOLID) result->summary.volumeMm3 += checkedVolume(p.shape);
            appendMesh(p.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        checkpoint(stop, progress, 1, "圆角 / 倒角计算完成");
        return std::make_shared<const Model>(std::move(result));
    }
    catch (const GeometryError &error)
    {
        if (error.code() == "cancelled") throw;
        throw GeometryError(error.code(), "部件 " + currentPart + "：" + error.what());
    }
    catch (const Standard_Failure &error)
    {
        if (stop.stop_requested()) throw GeometryError("cancelled", "操作已取消。");
        throw GeometryError("edge_treatment_failed", "部件 " + currentPart +
                            " 板件轮廓圆角 / 倒角计算失败，请检查轮廓和数值，整批未应用：" + error.what());
    }
}

std::map<std::string, PlateThicknessInfo> inspectModelThickness(
    const Model &model, const std::vector<std::string> &partIds, std::stop_token stop)
{
    if (partIds.empty())
    {
        throw GeometryError("empty_selection", "参数 --parts 至少需要一个板件 ID。");
    }
    std::map<std::string, PlateThicknessInfo> result;
    for (const auto &id : partIds)
    {
        checkpoint(stop, {}, 0, {});
        const auto &plates = model.implementation_->plates;
        const auto part = std::find_if(plates.begin(), plates.end(), [&](const auto &p) { return p.id == id; });
        if (part == plates.end())
        {
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
        }
        try
        {
            result.emplace(id, detail::inspectPlateThickness(part->shape));
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + id + "：" + error.what());
        }
    }
    return result;
}

std::shared_ptr<const Model> changeModelThickness(const Model &model, const PlateThicknessChange &request,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件和厚度");
    if (!std::isfinite(request.thicknessMm) || request.thicknessMm < 0.001 || request.thicknessMm > 1e6)
    {
        throw GeometryError("invalid_thickness", "参数 --thickness 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.1 + 0.85 * i / result->plates.size(), "更新板厚：" + part.id);
        try
        {
            const auto info = selected.find(part.id);
            if (info != selected.end())
            {
                part.shape = detail::resizePlateThickness(part.shape, request.thicknessMm);
                info->second.thicknessMm = request.thicknessMm;
                result->summary.thicknessOverrides[part.id] = info->second;
            }
            else
            {
                // 网格生成也修改 TShape；复制未选部件，保证历史快照独立。
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            }
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("thickness_failed", "部件 " + part.id + " 修改板厚失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "板厚修改完成");
    return std::make_shared<const Model>(std::move(result));
}

std::shared_ptr<const Model> expandModelContours(const Model &model, const PlateContourExpansion &request,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件和外扩距离");
    if (!std::isfinite(request.distanceMm) || request.distanceMm < 0.001 || request.distanceMm > 1e6)
    {
        throw GeometryError("invalid_distance", "参数 --distance 必须为 0.001 到 1000000 之间的有限毫米值。");
    }
    auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.1 + 0.85 * i / result->plates.size(), "外扩轮廓：" + part.id);
        try
        {
            const auto info = selected.find(part.id);
            if (info != selected.end())
            {
                part.shape = detail::expandPlateContour(part.shape, request.distanceMm);
                if (std::find(result->summary.contourExpandedParts.begin(), result->summary.contourExpandedParts.end(),
                              part.id) == result->summary.contourExpandedParts.end())
                {
                    result->summary.contourExpandedParts.push_back(part.id);
                }
                // 已经手工修改的板厚标注需跟随新轮廓质心；不为导入板件凭空添加板厚标签。
                if (result->summary.thicknessOverrides.contains(part.id))
                {
                    result->summary.thicknessOverrides[part.id] = detail::inspectPlateThickness(part.shape);
                }
            }
            else
            {
                // 网格生成也修改 TShape；复制未选部件，保证历史快照独立。
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            }
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + part.id + "：" + error.what());
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("contour_failed", "部件 " + part.id + " 外扩轮廓失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "板件轮廓外扩完成");
    return std::make_shared<const Model>(std::move(result));
}

std::shared_ptr<const Model> completeModelMirror(const Model &model, const MirrorCompletion &request,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查镜像板件与底板中面");
    const auto &source = *model.implementation_;
    if (request.planePartId.empty()) throw GeometryError("no_mirror_plane", "请选择作为镜像面的底板（腹板）。");
    if (std::find(request.partIds.begin(), request.partIds.end(), request.planePartId) != request.partIds.end())
        throw GeometryError("invalid_parts", "底板（腹板）是镜像参考，不能同时作为待补全板件；请取消选中该板。");
    if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), request.planePartId) == source.summary.partIds.end())
        throw GeometryError("unknown_mirror_plane", "参数 --plane-part 的参考板件不存在：" + request.planePartId);
    PlateThicknessInfo plane;
    try
    {
        plane = inspectModelThickness(model, {request.planePartId}, stop).at(request.planePartId);
    }
    catch (const GeometryError &error)
    {
        throw GeometryError(error.code(), "镜像参考板 --plane-part 无法使用：" + std::string(error.what()));
    }
    const auto selected = inspectModelThickness(model, request.partIds, stop);
    auto result = std::make_shared<Model::Impl>(source);
    bool changed = false;
    for (const auto &[id, thickness] : selected)
    {
        checkpoint(stop, progress, 0.1, "镜像补全：" + id);
        const auto original = std::find_if(source.plates.begin(), source.plates.end(), [&](const auto &p) { return p.id == id; });
        try
        {
            const auto reflected = detail::mirrorPlate(original->shape, plane);
            const double reflectedVolume = checkedVolume(reflected);
            const double tolerance = std::max(1e-4, reflectedVolume * 1e-7);
            double covered = 0;
            // 同板原有部分及已存在的对称部件均参与去重，避免重复应用生成重叠板。
            for (const auto &part : result->plates)
            {
                if (part.shape.ShapeType() != TopAbs_SOLID) continue;
                const double overlap = detail::mirrorOverlapVolume(part.shape, reflected, stop);
                if (part.id != id && overlap > tolerance && overlap < reflectedVolume - tolerance)
                    throw GeometryError("mirror_overlap", "镜像区域与部件 " + part.id + " 部分重叠，请先调整板件或镜像参考。");
                covered += overlap;
            }
            if (covered >= reflectedVolume - tolerance) continue;
            auto united = detail::uniteMirrorPlate(original->shape, reflected, stop);
            std::string targetId = id;
            if (!united.IsNull())
            {
                auto target = std::find_if(result->plates.begin(), result->plates.end(), [&](const auto &p) { return p.id == id; });
                target->shape = united;
                auto &completed = result->summary.mirrorCompletedParts;
                if (std::find(completed.begin(), completed.end(), id) == completed.end()) completed.push_back(id);
            }
            else
            {
                do { targetId = "mirrored-part-" + std::to_string(result->summary.nextMirroredPartId++); }
                while (std::find(result->summary.partIds.begin(), result->summary.partIds.end(), targetId) != result->summary.partIds.end());
                result->plates.push_back({targetId, {}, {}, reflected});
                result->summary.partIds.push_back(targetId);
                result->summary.mirroredParts[targetId] = id;
                ++result->summary.solidCount;
                united = reflected;
            }
            // 有板厚标签的板件随补全更新锚点；导入模型仍不凭空添加标签。
            if (source.summary.importedFormat.empty() || source.summary.thicknessOverrides.contains(id))
                result->summary.thicknessOverrides[targetId] = detail::inspectPlateThickness(united);
            changed = true;
        }
        catch (const GeometryError &error)
        { throw GeometryError(error.code(), "部件 " + id + " 镜像补全失败：" + error.what()); }
        catch (const Standard_Failure &error)
        { throw GeometryError("mirror_failed", "部件 " + id + " 镜像补全失败，整批未应用：" + error.what()); }
    }
    if (!changed) throw GeometryError("already_symmetric", "选中板件的镜像区域已存在，无需补全；原模型未修改。");
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        auto &part = result->plates[i];
        checkpoint(stop, progress, 0.5 + 0.49 * i / result->plates.size(), "生成镜像显示网格");
        part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
        const auto box = bounds(part.shape);
        part.minimum = {box[0], box[1], box[2]}; part.maximum = {box[3], box[4], box[5]};
        builder.Add(result->compound, part.shape);
        if (part.shape.ShapeType() == TopAbs_SOLID) result->summary.volumeMm3 += checkedVolume(part.shape);
        appendMesh(part.shape, static_cast<std::uint32_t>(i), result->mesh);
    }
    checkpoint(stop, progress, 1, "镜像补全完成");
    return std::make_shared<const Model>(std::move(result));
}

std::shared_ptr<const Model> generateModelPlate(const Model &model, const PlateGeneration &request,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查附加板适用性");
    const auto &source = *model.implementation_;
    const bool pair = request.kind == PlateGenerationKind::Base && request.baseMode == BasePlateMode::VerticalPair;
    if (pair && (request.secondPartId.empty() || request.secondPartId == request.partId))
    {
        throw GeometryError("invalid_parts", "两块竖直板方式需要两个不同的部件；请用 Shift 加选第二块板，或填写 --second-part。");
    }
    if (!pair && !request.secondPartId.empty())
    {
        throw GeometryError("invalid_parts", "当前方式只允许一块板；请清空 --second-part 或选择 --mode vertical_pair。");
    }
    const auto selected = std::find_if(source.plates.begin(), source.plates.end(),
        [&](const auto &p) { return p.id == request.partId; });
    if (selected == source.plates.end()) throw GeometryError("unknown_part", "部件不存在：" + request.partId);
    TopoDS_Shape secondSource;
    if (pair)
    {
        const auto second = std::find_if(source.plates.begin(), source.plates.end(),
            [&](const auto &p) { return p.id == request.secondPartId; });
        if (second == source.plates.end())
        {
            throw GeometryError("unknown_part", "第二块部件不存在：" + request.secondPartId);
        }
        secondSource = second->shape;
    }
    try
    {
        const auto shape = detail::generateAttachedPlate(selected->shape, request, secondSource);
        auto result = std::make_shared<Model::Impl>(source);
        const auto prefix = request.kind == PlateGenerationKind::Cover ? "generated-cover-" : "generated-base-";
        std::string id;
        do { id = prefix + std::to_string(result->summary.nextGeneratedPlateId++); }
        while (std::find(result->summary.partIds.begin(), result->summary.partIds.end(), id) != result->summary.partIds.end());
        const auto box = bounds(shape);
        result->plates.push_back({id, {box[0], box[1], box[2]}, {box[3], box[4], box[5]}, shape});
        result->summary.partIds.push_back(id);
        result->summary.generatedPlates[id] = {request.kind, request.partId, request.secondPartId, request.baseMode};
        const auto thickness = detail::inspectPlateThickness(shape);
        if (std::abs(thickness.thicknessMm - request.thicknessMm) > 1e-5)
            throw GeometryError("unsuitable_plate_dimensions", "新增板厚大于轮廓窄边尺寸，不再是适合的板件；请减小板厚或增大外扩量。");
        result->summary.thicknessOverrides[id] = thickness;
        ++result->summary.solidCount;
        result->summary.volumeMm3 = 0;
        result->mesh = {};
        result->mesh.partIds = result->summary.partIds;
        BRep_Builder builder;
        builder.MakeCompound(result->compound);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            auto &p = result->plates[i];
            checkpoint(stop, progress, 0.5 + 0.49 * i / result->plates.size(), "生成附加板显示网格");
            p.shape = BRepBuilderAPI_Copy(p.shape, true, false).Shape();
            builder.Add(result->compound, p.shape);
            if (p.shape.ShapeType() == TopAbs_SOLID) result->summary.volumeMm3 += checkedVolume(p.shape);
            appendMesh(p.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        checkpoint(stop, progress, 1, "附加板生成完成：" + id);
        return std::make_shared<const Model>(std::move(result));
    }
    catch (const Standard_Failure &error)
    {
        throw GeometryError("plate_generation_failed", "生成板件失败，原模型未修改：" + std::string(error.what()));
    }
}

std::vector<PlateHoleBase> inspectModelHoleBases(const Model &model, const std::string &partId)
{
    for (const auto &plate : model.implementation_->plates)
        if (plate.id == partId) return detail::plateHoleBases(plate.shape);
    throw GeometryError("unknown_part", "部件不存在：" + partId);
}

std::shared_ptr<const Model> addModelHole(const Model &model, const PlateHole &request, std::stop_token stop,
                                        const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查添加孔参数");
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    auto part = std::find_if(result->plates.begin(), result->plates.end(),
        [&](const auto &p) { return p.id == request.partId; });
    if (part == result->plates.end()) throw GeometryError("unknown_part", "部件不存在：" + request.partId);
    std::string currentPart = request.partId;
    try
    {
        part->shape = detail::cutPlateHole(part->shape, request, stop);
        auto &edited = result->summary.holeEditedParts;
        if (std::find(edited.begin(), edited.end(), request.partId) == edited.end()) edited.push_back(request.partId);
        result->mesh = {};
        result->mesh.partIds = result->summary.partIds;
        result->summary.volumeMm3 = 0;
        BRep_Builder builder;
        builder.MakeCompound(result->compound);
        for (std::size_t i = 0; i < result->plates.size(); ++i)
        {
            auto &p = result->plates[i];
            currentPart = p.id;
            checkpoint(stop, progress, 0.75 + 0.24 * i / result->plates.size(), "更新开孔显示网格");
            // 三角化也会修改 TShape；未编辑部件同样复制，避免触碰历史快照。
            p.shape = BRepBuilderAPI_Copy(p.shape, true, false).Shape();
            builder.Add(result->compound, p.shape);
            if (p.shape.ShapeType() == TopAbs_SOLID) result->summary.volumeMm3 += checkedVolume(p.shape);
            appendMesh(p.shape, static_cast<std::uint32_t>(i), result->mesh);
        }
        checkpoint(stop, progress, 1, "开孔完成");
        return std::make_shared<const Model>(std::move(result));
    }
    catch (const Standard_Failure &error)
    {
        if (stop.stop_requested()) throw GeometryError("cancelled", "操作已取消。");
        throw GeometryError("hole_cut_failed", "部件 " + request.partId + " 开孔失败，模型未修改：" + error.what());
    }
}

std::shared_ptr<const Model> transformModelParts(const Model &model, const std::vector<std::string> &partIds,
                                                 const ModelTransform &request, std::stop_token stop,
                                                 const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查变换参数和部件");
    const auto delta = modelTransformFrame(request);
    const auto &source = *model.implementation_;
    const std::set<std::string> selected(partIds.begin(), partIds.end());
    if (selected.empty())
        throw GeometryError("empty_selection", "参数 --parts 至少需要一个部件 ID。");
    for (const auto &id : selected)
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
    if (isIdentityTransform(request))
        return std::make_shared<const Model>(model.implementation_);
    auto result = std::make_shared<Model::Impl>(source);
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    gp_Trsf transform;
    transform.SetValues(delta.xAxis.x, delta.yAxis.x, delta.zAxis.x, delta.originMm.x, delta.xAxis.y, delta.yAxis.y,
                        delta.zAxis.y, delta.originMm.y, delta.xAxis.z, delta.yAxis.z, delta.zAxis.z, delta.originMm.z);
    std::vector<bool> moved(source.plates.size());
    for (std::size_t i = 0; i < result->plates.size(); ++i)
    {
        checkpoint(stop, progress, 0.1 + 0.6 * i / result->plates.size(), "变换实体部件");
        auto &plate = result->plates[i];
        moved[i] = selected.contains(plate.id);
        if (moved[i])
        {
            // Copy=true 避免修改撤销历史共享的拓扑；刚体变换不改变体积和板厚。
            BRepBuilderAPI_Transform operation(plate.shape, transform, true);
            if (!operation.IsDone()) throw GeometryError("transform_failed", "部件几何变换失败。");
            plate.shape = operation.Shape();
            auto &accumulated = result->summary.partTransforms[plate.id];
            accumulated = composeTransforms(delta, accumulated);
            if (auto info = result->summary.thicknessOverrides.find(plate.id);
                info != result->summary.thicknessOverrides.end())
            {
                info->second.centerMm = transformPoint(delta, info->second.centerMm);
                info->second.normal = transformVector(delta, info->second.normal);
            }
        }
        builder.Add(result->compound, plate.shape);
    }
    auto &mesh = result->mesh;
    std::vector<bool> vertices(mesh.vertices.size());
    for (std::size_t i = 0; i < mesh.triangles.size(); ++i)
    {
        if (i % 4096 == 0) checkpoint(stop, progress, 0.7 + 0.2*i/mesh.triangles.size(), "同步显示和拾取几何");
        if (moved.at(mesh.trianglePartIndices.at(i)))
            for (const auto v : mesh.triangles[i]) vertices.at(v) = true;
    }
    for (std::size_t i = 0; i < vertices.size(); ++i)
        if (vertices[i]) mesh.vertices[i] = transformPoint(delta, mesh.vertices[i]);
    for (auto &point : mesh.keyPoints)
        if (moved.at(point.partIndex)) point.positionMm = transformPoint(delta, point.positionMm);
    for (auto &edge : mesh.edges)
        if (moved.at(edge.partIndex)) for (auto &point : edge.pointsMm) point = transformPoint(delta, point);
    checkpoint(stop, progress, 1, "部件变换完成");
    return std::make_shared<const Model>(std::move(result));
}

PlateEditResult editModelPlates(const Model &model, PlateEdit request,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查板件编辑范围");
    std::map<std::string, TopoDS_Shape> shapes;
    for (const auto &p : model.implementation_->plates) shapes.emplace(p.id, p.shape);
    auto edited = detail::editPlateShapes(shapes, request, stop);
    if (edited.replacements.empty())
        return {std::make_shared<const Model>(model.implementation_), std::move(request), std::move(edited.reports), false};
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    auto &summary = result->summary;
    result->plates.clear(); result->mesh = {};
    summary.partIds.clear(); summary.solidCount = summary.surfaceCount = 0; summary.volumeMm3 = 0;
    std::set<std::string> removed;
    for (const auto &old : model.implementation_->plates)
    {
        const auto found = edited.replacements.find(old.id);
        if (found == edited.replacements.end()) { result->plates.push_back(old); continue; }
        const auto &replacements = found->second;
        if (replacements.size() != 1) removed.insert(old.id);
        for (const auto &shape : replacements)
        {
            Plate part = old; part.shape = shape;
            if (replacements.size() > 1) part.id = old.id + "-split-" + std::to_string(summary.nextSplitId++);
            const auto box = bounds(shape);
            part.minimum = {box[0],box[1],box[2]}; part.maximum = {box[3],box[4],box[5]};
            result->plates.push_back(part);
            if (std::find(summary.contourExpandedParts.begin(),summary.contourExpandedParts.end(),part.id)==summary.contourExpandedParts.end())
                summary.contourExpandedParts.push_back(part.id);
            summary.thicknessOverrides[part.id] = request.operation=="repair" ?
                detail::inspectRepairPlateThickness(shape) : detail::inspectPlateThickness(shape);
            summary.editSources[part.id] = {old.id};
            if (request.operation=="merge" && part.id==request.target)
            {
                summary.editSources[part.id].insert(summary.editSources[part.id].end(),request.parts.begin(),request.parts.end());
                result->cloudSources.erase(part.id);
                std::erase(summary.reconstructableParts,part.id);
                summary.localSourceUnavailable[part.id]="合并校正后无法可靠沿用原点云索引，请重新全局重建。";
            }
            else if (replacements.size()>1 && result->cloudSources.contains(old.id))
            {
                // 分割子件不能共用父件支持点，否则局部重建会恢复整块父板。
                summary.localSourceUnavailable[part.id]="分割后原支持点尚未重新分配，不能局部重建此子件；其他部件不受影响。";
            }
        }
    }
    std::erase_if(result->cloudSources,[&](const auto &p){return removed.contains(p.first);});
    for (auto *ids : {&summary.reconstructableParts,&summary.locallyReconstructedParts,&summary.mirrorCompletedParts,
         &summary.contourExpandedParts,&summary.holeEditedParts,&summary.edgeTreatedParts})
        std::erase_if(*ids,[&](const auto &id){return removed.contains(id);});
    std::erase_if(summary.plates,[&](const auto &p){return removed.contains(p.id);});
    std::erase_if(summary.thicknessOverrides,[&](const auto &p){return removed.contains(p.first);});
    std::erase_if(summary.partTransforms,[&](const auto &p){return removed.contains(p.first);});
    std::erase_if(summary.generatedPlates,[&](const auto &p){return removed.contains(p.first);});
    std::erase_if(summary.mirroredParts,[&](const auto &p){return removed.contains(p.first);});
    BRep_Builder builder; builder.MakeCompound(result->compound);
    for (std::size_t i=0;i<result->plates.size();++i)
    {
        auto &part=result->plates[i];
        checkpoint(stop,progress,0.7+0.29*i/result->plates.size(),"更新板件显示");
        part.shape=BRepBuilderAPI_Copy(part.shape,true,false).Shape();
        builder.Add(result->compound,part.shape); summary.partIds.push_back(part.id);
        if (part.shape.ShapeType()==TopAbs_SOLID) { ++summary.solidCount;summary.volumeMm3+=checkedVolume(part.shape); }
        else ++summary.surfaceCount;
        appendMesh(part.shape,static_cast<std::uint32_t>(i),result->mesh);
    }
    result->mesh.partIds=summary.partIds;
    checkpoint(stop,progress,1,"板件编辑完成");
    return {std::make_shared<const Model>(std::move(result)),std::move(request),std::move(edited.reports),true};
}

std::shared_ptr<const Model> deleteModelParts(const Model &model, const std::vector<std::string> &partIds,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查待删除部件");
    const auto &source = *model.implementation_;
    const std::set<std::string> removed(partIds.begin(), partIds.end());
    if (removed.empty()) throw GeometryError("empty_selection", "参数 --parts 至少需要一个部件 ID。");
    for (const auto &id : removed)
    {
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "参数 --parts 中的部件不存在：" + id);
    }
    if (removed.size() == source.plates.size()) return {};
    auto result = std::make_shared<Model::Impl>();
    auto &summary = result->summary;
    summary = source.summary;
    std::erase_if(summary.mirroredParts, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.mirrorCompletedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.generatedPlates, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.reconstructableParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.locallyReconstructedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.contourExpandedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.thicknessOverrides, [&](const auto &p) { return removed.contains(p.first); });
    std::erase_if(summary.holeEditedParts, [&](const auto &id) { return removed.contains(id); });
    std::erase_if(summary.edgeTreatedParts, [&](const auto &id) { return removed.contains(id); });
    summary.partIds.clear();
    summary.solidCount = summary.surfaceCount = 0;
    summary.volumeMm3 = 0;
    std::erase_if(summary.plates, [&](const auto &p) { return removed.contains(p.id); });
    std::erase_if(summary.partTransforms, [&](const auto &p) { return removed.contains(p.first); });
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    const auto absent = std::numeric_limits<std::uint32_t>::max();
    std::vector<std::uint32_t> partMap(source.plates.size(), absent);
    for (std::size_t i = 0; i < source.plates.size(); ++i)
    {
        checkpoint(stop, progress, 0.1 + 0.3 * i / source.plates.size(), "移除模型部件");
        const auto &plate = source.plates[i];
        if (removed.contains(plate.id)) continue;
        partMap[i] = static_cast<std::uint32_t>(result->plates.size());
        result->plates.push_back(plate);
        summary.partIds.push_back(plate.id);
        builder.Add(result->compound, plate.shape);
        if (plate.shape.ShapeType() == TopAbs_SOLID)
        {
            ++summary.solidCount;
            summary.volumeMm3 += checkedVolume(plate.shape);
        }
        else ++summary.surfaceCount;
    }
    // 同步裁剪中立显示网格，既不重新三角化共享 BRep，也不重新编号剩余部件 ID。
    auto &mesh = result->mesh;
    const auto &old = source.mesh;
    mesh.partIds = summary.partIds;
    std::vector<std::uint32_t> vertexMap(old.vertices.size(), absent);
    for (std::size_t i = 0; i < old.triangles.size(); ++i)
    {
        if (i % 4096 == 0) checkpoint(stop, progress, 0.4 + 0.5 * i / old.triangles.size(), "更新剩余部件网格");
        const auto part = partMap.at(old.trianglePartIndices.at(i));
        if (part == absent) continue;
        auto triangle = old.triangles[i];
        for (auto &v : triangle)
        {
            auto &mapped = vertexMap.at(v);
            if (mapped == absent)
            {
                mapped = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(old.vertices[v]);
            }
            v = mapped;
        }
        mesh.triangles.push_back(triangle);
        mesh.trianglePartIndices.push_back(part);
        mesh.triangleFaceIndices.push_back(old.triangleFaceIndices.at(i));
    }
    for (auto p : old.keyPoints)
    {
        p.partIndex = partMap.at(p.partIndex);
        if (p.partIndex != absent) mesh.keyPoints.push_back(p);
    }
    for (auto edge : old.edges)
    {
        edge.partIndex = partMap.at(edge.partIndex);
        if (edge.partIndex != absent) mesh.edges.push_back(std::move(edge));
    }
    for (auto face : old.planarFaces)
    {
        face[0] = partMap.at(face[0]);
        if (face[0] != absent) mesh.planarFaces.push_back(face);
    }
    checkpoint(stop, progress, 1, "部件删除完成");
    return std::make_shared<const Model>(std::move(result));
}

std::shared_ptr<const Model> buildBeam(const BeamFrame& inputFrame, const BeamParameters& parameters,
    std::stop_token stop, const ProgressCallback& progress)
{
    checkpoint(stop, progress, 0.0, "检查建模参数");
    validateFrame(inputFrame);
    validateParameters(parameters);
    auto implementation = std::make_shared<Model::Impl>();
    auto& frame = implementation->summary.frame;
    frame = inputFrame;
    if (parameters.swapCrossSection) {
        frame.yAxis = inputFrame.zAxis;
        frame.zAxis = negate(inputFrame.yAxis);
    }
    implementation->summary.parameters = parameters;
    const auto& p = parameters;
    const double x0 = -p.length / 2.0, x1 = p.length / 2.0;
    const double y0 = -p.width / 2.0, y1 = p.width / 2.0;
    const double z0 = -p.height / 2.0, z1 = p.height / 2.0;
    auto& plates = implementation->plates;
    const double sideMinimum = p.mirrorSymmetry ? y0 : -p.webThickness / 2.0;
    plates.push_back({"flange-bottom", {x0, sideMinimum, z0}, {x1, y1, z0 + p.flangeThickness}, {}});
    plates.push_back({"flange-top", {x0, sideMinimum, z1 - p.flangeThickness}, {x1, y1, z1}, {}});
    plates.push_back({"web", {x0, -p.webThickness / 2.0, z0 + p.flangeThickness},
                      {x1, p.webThickness / 2.0, z1 - p.flangeThickness}, {}});
    for (std::size_t index = 0; index < p.ribStations.size(); ++index) {
        const double center = x0 + p.ribStations[index];
        const std::string prefix = "rib-" + std::to_string(index + 1);
        if (p.mirrorSymmetry) {
            plates.push_back({prefix + "-left", {center - p.ribThickness / 2.0, y0, z0 + p.flangeThickness},
                              {center + p.ribThickness / 2.0, -p.webThickness / 2.0, z1 - p.flangeThickness}, {}});
        }
        plates.push_back({prefix + "-right", {center - p.ribThickness / 2.0, p.webThickness / 2.0, z0 + p.flangeThickness},
                          {center + p.ribThickness / 2.0, y1, z1 - p.flangeThickness}, {}});
    }
    try {
        gp_Trsf transform;
        transform.SetValues(frame.xAxis.x, frame.yAxis.x, frame.zAxis.x, frame.originMm.x,
                            frame.xAxis.y, frame.yAxis.y, frame.zAxis.y, frame.originMm.y,
                            frame.xAxis.z, frame.yAxis.z, frame.zAxis.z, frame.originMm.z);
        BRep_Builder builder;
        builder.MakeCompound(implementation->compound);
        for (std::size_t index = 0; index < plates.size(); ++index) {
            checkpoint(stop, progress, 0.05 + 0.9 * index / plates.size(), "生成并检查板件实体");
            auto& plate = plates[index];
            BRepPrimAPI_MakeBox box(gp_Pnt(plate.minimum.x, plate.minimum.y, plate.minimum.z),
                                   plate.maximum.x - plate.minimum.x, plate.maximum.y - plate.minimum.y,
                                   plate.maximum.z - plate.minimum.z);
            plate.shape = BRepBuilderAPI_Transform(box.Shape(), transform, true).Shape();
            implementation->summary.volumeMm3 += checkedVolume(plate.shape);
            builder.Add(implementation->compound, plate.shape);
            appendMesh(plate.shape, static_cast<std::uint32_t>(index), implementation->mesh);
            implementation->summary.partIds.push_back(plate.id);
            implementation->mesh.partIds.push_back(plate.id);
        }
    } catch (const Standard_Failure& failure) {
        throw GeometryError("cad_failed", failure.what());
    }
    implementation->summary.solidCount = plates.size();
    checkpoint(stop, progress, 1.0, "辅助参数模型生成完成");
    return std::make_shared<Model>(std::move(implementation));
}

std::shared_ptr<const Model> buildRecognizedPlates(const PlateRecognition &recognition, const PlateBuildOptions &options,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0.0, "按点云识别结果构建板件");
    validateFrame(recognition.frame);
    if (!std::isfinite(options.endOverhangToleranceMm) || options.endOverhangToleranceMm < 0 ||
        options.endOverhangToleranceMm > 1000)
    {
        throw GeometryError("invalid_parameters", "端部突出容差必须在 0–1000 mm 范围内。");
    }
    if (recognition.plates.empty())
    {
        throw GeometryError("no_recognized_plates", "当前没有可构建的点云识别结果。");
    }
    auto implementation = std::make_shared<Model::Impl>();
    auto &summary = implementation->summary;
    summary.frame = recognition.frame;
    summary.fromPointCloud = true;
    summary.parameters.mirrorSymmetry = options.mirrorSymmetry;
    summary.recognitionOptions = options;
    summary.plates = modelPlates(recognition, options, summary.warnings);
    const auto &frame = summary.frame;
    gp_Trsf transform;
    transform.SetValues(frame.xAxis.x, frame.yAxis.x, frame.zAxis.x, frame.originMm.x,
                        frame.xAxis.y, frame.yAxis.y, frame.zAxis.y, frame.originMm.y,
                        frame.xAxis.z, frame.yAxis.z, frame.zAxis.z, frame.originMm.z);
    BRep_Builder builder;
    builder.MakeCompound(implementation->compound);
    for (const auto &observed : summary.plates)
    {
        checkpoint(stop, progress, static_cast<double>(implementation->plates.size()) / summary.plates.size(),
                   "生成已识别板件；未识别板厚保留观测面");
        const auto &low = observed.minimumMm;
        const auto &high = observed.maximumMm;
        TopoDS_Shape shape = recognizedShape(observed);
        if (observed.thicknessMm)
        {
            summary.volumeMm3 += checkedVolume(shape);
            ++summary.solidCount;
        }
        else
        {
            ++summary.surfaceCount;
        }
        shape = BRepBuilderAPI_Transform(shape, transform, true).Shape();
        if (!BRepCheck_Analyzer(shape).IsValid())
        {
            throw GeometryError("invalid_recognition", "识别板件未通过几何有效性检查。");
        }
        builder.Add(implementation->compound, shape);
        appendMesh(shape, static_cast<std::uint32_t>(implementation->plates.size()), implementation->mesh);
        implementation->plates.push_back({observed.id, low, high, shape});
        summary.partIds.push_back(observed.id);
        implementation->mesh.partIds.push_back(observed.id);
    }
    checkpoint(stop, progress, 1.0, "点云识别模型构建完成");
    return std::make_shared<Model>(std::move(implementation));
}

std::shared_ptr<const Model> associateModelCloud(const Model &model, const CloudData &cloud, std::stop_token stop)
{
    auto result = std::make_shared<Model::Impl>(*model.implementation_);
    std::vector<detail::LocalPlateSource> sources;
    for (const auto &plate : model.summary().plates)
        sources.push_back({plate, model.summary().frame, {}});
    const auto &f = model.summary().frame;
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 4096 == 0)
            checkpoint(stop, {}, 0, {});
        const auto &p = cloud.points[i];
        const Vec3 relative{p.position.x + cloud.originMm.x - f.originMm.x,
                            p.position.y + cloud.originMm.y - f.originMm.y,
                            p.position.z + cloud.originMm.z - f.originMm.z};
        const double q[] = {dot(relative, f.xAxis), dot(relative, f.yAxis), dot(relative, f.zAxis)};
        double best = std::numeric_limits<double>::infinity();
        std::size_t chosen = sources.size();
        for (std::size_t j = 0; j < sources.size(); ++j)
        {
            const auto &plate = sources[j].plate;
            const double low[] = {plate.minimumMm.x, plate.minimumMm.y, plate.minimumMm.z};
            const double high[] = {plate.maximumMm.x, plate.maximumMm.y, plate.maximumMm.z};
            double distance = 0;
            for (int a = 0; a < 3; ++a)
            {
                const double d = a == plate.normalAxis ? std::min(std::abs(q[a] - low[a]), std::abs(q[a] - high[a]))
                                                       : std::max({low[a] - q[a], q[a] - high[a], 0.0});
                distance += d * d;
            }
            // 仅关联原板面附近的点，远离原模型的杂点不参与后续局部重建。
            if (distance <= 25.0 && distance < best)
            {
                best = distance;
                chosen = j;
            }
        }
        if (chosen < sources.size())
            sources[chosen].indices.push_back(p.sourceIndex);
    }
    result->summary.reconstructableParts.clear();
    for (auto &source : sources)
    {
        std::sort(source.indices.begin(), source.indices.end());
        source.indices.erase(std::unique(source.indices.begin(), source.indices.end()), source.indices.end());
        if (source.indices.size() < 12)
            continue;
        const auto id = source.plate.id;
        result->cloudSources[id] = std::make_shared<const detail::LocalPlateSource>(std::move(source));
        result->summary.reconstructableParts.push_back(id);
    }
    return std::make_shared<const Model>(std::move(result));
}

std::shared_ptr<const Model> reconstructLocalParts(const Model &model, const CloudData &cloud,
                                                   const LocalReconstruction &request, std::stop_token stop,
                                                   const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "检查局部重建规则和点云关联");
    const std::set<std::string> types{"plate", "horizontal", "box"};
    const std::set<std::string> contours{"circle", "rectangle", "chamfer_rectangle", "convex", "polygon", "smooth"};
    if (!types.contains(request.type) || !contours.contains(request.contour))
        throw GeometryError("invalid_rule", "局部重建类型或轮廓规则无效。");
    if (!std::isfinite(request.toleranceMm) || request.toleranceMm < 0.1 || request.toleranceMm > 1000 ||
        !std::isfinite(request.cornerMm) || request.cornerMm <= 0 || request.cornerMm > 100000)
        throw GeometryError("invalid_rule", "轮廓容差须为 0.1–1000 mm，倒角退距须为 0–100000 mm 的正数。");
    std::set<std::string> selected(request.partIds.begin(), request.partIds.end());
    if (selected.empty() || selected.size() != request.partIds.size())
        throw GeometryError("invalid_parts", "请选择至少一个部件，部件 ID 不可重复。");
    const auto &source = *model.implementation_;
    for (const auto &id : selected)
    {
        if (std::find(source.summary.partIds.begin(), source.summary.partIds.end(), id) == source.summary.partIds.end())
            throw GeometryError("unknown_part", "部件不存在：" + id);
        if (!source.cloudSources.contains(id))
            throw GeometryError("no_local_source", "部件 " + id + "：" +
                (source.summary.localSourceUnavailable.contains(id) ? source.summary.localSourceUnavailable.at(id) :
                 "没有足够的原始点云关联，不能局部重建。"));
        if (source.summary.partTransforms.contains(id))
            throw GeometryError("moved_local_source",
                                "部件 " + id + " 已移动或旋转，与原点云位置不一致；请撤销变换后再局部重建。");
    }
    auto result = std::make_shared<Model::Impl>(source);
    result->mesh = {};
    result->mesh.partIds = result->summary.partIds;
    result->summary.solidCount = 0;
    result->summary.surfaceCount = 0;
    result->summary.volumeMm3 = 0;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    for (std::size_t index = 0; index < result->plates.size(); ++index)
    {
        auto &part = result->plates[index];
        checkpoint(stop, progress, 0.1 + 0.8 * index / result->plates.size(), "局部重建：" + part.id);
        try
        {
            if (selected.contains(part.id))
            {
                const auto &support = *source.cloudSources.at(part.id);
                std::vector<Vec3> remaining;
                for (std::size_t i = 0; i < cloud.points.size(); ++i)
                {
                    if (i % 4096 == 0)
                        checkpoint(stop, {}, 0, {});
                    const auto &p = cloud.points[i];
                    if (std::binary_search(support.indices.begin(), support.indices.end(), p.sourceIndex))
                        remaining.push_back({p.position.x + cloud.originMm.x, p.position.y + cloud.originMm.y,
                                             p.position.z + cloud.originMm.z});
                }
                auto thickness = support.plate.thicknessMm;
                if (source.summary.thicknessOverrides.contains(part.id))
                    thickness = source.summary.thicknessOverrides.at(part.id).thicknessMm;
                const auto rebuilt = detail::rebuildLocalShape(remaining, support, thickness, request, stop);
                part.shape = rebuilt.shape;
                std::erase(result->summary.edgeTreatedParts, part.id);
                std::erase(result->summary.holeEditedParts, part.id);
                std::erase(result->summary.contourExpandedParts, part.id);
                std::erase(result->summary.mirrorCompletedParts, part.id);
                if (std::find(result->summary.locallyReconstructedParts.begin(),
                              result->summary.locallyReconstructedParts.end(),
                              part.id) == result->summary.locallyReconstructedParts.end())
                    result->summary.locallyReconstructedParts.push_back(part.id);
                if (rebuilt.thickness)
                    result->summary.thicknessOverrides[part.id] = *rebuilt.thickness;
                else
                    result->summary.thicknessOverrides.erase(part.id);
                for (auto &plate : result->summary.plates)
                    if (plate.id == part.id)
                    {
                        plate.supportPoints = remaining.size();
                        plate.method = "local_reconstruction";
                        if (result->summary.thicknessOverrides.contains(part.id))
                            plate.thicknessMm = result->summary.thicknessOverrides.at(part.id).thicknessMm;
                    }
            }
            else
                part.shape = BRepBuilderAPI_Copy(part.shape, true, false).Shape();
            builder.Add(result->compound, part.shape);
            if (part.shape.ShapeType() == TopAbs_SOLID)
            {
                ++result->summary.solidCount;
                result->summary.volumeMm3 += checkedVolume(part.shape);
            }
            else
                ++result->summary.surfaceCount;
            appendMesh(part.shape, static_cast<std::uint32_t>(index), result->mesh);
        }
        catch (const GeometryError &error)
        {
            throw GeometryError(error.code(), "部件 " + part.id + "：" + error.what() + " 整批未应用。");
        }
        catch (const Standard_Failure &error)
        {
            throw GeometryError("local_rebuild_failed",
                                "部件 " + part.id + " 局部重建失败，整批未应用：" + error.what());
        }
    }
    checkpoint(stop, progress, 1, "局部重建完成，未选部件保持不变");
    return std::make_shared<const Model>(std::move(result));
}

StepExportResult exportCwrm(const Model& model, const std::filesystem::path& path,
    std::stop_token stop, const ProgressCallback& progress)
{
    return detail::writeCwrmShape(model.implementation_->compound, model.summary(), path, stop, progress);
}

StepExportResult exportStep(const Model& model, const std::filesystem::path& requestedPath,
    std::stop_token stop, const ProgressCallback& progress)
{
    checkpoint(stop, progress, 0.0, "准备 STEP 装配导出");
    if (requestedPath.empty()) throw GeometryError("invalid_output", "必须指定 STEP 输出路径");
    const auto target = std::filesystem::absolute(requestedPath);
    std::error_code error;
    if (std::filesystem::exists(target, error))
        throw GeometryError("output_exists", "输出文件已存在，请选择新文件名");
    if (error || !std::filesystem::is_directory(target.parent_path()))
        throw GeometryError("output_not_writable", "STEP 输出目录不存在或不可访问");
    detail::StagedFile staged(target.parent_path());
    const auto& source = *model.implementation_;
    StepExportResult result;
    try {
        const Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        const auto colorTool = XCAFDoc_DocumentTool::ColorTool(document->Main());
        XCAFDoc_DocumentTool::SetLengthUnit(document, 0.001);
        const auto assembly = shapeTool->NewShape();
        TDataStd_Name::Set(assembly, !source.summary.importedFormat.empty() ? "ImportedModel" :
            source.summary.fromPointCloud ? "RecognizedPlates" : "AssistedBeam");
        for (std::size_t index = 0; index < source.plates.size(); ++index) {
            const auto& plate = source.plates[index];
            // 导入装配可能多次引用同一拓扑；独立复制拓扑以保留各部件名称和实例位置。
            const auto exportedShape = source.summary.importedFormat.empty() ? plate.shape :
                BRepBuilderAPI_Copy(plate.shape, false, false).Shape();
            const auto part = shapeTool->AddShape(exportedShape, false);
            TDataStd_Name::Set(part, TCollection_ExtendedString(plate.id.c_str()));
            const auto instance = shapeTool->AddComponent(assembly, part, TopLoc_Location());
            TDataStd_Name::Set(instance, TCollection_ExtendedString(plate.id.c_str()));
            colorTool->SetColor(part, Quantity_Color(index < 3 ? 0.35 : 0.85, 0.65, 0.85,
                                                     Quantity_TOC_RGB), XCAFDoc_ColorGen);
        }
        shapeTool->UpdateAssemblies();
        STEPCAFControl_Writer writer;
        writer.SetNameMode(true);
        writer.SetColorMode(true);
        DESTEP_Parameters options;
        options.WriteSchema = DESTEP_Parameters::WriteMode_StepSchema_AP214IS;
        options.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
        if (!writer.Transfer(document, options, STEPControl_AsIs))
            throw GeometryError("step_transfer_failed", "XCAF 到 STEP 转换失败");
        checkpoint(stop, progress, 0.35, "写出临时 STEP 文件");
        std::ofstream output(staged.file, std::ios::binary | std::ios::trunc);
        if (!output || writer.WriteStream(output) != IFSelect_RetDone)
            throw GeometryError("step_write_failed", "STEP 文件写出失败");
        output.flush();
        if (!output) throw GeometryError("step_write_failed", "STEP 文件刷新失败");
        output.close();
        if (output.fail()) throw GeometryError("step_write_failed", "STEP 文件关闭失败");
        checkpoint(stop, progress, 0.6, "独立回读 STEP 并检查实体");
        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        reader.SetColorMode(true);
        std::ifstream input(staged.file, std::ios::binary);
        if (!input || reader.ReadStream("verified.step", input) != IFSelect_RetDone)
            throw GeometryError("step_readback_failed", "STEP 回读解析失败");
        reader.ChangeReader().SetSystemLengthUnit(1.0);
        const Handle(TDocStd_Document) restoredDocument = new TDocStd_Document("BinXCAF");
        XCAFDoc_DocumentTool::SetLengthUnit(restoredDocument, 0.001);
        if (!reader.Transfer(restoredDocument))
            throw GeometryError("step_readback_failed", "STEP 回读文档转换失败");
        const auto restoredTool = XCAFDoc_DocumentTool::ShapeTool(restoredDocument->Main());
        const auto restoredShape = restoredTool->GetOneShape();
        NCollection_Sequence<TDF_Label> roots;
        restoredTool->GetFreeShapes(roots);
        NCollection_Sequence<TDF_Label> components;
        if (roots.Length() != 1 || !XCAFDoc_ShapeTool::GetComponents(roots.Value(1), components) ||
            components.Length() != static_cast<int>(source.plates.size()))
            throw GeometryError("step_validation_failed", "STEP 回读的根装配或板件实例数量不一致");
        std::vector<bool> namedParts(source.plates.size(), false);
        for (int index = 1; index <= components.Length(); ++index) {
            TDF_Label referred;
            if (!XCAFDoc_ShapeTool::GetReferredShape(components.Value(index), referred))
                throw GeometryError("step_validation_failed", "STEP 装配包含无法解析的板件引用");
            Handle(TDataStd_Name) name;
            if (!referred.FindAttribute(TDataStd_Name::GetID(), name))
                throw GeometryError("step_validation_failed", "STEP 回读丢失板件名称");
            bool matched = false;
            for (std::size_t part = 0; part < source.plates.size(); ++part) {
                if (name->Get().IsEqual(TCollection_ExtendedString(source.plates[part].id.c_str()))) {
                    if (namedParts[part]) throw GeometryError("step_validation_failed", "STEP 回读板件名称重复");
                    namedParts[part] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched) throw GeometryError("step_validation_failed", "STEP 回读板件名称与原模型不一致");
        }
        for (TopExp_Explorer solids(restoredShape, TopAbs_SOLID); solids.More(); solids.Next()) {
            result.volumeMm3 += checkedVolume(solids.Current());
            ++result.solidCount;
        }
        for (TopExp_Explorer faces(restoredShape, TopAbs_FACE, TopAbs_SOLID); faces.More(); faces.Next()) {
            ++result.surfaceCount;
        }
        if (result.surfaceCount != source.summary.surfaceCount)
            throw GeometryError("step_validation_failed", "STEP 回读的独立观测面数量不一致");
        if (result.solidCount != source.summary.solidCount ||
            std::abs(result.volumeMm3 - source.summary.volumeMm3) >
                std::max(1e-4, source.summary.volumeMm3 * 1e-8))
            throw GeometryError("step_validation_failed", "STEP 回读的实体数量或体积与原模型不一致");
        const auto before = bounds(source.compound);
        const auto after = bounds(restoredShape);
        for (std::size_t index = 0; index < before.size(); ++index) {
            if (!std::isfinite(after[index]) || std::abs(before[index] - after[index]) > 1e-4)
                throw GeometryError("step_validation_failed", "STEP 回读的位置或包围尺寸与原模型不一致");
        }
        double restoredUnit{};
        if (!XCAFDoc_DocumentTool::GetLengthUnit(restoredDocument, restoredUnit) ||
            std::abs(restoredUnit - 0.001) > 1e-12)
            throw GeometryError("step_validation_failed", "STEP 回读文档单位不是毫米");
        input.close();
        result.fileBytes = std::filesystem::file_size(staged.file);
        checkpoint(stop, progress, 0.95, "提交已验证的 STEP 文件");
        detail::commitWithoutOverwrite(staged.file, target);
        result.valid = true;
    } catch (const Standard_Failure& failure) {
        throw GeometryError("step_failed", failure.what());
    }
    // 提交后不再处理取消；文件成功落盘必须返回成功状态。
    return result;
}

DeviationResult evaluateDeviation(const CloudData& cloud, const Model& model,
    std::stop_token stop, const ProgressCallback& progress)
{
    if (!model.summary().mirrorCompletedParts.empty() || !model.summary().mirroredParts.empty())
        throw GeometryError("unsupported_deviation", "镜像补全后不再适用原矩形板件偏差算法。");
    if (!model.summary().contourExpandedParts.empty() || !model.summary().locallyReconstructedParts.empty())
    {
        throw GeometryError("unsupported_deviation", "外扩轮廓后的模型暂不适用原矩形板件偏差算法。");
    }
    if (!model.summary().thicknessOverrides.empty())
        throw GeometryError("unsupported_deviation", "修改板厚后的模型暂不适用矩形板件偏差算法。");
    if (!model.summary().edgeTreatedParts.empty() || !model.summary().holeEditedParts.empty())
        throw GeometryError("unsupported_deviation", "圆角/倒角后的模型不适用矩形板件偏差算法。");
    if (!model.summary().importedFormat.empty())
        throw GeometryError("unsupported_deviation", "导入模型不适用矩形板件偏差算法");
    if (cloud.points.empty()) throw GeometryError("empty_cloud", "没有可比较的有效点");
    const auto& source = *model.implementation_;
    std::vector<BeamFrame> frames;
    for (const auto &plate : source.plates) frames.push_back(modelPartFrame(source.summary, plate.id));
    DeviationResult result;
    result.pointCount = cloud.points.size();
    result.distancesMm.reserve(cloud.points.size());
    result.nearestPartIndices.reserve(cloud.points.size());
    long double sum = 0, squaredSum = 0;
    for (std::size_t index = 0; index < cloud.points.size(); ++index) {
        if (index % 4096 == 0)
            checkpoint(stop, progress, 0.95 * index / cloud.points.size(), "计算原始有效点到有限板件表面的偏差");
        const auto& point = cloud.points[index].position;
        const Vec3 world{point.x + cloud.originMm.x, point.y + cloud.originMm.y, point.z + cloud.originMm.z};
        double distance = std::numeric_limits<double>::infinity();
        std::uint32_t nearest{};
        for (std::size_t part = 0; part < source.plates.size(); ++part) {
            const double candidate = boxSurfaceDistance(inverseTransformPoint(frames[part], world), source.plates[part]);
            if (candidate < distance) { distance = candidate; nearest = static_cast<std::uint32_t>(part); }
        }
        if (!std::isfinite(distance)) throw GeometryError("deviation_failed", "偏差计算出现无效数值");
        result.distancesMm.push_back(distance);
        result.nearestPartIndices.push_back(nearest);
        sum += distance;
        squaredSum += static_cast<long double>(distance) * distance;
        result.maximumMm = std::max(result.maximumMm, distance);
    }
    result.meanMm = static_cast<double>(sum / cloud.points.size());
    result.rmsMm = std::sqrt(static_cast<double>(squaredSum / cloud.points.size()));
    auto sorted = result.distancesMm;
    const auto quantile = static_cast<std::size_t>(std::ceil(0.95 * sorted.size())) - 1;
    std::nth_element(sorted.begin(), sorted.begin() + static_cast<std::ptrdiff_t>(quantile), sorted.end());
    result.p95Mm = sorted[quantile];
    checkpoint(stop, progress, 1.0, "有限板件表面偏差计算完成");
    return result;
}

} // namespace reverse
