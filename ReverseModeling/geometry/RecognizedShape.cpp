/* 文件描述：把识别板件的轮廓、厚度、孔转换为 OCCT 形状，供首次构建和板厚修改复用。 */

#include <cmath>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepPrimAPI_MakeBox.hxx>

#include "geometry/ModelState.h"
#include "geometry/OtherModelRecognition.h"
#include "geometry/ObservedWebHoles.h"
#include "geometry/ScanProfile.h"

namespace reverse::detail::model
{

TopoDS_Shape recognizedShape(const RecognizedPlate &plate, std::stop_token stop)
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
    if (!plate.uprightFootprint.empty())
    {
        return detail::buildUprightFootprint(plate);
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
        if (!plate.outline.empty())
            return detail::buildScanProfile(plate);
        TopoDS_Shape box = BRepPrimAPI_MakeBox(gp_Pnt(low.x, low.y, low.z), sizes[0], sizes[1], sizes[2]);
        return detail::cutObservedWebHoles(box, plate, stop);
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
    return detail::cutObservedWebHoles(BRepBuilderAPI_MakeFace(boundary.Wire()), plate, stop);
}

} // namespace reverse::detail::model
