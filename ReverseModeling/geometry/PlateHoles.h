/* 文件描述：等厚板件底边识别及贯穿板厚的过焊孔、底边孔实体切除。 */
#pragma once
#include "core/Types.h"
#include <TopoDS_Shape.hxx>
#include <stop_token>
namespace reverse::detail
{
std::vector<PlateHoleBase> plateHoleBases(const TopoDS_Shape &shape);
// 纯刀具构造，不执行布尔运算；绿色位置预览与实际切孔共用，坐标单位为 mm。
std::vector<TopoDS_Shape> plateHoleTools(const PlateHoleBase &base, const PlateHole &request);
TopoDS_Shape cutPlateHole(const TopoDS_Shape &shape, const PlateHole &request, std::stop_token stop);
} // namespace reverse::detail
