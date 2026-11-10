/* 文件描述：构建参数化梁与识别板件，处理板件轮廓、镜像及装配约束。 */

#include <algorithm>
#include <cmath>
#include <memory>
#include <string>
#include <utility>

#include <BRepAlgoAPI_Common.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeHalfSpace.hxx>
#include <BRep_Builder.hxx>
#include <Standard_Failure.hxx>
#include <gp_Pln.hxx>
#include <gp_Trsf.hxx>

#include "geometry/ModelState.h"
#include "geometry/TBarRecognition.h"

namespace reverse
{
using detail::model::appendMesh;
using detail::model::checkedVolume;
using detail::model::checkpoint;
using detail::model::recognizedShape;

namespace
{
Vec3 negate(Vec3 value)
{
    return {-value.x, -value.y, -value.z};
}
double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
Vec3 cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
bool finite(Vec3 value)
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

void validateFrame(const BeamFrame &frame)
{
    if (!finite(frame.originMm) || !finite(frame.xAxis) || !finite(frame.yAxis) || !finite(frame.zAxis))
        throw GeometryError("invalid_frame", "梁框架必须包含有限坐标");
    constexpr double tolerance = 1e-8;
    if (std::abs(dot(frame.xAxis, frame.xAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.yAxis, frame.yAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.zAxis, frame.zAxis) - 1.0) > tolerance ||
        std::abs(dot(frame.xAxis, frame.yAxis)) > tolerance || std::abs(dot(frame.xAxis, frame.zAxis)) > tolerance ||
        std::abs(dot(frame.yAxis, frame.zAxis)) > tolerance ||
        dot(cross(frame.xAxis, frame.yAxis), frame.zAxis) < 1.0 - tolerance)
        throw GeometryError("invalid_frame", "梁框架必须是正交的右手单位坐标系");
}

void validateParameters(const BeamParameters &parameters)
{
    for (const auto value :
         {parameters.length, parameters.width, parameters.height, parameters.webThickness, parameters.flangeThickness})
    {
        if (!std::isfinite(value) || value <= 1e-5 || value > 1e9)
            throw GeometryError("invalid_dimensions", "长度、宽度、高度及板厚必须是有效正数（mm）");
    }
    if (parameters.webThickness >= parameters.width || 2.0 * parameters.flangeThickness >= parameters.height)
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
    for (const double station : stations)
    {
        if (!std::isfinite(station) || station < parameters.ribThickness / 2.0 ||
            station > parameters.length - parameters.ribThickness / 2.0)
            throw GeometryError("invalid_ribs", "肋板站位必须使整块肋板位于梁长范围内");
    }
    std::sort(stations.begin(), stations.end());
    for (std::size_t index = 1; index < stations.size(); ++index)
    {
        if (stations[index] - stations[index - 1] < parameters.ribThickness + 1e-6)
            throw GeometryError("overlapping_ribs", "肋板站位重叠或重合，请修正站位/厚度");
    }
}

std::vector<RecognizedPlate> modelPlates(const PlateRecognition &recognition, const PlateBuildOptions &options,
                                         std::vector<std::string> &warnings)
{
    auto plates = recognition.plates;
    for (auto &plate : plates)
    {
        if (!options.recognizeWebHoles)
            plate.holes.clear();
        else
            std::erase_if(plate.holes, [&](const auto &hole) { return hole.radiusMm < options.minimumHoleRadiusMm; });
    }
    const auto base = std::find_if(plates.begin(), plates.end(), [](const auto &plate) { return plate.id == "web"; });
    const auto first =
        std::find_if(plates.begin(), plates.end(), [](const auto &plate) { return plate.id == "flange-bottom"; });
    const auto second =
        std::find_if(plates.begin(), plates.end(), [](const auto &plate) { return plate.id == "flange-top"; });
    if (base != plates.end() && first != plates.end() && second != plates.end() && options.endOverhangToleranceMm > 0)
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
        const auto mirrorPoint = [&](Vec3 &p) { p.y = 2 * middle - p.y; };
        for (auto &segment : mirrored.outline)
        {
            mirrorPoint(segment.firstMm);
            mirrorPoint(segment.lastMm);
            if (segment.arcMiddleMm)
                mirrorPoint(*segment.arcMiddleMm);
        }
        if (plate.normalAxis == 2)
        {
            const auto trimBase = [](std::vector<PlateBoundarySegment> &outline, double from, double to)
            {
                for (auto &segment : outline)
                {
                    for (auto *p : {&segment.firstMm, &segment.lastMm})
                        if (std::abs(p->y - from) < 1e-6)
                            p->y = to;
                }
            };
            trimBase(plates[index].outline, plate.minimumMm.y, middle);
            trimBase(mirrored.outline, 2 * middle - plate.minimumMm.y, middle);
        }
        mirrored.method = "explicit_mirror_of_" + plate.method;
        plates.push_back(std::move(mirrored));
    }
    return plates;
}
} // namespace

