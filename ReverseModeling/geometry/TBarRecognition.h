/* 文件描述：从地面/台架扫描中识别 T 排底板、直立或倾斜立板及可见顶边。 */
#pragma once
#include "geometry/Geometry.h"
namespace reverse
{
PlateRecognition recognizeTBar(const CloudData &cloud, const BeamEstimate &estimate, const PlateBuildOptions &options,
                               std::stop_token stop = {}, const ProgressCallback &progress = {});
void validateTBarOptions(const PlateBuildOptions &options);
} // namespace reverse
