/*
 * 文件描述：
 * 根据提交的 H 型钢及肋板参数创建独立 OCCT 实体、显示网格和 XCAF 装配。
 * 实现 STEP 临时写出、独立回读核验及不覆盖提交，并计算点到有限矩形板件表面的距离。
 */
#include "reverse/Geometry.h"

#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <Bnd_Box.hxx>
#include <DESTEP_Parameters.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Trsf.hxx>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>

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
    BRepMesh_IncrementalMesh triangulate(shape, 0.1, false, 0.5, false);
    if (!triangulate.IsDone()) throw GeometryError("mesh_failed", "OCCT 显示三角化失败");
    for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
        const auto face = TopoDS::Face(faces.Current());
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
        }
    }
}

struct StagedFile {
    std::filesystem::path directory;
    std::filesystem::path file;
    explicit StagedFile(const std::filesystem::path& parent)
    {
        std::random_device random;
        for (int attempt = 0; attempt < 50; ++attempt) {
            std::ostringstream name;
            name << ".reverse-step-" << std::hex << random() << random();
            directory = parent / name.str();
            std::error_code error;
            if (std::filesystem::create_directory(directory, error)) {
                file = directory / "verified.step";
                return;
            }
            if (error && error != std::errc::file_exists)
                throw GeometryError("output_not_writable", "无法在输出目录中建立临时 STEP 文件");
        }
        throw GeometryError("output_not_writable", "无法分配独立的临时导出目录");
    }
    ~StagedFile()
    {
        std::error_code ignored;
        if (!file.empty()) std::filesystem::remove(file, ignored);
        if (!directory.empty()) std::filesystem::remove(directory, ignored);
    }
};

void commitWithoutOverwrite(const std::filesystem::path& temporary,
                            const std::filesystem::path& target)
{
#ifdef _WIN32
    // 不设置 MOVEFILE_REPLACE_EXISTING；同盘移动在目标已存在时失败。
    if (!MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH))
        throw GeometryError("output_commit_failed", "STEP 提交失败：目标已存在或输出目录不可写");
#else
    // 同文件系统硬链接原子创建目标名，已有目标不会被替换。
    std::error_code error;
    std::filesystem::create_hard_link(temporary, target, error);
    if (error) throw GeometryError("output_commit_failed", "STEP 提交失败：目标已存在或文件系统不支持原子提交");
#endif
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

} // namespace

struct Model::Impl {
    ModelSummary summary;
    MeshData mesh;
    std::vector<Plate> plates;
    TopoDS_Compound compound;
};

Model::Model(std::shared_ptr<const Impl> implementation)
    : implementation_(std::move(implementation))
{
    if (!implementation_) throw GeometryError("invalid_model", "模型实现不能为空");
}

const ModelSummary& Model::summary() const noexcept { return implementation_->summary; }
const MeshData& Model::mesh() const noexcept { return implementation_->mesh; }

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
    plates.push_back({"flange-bottom", {x0, y0, z0}, {x1, y1, z0 + p.flangeThickness}, {}});
    plates.push_back({"flange-top", {x0, y0, z1 - p.flangeThickness}, {x1, y1, z1}, {}});
    plates.push_back({"web", {x0, -p.webThickness / 2.0, z0 + p.flangeThickness},
                      {x1, p.webThickness / 2.0, z1 - p.flangeThickness}, {}});
    for (std::size_t index = 0; index < p.ribStations.size(); ++index) {
        const double center = x0 + p.ribStations[index];
        const std::string prefix = "rib-" + std::to_string(index + 1);
        plates.push_back({prefix + "-left", {center - p.ribThickness / 2.0, y0, z0 + p.flangeThickness},
                          {center + p.ribThickness / 2.0, -p.webThickness / 2.0, z1 - p.flangeThickness}, {}});
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
    StagedFile staged(target.parent_path());
    const auto& source = *model.implementation_;
    StepExportResult result;
    try {
        const Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        const auto colorTool = XCAFDoc_DocumentTool::ColorTool(document->Main());
        XCAFDoc_DocumentTool::SetLengthUnit(document, 0.001);
        const auto assembly = shapeTool->NewShape();
        TDataStd_Name::Set(assembly, "HBeam");
        for (std::size_t index = 0; index < source.plates.size(); ++index) {
            const auto& plate = source.plates[index];
            const auto part = shapeTool->AddShape(plate.shape, false);
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
        commitWithoutOverwrite(staged.file, target);
        result.valid = true;
    } catch (const Standard_Failure& failure) {
        throw GeometryError("step_failed", failure.what());
    }
    // 提交后不再处理取消；文件成功落盘必须返回成功状态。
    if (progress) progress(1.0, "STEP 写出及回读检查通过");
    return result;
}

DeviationResult evaluateDeviation(const CloudData& cloud, const Model& model,
    std::stop_token stop, const ProgressCallback& progress)
{
    if (cloud.points.empty()) throw GeometryError("empty_cloud", "没有可比较的有效点");
    const auto& source = *model.implementation_;
    const auto& frame = source.summary.frame;
    const auto originDifference = subtract(cloud.originMm, frame.originMm);
    DeviationResult result;
    result.pointCount = cloud.points.size();
    result.distancesMm.reserve(cloud.points.size());
    result.nearestPartIndices.reserve(cloud.points.size());
    long double sum = 0, squaredSum = 0;
    for (std::size_t index = 0; index < cloud.points.size(); ++index) {
        if (index % 4096 == 0)
            checkpoint(stop, progress, 0.95 * index / cloud.points.size(), "计算原始有效点到有限板件表面的偏差");
        const auto& point = cloud.points[index].position;
        const Vec3 displacement{point.x + originDifference.x, point.y + originDifference.y,
                                point.z + originDifference.z};
        const Vec3 local{dot(displacement, frame.xAxis), dot(displacement, frame.yAxis),
                         dot(displacement, frame.zAxis)};
        double distance = std::numeric_limits<double>::infinity();
        std::uint32_t nearest{};
        for (std::size_t part = 0; part < source.plates.size(); ++part) {
            const double candidate = boxSurfaceDistance(local, source.plates[part]);
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