std::shared_ptr<const Model> buildBeam(const BeamFrame &inputFrame, const BeamParameters &parameters,
                                       std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0.0, "检查建模参数");
    validateFrame(inputFrame);
    validateParameters(parameters);
    auto implementation = std::make_shared<Model::Impl>();
    auto &frame = implementation->summary.frame;
    frame = inputFrame;
    if (parameters.swapCrossSection)
    {
        frame.yAxis = inputFrame.zAxis;
        frame.zAxis = negate(inputFrame.yAxis);
    }
    implementation->summary.parameters = parameters;
    const auto &p = parameters;
    const double x0 = -p.length / 2.0, x1 = p.length / 2.0;
    const double y0 = -p.width / 2.0, y1 = p.width / 2.0;
    const double z0 = -p.height / 2.0, z1 = p.height / 2.0;
    auto &plates = implementation->plates;
    const double sideMinimum = p.mirrorSymmetry ? y0 : -p.webThickness / 2.0;
    plates.push_back({"flange-bottom", {x0, sideMinimum, z0}, {x1, y1, z0 + p.flangeThickness}, {}});
    plates.push_back({"flange-top", {x0, sideMinimum, z1 - p.flangeThickness}, {x1, y1, z1}, {}});
    plates.push_back({"web",
                      {x0, -p.webThickness / 2.0, z0 + p.flangeThickness},
                      {x1, p.webThickness / 2.0, z1 - p.flangeThickness},
                      {}});
    for (std::size_t index = 0; index < p.ribStations.size(); ++index)
    {
        const double center = x0 + p.ribStations[index];
        const std::string prefix = "rib-" + std::to_string(index + 1);
        if (p.mirrorSymmetry)
        {
            plates.push_back({prefix + "-left",
                              {center - p.ribThickness / 2.0, y0, z0 + p.flangeThickness},
                              {center + p.ribThickness / 2.0, -p.webThickness / 2.0, z1 - p.flangeThickness},
                              {}});
        }
        plates.push_back({prefix + "-right",
                          {center - p.ribThickness / 2.0, p.webThickness / 2.0, z0 + p.flangeThickness},
                          {center + p.ribThickness / 2.0, y1, z1 - p.flangeThickness},
                          {}});
    }
    try
    {
        gp_Trsf transform;
        transform.SetValues(frame.xAxis.x, frame.yAxis.x, frame.zAxis.x, frame.originMm.x, frame.xAxis.y, frame.yAxis.y,
                            frame.zAxis.y, frame.originMm.y, frame.xAxis.z, frame.yAxis.z, frame.zAxis.z,
                            frame.originMm.z);
        BRep_Builder builder;
        builder.MakeCompound(implementation->compound);
        for (std::size_t index = 0; index < plates.size(); ++index)
        {
            checkpoint(stop, progress, 0.05 + 0.9 * index / plates.size(), "生成并检查板件实体");
            auto &plate = plates[index];
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
    }
    catch (const Standard_Failure &failure)
    {
        throw GeometryError("cad_failed", failure.what());
    }
    implementation->summary.solidCount = plates.size();
    checkpoint(stop, progress, 1.0, "辅助参数模型生成完成");
    return std::make_shared<Model>(std::move(implementation));
}

