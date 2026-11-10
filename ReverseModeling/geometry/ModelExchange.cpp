/*
 * 文件描述：按 CWRM v2 的 BRep/JSON/AES/Base64 布局写出模型，回读核对后原子发布；
 * CAD 表面按三角面积确定性采样为 PCD。独立文件转换不读取或替换工作台工程。
 */
#include <algorithm>
#include <cmath>
#include <cctype>
#include <fstream>
#include <locale>
#include <sstream>

#include <BRepBndLib.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <Bnd_Box.hxx>
#include <TopTools_FormatVersion.hxx>
#include <openssl/evp.h>

#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/StagedFile.h"

namespace reverse
{
namespace
{
void checkpoint(std::stop_token stop, const ProgressCallback &progress, double value, std::string_view message)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "操作已取消");
    if (progress) progress(value, message);
}
std::string extension(const std::filesystem::path &path)
{
    auto value = path.extension().string();
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
    return value;
}
std::array<double, 6> bounds(const TopoDS_Shape &shape)
{
    Bnd_Box box;
    BRepBndLib::AddOptimal(shape, box, false, false);
    std::array<double, 6> result;
    box.Get(result[0], result[1], result[2], result[3], result[4], result[5]);
    return result;
}
std::string encodeCwrm(std::string plain)
{
    plain.push_back('\0');
    plain.resize((plain.size() + 15) / 16 * 16, '\0');
    // 与已有 CWRM v2 解码器一致的文件格式常量，不是用户凭据。
    constexpr unsigned char key[] = "5ec6-868b-48f2-b";
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key, nullptr) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1)
        throw GeometryError("cwrm_encode_failed", "无法初始化 CWRM 编码器");
    std::string cipher(plain.size() + 16, '\0');
    int count{}, tail{};
    if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(cipher.data()), &count,
        reinterpret_cast<const unsigned char *>(plain.data()), static_cast<int>(plain.size())) != 1 ||
        EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(cipher.data()) + count, &tail) != 1)
        throw GeometryError("cwrm_encode_failed", "CWRM 编码失败");
    cipher.resize(count + tail);
    std::string encoded((cipher.size() + 2) / 3 * 4 + 1, '\0');
    const int size = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()),
        reinterpret_cast<const unsigned char *>(cipher.data()), static_cast<int>(cipher.size()));
    if (size <= 0) throw GeometryError("cwrm_encode_failed", "CWRM Base64 编码失败");
    encoded.resize(size);
    return encoded;
}
}

