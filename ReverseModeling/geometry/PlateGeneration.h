/* 文件描述：在等厚实体的主面或下支撑面生成独立附加板，校验适用性及有效实体。 */
#pragma once
#include <TopoDS_Shape.hxx>

#include "core/Types.h"

namespace reverse::detail
{
TopoDS_Shape generateAttachedPlate(const TopoDS_Shape &source, const PlateGeneration &request,
                                 const TopoDS_Shape &secondSource = {});
}
