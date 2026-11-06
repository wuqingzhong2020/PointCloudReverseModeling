/* 文件描述：模型交换、表面采样与独立文件转换；不修改当前工程。 */
#pragma once
#include <TopoDS_Shape.hxx>
#include "geometry/Geometry.h"

namespace reverse
{
[[nodiscard]] StepExportResult exportCwrm(const Model &model, const std::filesystem::path &path,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
// spacing 是面积采样密度（mm），使用显示网格，曲面受其离散精度限制；最多五百万点。
[[nodiscard]] std::shared_ptr<const CloudData> sampleModelSurface(const Model &model, double spacingMm,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
struct FileConversionResult
{
    std::string inputFormat;
    std::string outputFormat;
    std::uint64_t pointCount{};
    std::uint64_t fileBytes{};
};
// 输入仅限 STEP/STP、CWRM；明确禁止以 PCD 为输入（包括 PCD -> PCD）。
[[nodiscard]] FileConversionResult convertModelFile(const std::filesystem::path &input,
    const std::filesystem::path &output, double spacingMm = 2.0,
    std::stop_token stop = {}, const ProgressCallback &progress = {});
namespace detail
{
StepExportResult writeCwrmShape(const TopoDS_Shape &shape, const ModelSummary &summary,
    const std::filesystem::path &path, std::stop_token stop, const ProgressCallback &progress);
}
}