StepExportResult detail::writeCwrmShape(const TopoDS_Shape &shape, const ModelSummary &summary,
    const std::filesystem::path &path, std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "准备 CWRM v2 模型导出");
    if (path.empty()) throw GeometryError("invalid_output", "必须指定 CWRM 输出路径");
    const auto target = std::filesystem::absolute(path);
    if (std::filesystem::exists(target)) throw GeometryError("output_exists", "输出文件已存在，请选择新文件名");
    if (!std::filesystem::is_directory(target.parent_path()))
        throw GeometryError("output_not_writable", "CWRM 输出目录不存在");
    std::ostringstream body;
    body.imbue(std::locale::classic());
    body << R"(BREP_HEADER_START{"file_version":2,"shape_type":"brep","is_multi_workpiece":false}BREP_HEADER_END)";
    // 不写显示三角网格，使用旧版 OCCT 也能读取的文本拓扑格式。
    BRepTools::Write(shape, body, false, false, TopTools_FormatVersion_VERSION_1);
    if (!body) throw GeometryError("cwrm_write_failed", "CWRM BRep 序列化失败");
    const auto plain = body.str();
    if (plain.size() > 380ULL * 1024 * 1024)
        throw GeometryError("cwrm_too_large", "模型超过 CWRM 512 MiB 编码文件限制");
    checkpoint(stop, progress, 0.3, "编码 CWRM 模型");
    const auto encoded = encodeCwrm(plain);
    detail::StagedFile staged(target.parent_path());
    staged.file = staged.directory / "verified.cwrm";
    {
        std::ofstream output(staged.file, std::ios::binary);
        output.write(encoded.data(), static_cast<std::streamsize>(encoded.size()));
        output.close();
        if (!output) throw GeometryError("cwrm_write_failed", "CWRM 文件写出失败");
    }
    checkpoint(stop, progress, 0.5, "回读 CWRM 并核对几何");
    const auto restored = loadCadModel(staged.file, stop,
        [&](double p, auto m) { checkpoint(stop, progress, 0.5 + p * 0.4, m); });
    const auto &actual = restored->summary();
    if (actual.solidCount != summary.solidCount || actual.surfaceCount != summary.surfaceCount ||
        std::abs(actual.volumeMm3 - summary.volumeMm3) > std::max(1e-4, summary.volumeMm3 * 1e-8))
        throw GeometryError("cwrm_validation_failed", "CWRM 回读实体数量或体积不一致");
    std::istringstream input(detail::decodeCwrm(encoded));
    TopoDS_Shape restoredShape;
    BRep_Builder builder;
    BRepTools::Read(restoredShape, input, builder);
    const auto before = bounds(shape), after = bounds(restoredShape);
    for (std::size_t i = 0; i < before.size(); ++i)
        if (!std::isfinite(after[i]) || std::abs(before[i] - after[i]) > 1e-4)
            throw GeometryError("cwrm_validation_failed", "CWRM 回读位置或包围尺寸不一致");
    StepExportResult result{actual.solidCount, actual.volumeMm3, std::filesystem::file_size(staged.file), true,
        actual.surfaceCount};
    checkpoint(stop, progress, 0.99, "提交已验证的 CWRM 文件");
    detail::commitWithoutOverwrite(staged.file, target, stop);
    return result;
}

std::shared_ptr<const CloudData> sampleModelSurface(const Model &model, double spacingMm,
    std::stop_token stop, const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0, "按模型表面采样点云（非原始扫描数据）");
    if (!std::isfinite(spacingMm) || spacingMm < 0.01 || spacingMm > 100000)
        throw GeometryError("invalid_spacing", "采样间距必须为 0.01 至 100000 mm");
    const auto &mesh = model.mesh();
    constexpr std::size_t limit = 5000000;
    std::vector<std::size_t> counts;
    std::size_t total{};
    for (const auto &triangle : mesh.triangles)
    {
        if (counts.size() % 4096 == 0) checkpoint(stop, progress, 0.05, "计算表面采样数量");
        const auto a = mesh.vertices.at(triangle[0]), b = mesh.vertices.at(triangle[1]), c = mesh.vertices.at(triangle[2]);
        const Vec3 u{b.x-a.x,b.y-a.y,b.z-a.z}, v{c.x-a.x,c.y-a.y,c.z-a.z};
        const Vec3 cross{u.y*v.z-u.z*v.y,u.z*v.x-u.x*v.z,u.x*v.y-u.y*v.x};
        const double area = 0.5 * std::hypot(cross.x, cross.y, cross.z);
        const double count = area > 1e-15 ? std::max(1.0, std::ceil(area / (spacingMm * spacingMm))) + 3 : 0;
        if (!std::isfinite(count) || count > limit - total)
            throw GeometryError("sampling_limit", "采样点数超过五百万，请增大 --spacing 采样间距");
        counts.push_back(static_cast<std::size_t>(count));
        total += counts.back();
    }
    if (!total) throw GeometryError("empty_model", "模型没有可采样的表面");
    auto cloud = std::make_shared<CloudData>();
    cloud->points.reserve(total);
    cloud->fields = {"x", "y", "z"};
    cloud->sourcePointCount = total;
    Vec3 low = mesh.vertices.front(), high = low;
    for (const auto &v : mesh.vertices)
    {
        low = {std::min(low.x,v.x), std::min(low.y,v.y), std::min(low.z,v.z)};
        high = {std::max(high.x,v.x), std::max(high.y,v.y), std::max(high.z,v.z)};
    }
    cloud->originMm = {(low.x+high.x)/2,(low.y+high.y)/2,(low.z+high.z)/2};
    const auto &o = cloud->originMm;
    cloud->boundsMinMm = {low.x-o.x,low.y-o.y,low.z-o.z};
    cloud->boundsMaxMm = {high.x-o.x,high.y-o.y,high.z-o.z};
    const auto append = [&](Vec3 p) { cloud->points.push_back({{p.x-o.x,p.y-o.y,p.z-o.z}, 0, cloud->points.size()}); };
    for (std::size_t t = 0; t < counts.size(); ++t)
    {
        if (!counts[t]) continue;
        const auto &indices = mesh.triangles[t];
        const auto a=mesh.vertices[indices[0]], b=mesh.vertices[indices[1]], c=mesh.vertices[indices[2]];
        append(a); append(b); append(c);
        const auto n = counts[t]-3;
        for (std::size_t i=0; i<n; ++i)
        {
            if (i%4096==0) checkpoint(stop,progress,0.1+0.9*cloud->points.size()/total,"采样模型表面");
            // Hammersley 序列均匀覆盖三角形；固定顺序使相同模型/间距产生相同结果。
            double v{}, factor=0.5;
            for (auto bits=i; bits; bits>>=1, factor*=0.5) v += (bits&1)*factor;
            const double u=std::sqrt((i+0.5)/n), w0=1-u, w1=u*(1-v), w2=u*v;
            append({w0*a.x+w1*b.x+w2*c.x,w0*a.y+w1*b.y+w2*c.y,w0*a.z+w1*b.z+w2*c.z});
        }
    }
    return cloud;
}

