/*
 * 文件描述：在完整点云上进行相机投影框选和保留/删除操作，不依赖显示采样或 Qt。
 */
#pragma once

#include <span>

#include "geometry/Geometry.h"

namespace reverse
{
[[nodiscard]] CloudSelection selectCloudPoints(const CloudData &cloud, const CloudSelectionRect &rectangle,
                                               CloudSelectionMode mode, std::span<const std::size_t> previous = {},
                                               std::stop_token stop = {}, const ProgressCallback &progress = {});

// 索引必须递增且无重复，指向当前 points；输出保留源索引、强度、单位、原点和来源计数。
[[nodiscard]] std::shared_ptr<const CloudData> editCloudPoints(const CloudData &cloud,
                                                               std::span<const std::size_t> selection,
                                                               CloudEditMode mode, std::stop_token stop = {},
                                                               const ProgressCallback &progress = {});
} // namespace reverse
