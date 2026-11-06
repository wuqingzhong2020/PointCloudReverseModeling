/* 文件描述：从俯视腹板的封闭缺点区域提取有环周支持的圆孔/长圆孔，并生成贯穿板厚的孔。 */
#pragma once
#include <stop_token>

#include <TopoDS_Shape.hxx>

#include "core/Types.h"

namespace reverse::detail
{
void recognizeWebHoles(const CloudData &cloud, PlateRecognition &recognition, std::stop_token stop);
TopoDS_Shape cutObservedWebHoles(const TopoDS_Shape &shape, const RecognizedPlate &plate, std::stop_token stop);
}
