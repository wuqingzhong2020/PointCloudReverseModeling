/* 文件描述：有序选点及完整点云索引使用工程坐标录制，回放不依赖视角。 */
#include "commands/PointModelingCommands.h"
namespace reverse
{
namespace
{
nlohmann::json vec(Vec3 p) { return {p.x,p.y,p.z}; }
Vec3 vec(const nlohmann::json &j) { return {j.at(0).get<double>(),j.at(1).get<double>(),j.at(2).get<double>()}; }
}
nlohmann::json pointModelingJson(const PointModeling &r)
{
    nlohmann::json j{{"shape",r.shape},{"method",r.method},{"origin",vec(r.origin)},
        {"normal",vec(r.normal)},{"axis",vec(r.xAxis)},{"width",r.width},{"length",r.length},
        {"diameter",r.diameter},{"height",r.height},{"to_plane",r.toPlane},{"cut",r.cut},
        {"scope",r.cutScope},{"targets",r.targets},{"indices",r.indices},{"section",r.section},
        {"reversed",r.reversed},{"offset",r.offset},{"points",nlohmann::json::array()}};
    for (const auto &p : r.points) j["points"].push_back(vec(p));
    return j;
}
PointModeling pointModelingRequest(const nlohmann::json &j)
{
    PointModeling r;
    r.shape=j.at("shape"); r.method=j.value("method",r.method);
    r.origin=vec(j.value("origin",vec(r.origin))); r.normal=vec(j.value("normal",vec(r.normal)));
    r.xAxis=vec(j.value("axis",vec(r.xAxis)));
    r.width=j.value("width",r.width); r.length=j.value("length",0.0);
    r.diameter=j.value("diameter",0.0); r.height=j.value("height",r.height);
    r.toPlane=j.value("to_plane",false); r.cut=j.value("cut",false);
    r.cutScope=j.value("scope",r.cutScope); r.targets=j.value("targets",r.targets);
    r.indices=j.value("indices",r.indices); r.section=j.value("section",true);
    r.reversed=j.value("reversed",false); r.offset=j.value("offset",10.0);
    for (const auto &p : j.value("points",nlohmann::json::array())) r.points.push_back(vec(p));
    return r;
}
}
