/*
 * 文件描述：
 * 导入模块内部接口：解码 CWRM、将 OCCT 形状转换为中立模型及显示网格。
 */
#pragma once
#include <string>
#include <string_view>

#include <TopoDS_Shape.hxx>

#include "geometry/Geometry.h"
namespace reverse::detail
{
std::string decodeCwrm(std::string_view encoded);
std::shared_ptr<const Model> modelFromShape(const TopoDS_Shape &shape, const std::string &format, std::stop_token stop,
                                            const ProgressCallback &progress);
} // namespace reverse::detail