std::shared_ptr<const Model> buildRecognizedPlates(const PlateRecognition &recognition,
                                                   const PlateBuildOptions &options, std::stop_token stop,
                                                   const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0.0, "按点云识别结果构建板件");
    validateFrame(recognition.frame);
    validateTBarOptions(options);
    if (!std::isfinite(options.endOverhangToleranceMm) || options.endOverhangToleranceMm < 0 ||
        options.endOverhangToleranceMm > 1000)
    {
        throw GeometryError("invalid_parameters", "端部突出容差必须在 0–1000 mm 范围内。");
    }
    if (!std::isfinite(options.minimumHoleRadiusMm) || options.minimumHoleRadiusMm < 0 ||
        options.minimumHoleRadiusMm > 1000)
        throw GeometryError("invalid_parameters", "忽略小孔半径必须在 0–1000 mm 范围内。");
    if (!std::isfinite(options.obstacleExclusionDistanceMm) || options.obstacleExclusionDistanceMm < 0 ||
        options.obstacleExclusionDistanceMm > 1000)
        throw GeometryError("invalid_parameters", "钢板附近排除距离必须在 0–1000 mm 范围内。");
    if (recognition.plates.empty())
    {
        throw GeometryError("no_recognized_plates", "当前没有可构建的点云识别结果。");
    }
    auto implementation = std::make_shared<Model::Impl>();
    auto &summary = implementation->summary;
    summary.frame = recognition.frame;
    summary.fromPointCloud = true;
    summary.warnings = recognition.warnings;
    summary.parameters.mirrorSymmetry = options.mirrorSymmetry;
    summary.recognitionOptions = options;
    summary.recognizedPartFrames.insert(recognition.plateFrames.begin(), recognition.plateFrames.end());
    summary.plates = modelPlates(recognition, options, summary.warnings);
    BRep_Builder builder;
    builder.MakeCompound(implementation->compound);
    for (const auto &observed : summary.plates)
    {
        checkpoint(stop, progress, static_cast<double>(implementation->plates.size()) / summary.plates.size(),
                   "生成已识别板件；未识别板厚保留观测面");
        const auto &low = observed.minimumMm;
        const auto &high = observed.maximumMm;
        TopoDS_Shape shape = recognizedShape(observed, stop);
        const auto placement = modelPartFrame(summary, observed.id);
        validateFrame(placement);
        gp_Trsf partTransform;
        partTransform.SetValues(placement.xAxis.x, placement.yAxis.x, placement.zAxis.x, placement.originMm.x,
                                placement.xAxis.y, placement.yAxis.y, placement.zAxis.y, placement.originMm.y,
                                placement.xAxis.z, placement.yAxis.z, placement.zAxis.z, placement.originMm.z);
        shape = BRepBuilderAPI_Transform(shape, partTransform, true).Shape();
        if (options.type == "tbar" && observed.id.ends_with("-web"))
        {
            const auto baseId = observed.id.substr(0, observed.id.size() - 4) + "-base";
            const auto baseFrame = modelPartFrame(summary, baseId);
            const gp_Pnt origin(baseFrame.originMm.x, baseFrame.originMm.y, baseFrame.originMm.z);
            const gp_Dir up(baseFrame.yAxis.x, baseFrame.yAxis.y, baseFrame.yAxis.z);
            const auto face = BRepBuilderAPI_MakeFace(gp_Pln(origin, up)).Face();
            const auto half = BRepPrimAPI_MakeHalfSpace(face, origin.Translated(gp_Vec(up) * 10)).Solid();
            BRepAlgoAPI_Common trim(shape, half);
            if (!trim.IsDone() || trim.Shape().IsNull())
                throw GeometryError("tbar_join_failed", "T 排立板与底板贴合失败。");
            shape = trim.Shape();
        }
        if (observed.thicknessMm || !observed.uprightFootprint.empty())
        {
            summary.volumeMm3 += checkedVolume(shape);
            ++summary.solidCount;
        }
        else
        {
            ++summary.surfaceCount;
        }
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

} // namespace reverse
