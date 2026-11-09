/* 文件描述：俯视线扫的一底板、多立板/围板焊件识别及连接组轮廓建模。 */
#pragma once

#include <TopoDS_Shape.hxx>

#include "geometry/Geometry.h"

namespace reverse
{
[[nodiscard]] PlateRecognition recognizeOtherModel(const CloudData &cloud,
    std::stop_token stop = {}, const ProgressCallback &progress = {});

namespace detail
{
[[nodiscard]] TopoDS_Shape buildUprightFootprint(const RecognizedPlate &plate);
// 四直边、四圆角的等厚围板；厚度沿轮廓法向变化，保持中间轮廓和高度。
[[nodiscard]] std::optional<PlateThicknessInfo> enclosureThickness(const RecognizedPlate &plate);
void resizeEnclosure(RecognizedPlate &plate, double thicknessMm);
bool recognizeEnclosureThickness(RecognizedPlate &plate, const std::vector<Vec3> &points, double tolerance);
}
}
