/* 文件描述：规范化可录制的板件范围、目标面、截断面及逐板开孔参数。 */
#include "commands/PlateEditCommands.h"
#include "geometry/Geometry.h"
namespace reverse
{
namespace
{
const std::map<std::string, std::pair<std::string, std::string>> names{
    {"extend", {"model.extend_to_plate", "延长到板"}}, {"trim", {"model.trim_to_plate", "截取到板"}},
    {"merge", {"model.merge_to_plate", "合并到板"}},   {"split", {"model.split_to_plate", "分割到板"}},
    {"crop", {"model.crop_parts", "框选切割板件"}},    {"repair", {"model.repair_holes", "修复板件孔洞"}},
    {"holes", {"model.add_holes", "批量过焊孔"}}};
nlohmann::json vector(const Vec3 &v)
{
    return {v.x, v.y, v.z};
}
Vec3 vector(const nlohmann::json &v)
{
    return {v.at(0).get<double>(), v.at(1).get<double>(), v.at(2).get<double>()};
}
} // namespace
std::string plateEditCommand(const std::string &op)
{
    return names.at(op).first;
}
std::string plateEditTitle(const std::string &op)
{
    return names.at(op).second;
}
PlateEdit plateEditRequest(const std::string &command, const rm::Arguments &args)
{
    PlateEdit r;
    for (const auto &[key, value] : names)
        if (value.first == command)
            r.operation = key;
    const auto j = commandJson(rm::Value(args));
    r.parts = j.value("parts", std::vector<std::string>{});
    if (command == "model.edit_plate")
    {
        r.direct = true;
        r.operation = j.at("operation").get<std::string>();
        r.all = j.value("all", false);
    }
    r.target = j.value("target", std::string{});
    r.tolerance = j.value("tolerance", r.tolerance);
    r.maxDistance = j.value("max_distance", r.maxDistance);
    r.clearance = j.value("clearance", r.clearance);
    r.normalAngle = j.value("normal_angle", r.normalAngle);
    r.inplaneAngle = j.value("inplane_angle", r.inplaneAngle);
    r.maxGap = j.value("max_gap", r.maxGap);
    r.boundary = j.value("boundary", false);
    r.maxHoleArea = j.value("max_hole_area", 0.0);
    r.notchWidth = j.value("notch_width", r.notchWidth);
    r.notchDepth = j.value("notch_depth", r.notchDepth);
    r.simplifyTolerance = j.value("simplify_tolerance", r.simplifyTolerance);
    for (const auto &c : j.value("contacts", nlohmann::json::array()))
        r.contacts.push_back({c.at("part"), c.at("face")});
    for (const auto &c : j.value("cuts", nlohmann::json::array()))
        r.cuts.push_back({c.at("part"), vector(c.at("origin")), vector(c.at("normal")), c.at("keep") == "positive"});
    if (r.operation == "crop" && !r.direct)
        for (const auto &c : r.cuts)
            r.parts.push_back(c.part);
    for (const auto &h : j.value("targets", nlohmann::json::array()))
    {
        PlateHole hole;
        hole.partId = h.at("part");
        hole.bottomEdge = h.at("edge");
        hole.sizeMm = h.at("size");
        hole.kind = PlateHoleKind::Weld;
        hole.shape = h.at("shape") == "circle" ? PlateHoleShape::Circle : PlateHoleShape::Triangle;
        hole.first = h.at("first");
        hole.second = h.at("second");
        r.holes.push_back(hole);
        r.parts.push_back(hole.partId);
    }
    return r;
}
rm::Arguments plateEditArguments(const PlateEdit &r)
{
    nlohmann::json j = nlohmann::json::object();
    if (r.direct)
    {
        j = {{"operation", r.operation}, {"parts", r.parts}, {"target", r.target}, {"all", r.all},
             {"cuts", nlohmann::json::array()}};
        for (const auto &c : r.cuts)
            j["cuts"].push_back({{"part", c.part}, {"origin", vector(c.origin)}, {"normal", vector(c.normal)},
                                 {"keep", c.positive ? "positive" : "negative"}});
        return commandValue(j).as<rm::Arguments>();
    }
    if (r.operation != "holes" && r.operation != "crop")
        j["parts"] = r.parts;
    if (r.operation == "extend" || r.operation == "trim" || r.operation == "merge" || r.operation == "split")
        j["target"] = r.target;
    if (r.operation == "extend" || r.operation == "trim")
    {
        j["contacts"] = nlohmann::json::array();
        for (const auto &c : r.contacts)
            j["contacts"].push_back({{"part", c.part}, {"face", c.face}});
    }
    if (r.operation == "extend")
    {
        j["tolerance"] = r.tolerance;
        j["max_distance"] = r.maxDistance;
    }
    if (r.operation == "trim" || r.operation == "split")
        j["clearance"] = r.clearance;
    if (r.operation == "trim" || r.operation == "crop")
    {
        j["cuts"] = nlohmann::json::array();
        for (const auto &c : r.cuts)
            j["cuts"].push_back({{"part", c.part},
                                 {"origin", vector(c.origin)},
                                 {"normal", vector(c.normal)},
                                 {"keep", c.positive ? "positive" : "negative"}});
    }
    if (r.operation == "merge")
    {
        j["normal_angle"] = r.normalAngle;
        j["inplane_angle"] = r.inplaneAngle;
        j["max_gap"] = r.maxGap;
    }
    if (r.operation == "repair")
    {
        j["boundary"] = r.boundary;
        j["max_hole_area"] = r.maxHoleArea;
        j["notch_width"] = r.notchWidth;
        j["notch_depth"] = r.notchDepth;
        j["simplify_tolerance"] = r.simplifyTolerance;
    }
    if (r.operation == "holes")
    {
        j["targets"] = nlohmann::json::array();
        for (const auto &h : r.holes)
            j["targets"].push_back({{"part", h.partId},
                                    {"edge", h.bottomEdge},
                                    {"kind", "weld"},
                                    {"shape", h.shape == PlateHoleShape::Circle ? "circle" : "triangle"},
                                    {"size", h.sizeMm},
                                    {"first", h.first},
                                    {"second", h.second}});
    }
    return commandValue(j).as<rm::Arguments>();
}
} // namespace reverse
