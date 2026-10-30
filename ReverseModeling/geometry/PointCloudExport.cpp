/*
 * 文件描述：
 * 将完整内存点云按毫米项目坐标导出为 binary PCD，保留 XYZ 和可选 intensity。
 * 原生路径流支持中文；暂存文件独立回读核对后才提交，取消或失败不发布半成品。
 */
#include <array>
#include <bit>
#include <cmath>
#include <fstream>
#include <locale>

#include "geometry/Geometry.h"
#include "geometry/StagedFile.h"
namespace reverse
{
CloudExportResult exportPointCloud(const CloudData &cloud, const std::filesystem::path &requestedPath,
                                   std::stop_token stop, const ProgressCallback &progress)
{
    const auto checkpoint = [&](double p, std::string_view message)
    {
        if (stop.stop_requested())
            throw GeometryError("cancelled", "操作已取消");
        if (progress)
            progress(p, message);
    };
    checkpoint(0, "准备导出完整点云");
    if (cloud.points.empty())
        throw GeometryError("empty_cloud", "没有可导出的有效点云");
    if (requestedPath.empty())
        throw GeometryError("invalid_output", "必须指定 PCD 输出路径");
    const auto target = std::filesystem::absolute(requestedPath);
    if (std::filesystem::exists(target))
        throw GeometryError("output_exists", "输出文件已存在，请选择新文件名");
    if (!std::filesystem::is_directory(target.parent_path()))
        throw GeometryError("output_not_writable", "PCD 输出目录不存在");
    detail::StagedFile staged(target.parent_path());
    std::ofstream output(staged.file, std::ios::binary | std::ios::trunc);
    output.imbue(std::locale::classic());
    output << "# .PCD v0.7 - coordinates in millimeters\nVERSION 0.7\nFIELDS x y z"
           << (cloud.hasIntensity ? " intensity\nSIZE 8 8 8 8\nTYPE F F F F\nCOUNT 1 1 1 1\n"
                                  : "\nSIZE 8 8 8\nTYPE F F F\nCOUNT 1 1 1\n")
           << "WIDTH " << cloud.points.size() << "\nHEIGHT 1\nVIEWPOINT 0 0 0 1 0 0 0\nPOINTS " << cloud.points.size()
           << "\nDATA binary\n";
    static_assert(std::endian::native == std::endian::little, "PCD binary output requires little endian");
    for (std::size_t index = 0; index < cloud.points.size(); ++index)
    {
        if (index % 4096 == 0)
            checkpoint(0.6 * index / cloud.points.size(), "写出点云");
        const auto &point = cloud.points[index];
        const std::array values{point.position.x + cloud.originMm.x, point.position.y + cloud.originMm.y,
                                point.position.z + cloud.originMm.z, point.intensity};
        if (!std::isfinite(values[0]) || !std::isfinite(values[1]) || !std::isfinite(values[2]))
            throw GeometryError("invalid_cloud", "点云包含非有限坐标");
        output.write(reinterpret_cast<const char *>(values.data()), (cloud.hasIntensity ? 4 : 3) * sizeof(double));
    }
    output.flush();
    if (!output)
        throw GeometryError("pcd_write_failed", "PCD 文件写出失败");
    output.close();
    if (output.fail())
        throw GeometryError("pcd_write_failed", "PCD 文件关闭失败");
    const auto restored = loadPointCloud(staged.file, LengthUnit::Millimeter, stop,
                                         [&](double p, auto m) { checkpoint(0.6 + 0.3 * p, m); });
    if (restored->points.size() != cloud.points.size() || restored->hasIntensity != cloud.hasIntensity)
        throw GeometryError("pcd_validation_failed", "PCD 回读点数或属性不一致");
    const auto coordinateMatches = [](double a, double b) { return std::abs(a - b) <= 1e-8 + std::abs(a) * 1e-12; };
    for (std::size_t i = 0; i < cloud.points.size(); ++i)
    {
        if (i % 4096 == 0)
            checkpoint(0.9 + 0.09 * i / cloud.points.size(), "核对点云坐标及强度");
        const auto &a = cloud.points[i];
        const auto &b = restored->points[i];
        if (!coordinateMatches(a.position.x + cloud.originMm.x, b.position.x + restored->originMm.x) ||
            !coordinateMatches(a.position.y + cloud.originMm.y, b.position.y + restored->originMm.y) ||
            !coordinateMatches(a.position.z + cloud.originMm.z, b.position.z + restored->originMm.z) ||
            (cloud.hasIntensity && a.intensity != b.intensity && !(std::isnan(a.intensity) && std::isnan(b.intensity))))
            throw GeometryError("pcd_validation_failed", "PCD 回读坐标或强度不一致");
    }
    const CloudExportResult result{cloud.points.size(), std::filesystem::file_size(staged.file), cloud.hasIntensity};
    checkpoint(0.99, "提交 PCD 文件");
    detail::commitWithoutOverwrite(staged.file, target);
    return result; // 提交后不再响应取消，以免已成功落盘却报告失败。
}
} // namespace reverse
