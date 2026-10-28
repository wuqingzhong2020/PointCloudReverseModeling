/*
 * 文件描述：
 * 实现版本化 JSON 摘要与辅助建模参数读取，避免将点云大数组和第三方对象传入命令消息。
 */
#include "commands/Json.h"
#include "geometry/Geometry.h"

#include <cmath>

namespace reverse {
namespace {
using nlohmann::json;
json vectorJson(const Vec3& v) { return json::array({v.x, v.y, v.z}); }
json frameJson(const BeamFrame& f) {
    return {{"originMm", vectorJson(f.originMm)}, {"xAxis", vectorJson(f.xAxis)},
            {"yAxis", vectorJson(f.yAxis)}, {"zAxis", vectorJson(f.zAxis)}};
}
json parametersJson(const BeamParameters& p) {
    return {{"length", p.length}, {"width", p.width}, {"height", p.height},
            {"webThickness", p.webThickness}, {"flangeThickness", p.flangeThickness},
            {"ribThickness", p.ribThickness}, {"ribStations", p.ribStations},
            {"swapCrossSection", p.swapCrossSection}};
}
double number(const json& data, const char* name) {
    if (!data.contains(name) || !data.at(name).is_number())
        throw GeometryError("invalid_parameters", std::string("Expected numeric field: ") + name);
    const auto value = data.at(name).get<double>();
    if (!std::isfinite(value)) throw GeometryError("invalid_parameters", "Parameter must be finite.");
    return value;
}
Vec3 vectorValue(const json& value) {
    if (!value.is_array() || value.size() != 3)
        throw GeometryError("invalid_frame", "Frame vectors must contain three finite numbers.");
    Vec3 result{value.at(0).get<double>(), value.at(1).get<double>(), value.at(2).get<double>()};
    if (!std::isfinite(result.x) || !std::isfinite(result.y) || !std::isfinite(result.z))
        throw GeometryError("invalid_frame", "Frame vectors must be finite.");
    return result;
}
} // namespace

const char* taskStateName(TaskState state) noexcept {
    switch (state) {
    case TaskState::Queued: return "queued";
    case TaskState::Running: return "running";
    case TaskState::Succeeded: return "succeeded";
    case TaskState::Failed: return "failed";
    case TaskState::Cancelled: return "cancelled";
    }
    return "unknown";
}

LengthUnit parseLengthUnit(std::string_view value) {
    if (value == "mm") return LengthUnit::Millimeter;
    if (value == "cm") return LengthUnit::Centimeter;
    if (value == "m") return LengthUnit::Meter;
    throw GeometryError("invalid_unit", "Explicit input unit must be mm, cm or m.");
}

json toJson(const TaskSnapshot& task) {
    return {{"schemaVersion", 1}, {"taskId", task.id}, {"operation", task.operation},
            {"state", taskStateName(task.state)}, {"progress", task.progress},
            {"message", task.message}, {"errorCode", task.errorCode},
            {"cancellationRequested", task.cancellationRequested}};
}

json toJson(const ProjectSnapshot& project) {
    json result{{"schemaVersion", 1}, {"revision", project.revision}, {"lengthUnit", "mm"},
                {"cloudId", project.cloudId}, {"modelId", project.modelId}, {"warnings", project.warnings}};
    if (project.cloud) {
        const auto& c = *project.cloud;
        result["cloud"] = {{"sourcePath", project.sourcePathUtf8}, {"sourcePointCount", c.sourcePointCount},
            {"validPointCount", c.points.size()}, {"invalidPointCount", c.invalidPointCount},
            {"fields", c.fields}, {"hasIntensity", c.hasIntensity}, {"sourceScaleToMm", c.sourceScaleToMm},
            {"originMm", vectorJson(c.originMm)}, {"boundsMinMm", vectorJson(c.boundsMinMm)},
            {"boundsMaxMm", vectorJson(c.boundsMaxMm)}};
    }
    if (project.estimate) {
        const auto& e = *project.estimate;
        result["estimate"] = {{"method", "pca_oriented_bounds_initial_guess"}, {"frame", frameJson(e.frame)},
            {"length", e.length}, {"width", e.width}, {"height", e.height}, {"warnings", e.warnings}};
    }
    if (project.model) {
        const auto& m = *project.model;
        result["model"] = {{"method", "assisted_parametric_h_beam"}, {"frame", frameJson(m.frame)},
            {"parameters", parametersJson(m.parameters)}, {"solidCount", m.solidCount},
            {"volumeMm3", m.volumeMm3}, {"partIds", m.partIds}};
    }
    if (project.suggestedParameters) {
        const auto& suggestion = *project.suggestedParameters;
        result["suggestedParameters"] = {{"profile", suggestion.profile},
            {"parameters", parametersJson(suggestion.parameters)}, {"notes", suggestion.notes},
            {"source", "reference_preset_not_measurement"}};
    }
    if (project.lastExport) {
        const auto& e = *project.lastExport;
        result["step"] = {{"path", project.exportedPathUtf8}, {"valid", e.valid},
            {"solidCount", e.solidCount}, {"volumeMm3", e.volumeMm3}, {"fileBytes", e.fileBytes}};
    }
    if (project.deviation) {
        const auto& d = *project.deviation;
        result["deviation"] = {{"method", d.method}, {"pointCount", d.pointCount}, {"meanMm", d.meanMm},
            {"rmsMm", d.rmsMm}, {"p95Mm", d.p95Mm}, {"maximumMm", d.maximumMm},
            {"scope", "point_to_separate_rectangular_plate_surfaces; not a dimensional acceptance verdict"}};
    }
    return result;
}

BeamParameters parseBeamParameters(const json& value) {
    if (!value.is_object()) throw GeometryError("invalid_parameters", "Expected parameter JSON object.");
    if (value.value("schemaVersion", 1) != 1)
        throw GeometryError("unsupported_schema", "Only parameter schemaVersion 1 is supported.");
    if (value.value("lengthUnit", std::string("mm")) != "mm")
        throw GeometryError("invalid_unit", "Model parameter lengths must be in mm.");
    const auto& p = value.contains("parameters") ? value.at("parameters") : value;
    BeamParameters result;
    result.length = number(p, "length");
    result.width = number(p, "width");
    result.height = number(p, "height");
    result.webThickness = number(p, "webThickness");
    result.flangeThickness = number(p, "flangeThickness");
    result.ribThickness = p.contains("ribThickness") ? number(p, "ribThickness") : 0.0;
    result.ribStations = p.value("ribStations", std::vector<double>{});
    result.swapCrossSection = p.value("swapCrossSection", false);
    for (const auto station : result.ribStations)
        if (!std::isfinite(station)) throw GeometryError("invalid_parameters", "Rib stations must be finite.");
    return result;
}

std::optional<BeamFrame> parseFrameOverride(const json& value) {
    if (!value.contains("frame")) return {};
    const auto& f = value.at("frame");
    return BeamFrame{vectorValue(f.at("originMm")), vectorValue(f.at("xAxis")),
                     vectorValue(f.at("yAxis")), vectorValue(f.at("zAxis"))};
}
} // namespace reverse
