/* 文件描述：选点建模请求的命令录制与回放契约。 */
#pragma once
#include "commands/CommandBindings.h"
#include "geometry/PointModeling.h"
namespace reverse
{
nlohmann::json pointModelingJson(const PointModeling &r);
PointModeling pointModelingRequest(const nlohmann::json &j);
}