FileConversionResult convertModelFile(const std::filesystem::path &input, const std::filesystem::path &output,
    double spacingMm, std::stop_token stop, const ProgressCallback &progress)
{
    const auto from = extension(input), to = extension(output);
    if (from == ".pcd")
        throw GeometryError("pcd_conversion_disabled", "转换命令禁止以 PCD 为输入，包括 PCD 转 PCD；请使用正常的点云导入、导出功能");
    if (from != ".step" && from != ".stp" && from != ".cwrm")
        throw GeometryError("unsupported_format", "转换输入仅支持 STEP/STP、CWRM 模型");
    if (to != ".step" && to != ".stp" && to != ".cwrm" && to != ".pcd")
        throw GeometryError("unsupported_format", "转换输出必须为 .step/.stp、.cwrm 或 .pcd");
    if (std::filesystem::exists(output)) throw GeometryError("output_exists", "输出文件已存在，请选择新文件名");
    if (!std::filesystem::is_directory(std::filesystem::absolute(output).parent_path()))
        throw GeometryError("output_not_writable", "输出目录不存在");
    const auto model = loadCadModel(input, stop,
        [&](double p, auto m) { checkpoint(stop, progress, p*0.4, m); });
    const auto reporting = [&](double p, auto m) { checkpoint(stop, progress, 0.4+p*0.6, m); };
    FileConversionResult result{from.substr(1),to.substr(1)};
    if (to == ".pcd")
    {
        const auto cloud = sampleModelSurface(*model, spacingMm, stop,
            [&](double p, auto m) { reporting(p*0.5,m); });
        const auto saved = exportPointCloud(*cloud, output, stop,
            [&](double p, auto m) { reporting(0.5+p*0.5,m); });
        result.pointCount=saved.pointCount; result.fileBytes=saved.fileBytes;
    }
    else result.fileBytes = (to == ".cwrm" ? exportCwrm(*model, output, stop, reporting)
                                            : exportStep(*model, output, stop, reporting)).fileBytes;
    return result;
}
}
