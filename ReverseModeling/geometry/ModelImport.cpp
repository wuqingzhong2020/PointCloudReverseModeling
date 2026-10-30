/*
 * 文件描述：
 * 使用原生路径流导入 STEP 和 CWRM 模型。CWRM v2 解码为 OCCT 文本 BRep，
 * STEP 按文件单位换算到 mm；保留实例位置，按实体及独立面生成可显示、可再导出的模型。
 */
#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <sstream>

#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <STEPControl_Reader.hxx>
#include <Standard_Failure.hxx>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include "geometry/ModelImport.h"

namespace reverse::detail
{
std::string decodeCwrm(std::string_view encoded)
{
    // CypModeler utilities.dll 的 ShapeWriter<2> 使用固定格式密钥、AES-128-ECB、零填充及 Base64。
    constexpr unsigned char key[] = "5ec6-868b-48f2-b";
    std::string compact;
    compact.reserve(encoded.size());
    for (const unsigned char c : encoded)
    {
        if (c == ' ' || c == '\r' || c == '\n' || c == '\t')
            continue;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/' ||
              c == '='))
            throw GeometryError("invalid_cwrm", "CWRM 不是有效的 Base64 文件");
        compact.push_back(static_cast<char>(c));
    }
    if (compact.empty() || compact.size() % 4 != 0 || compact.size() > 512ULL * 1024 * 1024)
        throw GeometryError("invalid_cwrm", "CWRM 长度无效或超过 512 MiB 解码限制");
    const auto padding = compact.ends_with("==") ? 2 : compact.ends_with("=") ? 1 : 0;
    if (compact.find('=') < compact.size() - padding)
        throw GeometryError("invalid_cwrm", "CWRM Base64 填充无效");
    std::string cipher(compact.size() / 4 * 3, '\0');
    const int decoded =
        EVP_DecodeBlock(reinterpret_cast<unsigned char *>(cipher.data()),
                        reinterpret_cast<const unsigned char *>(compact.data()), static_cast<int>(compact.size()));
    if (decoded <= padding)
        throw GeometryError("invalid_cwrm", "CWRM Base64 解码失败");
    cipher.resize(decoded - padding);
    if (cipher.size() % 16 != 0)
        throw GeometryError("invalid_cwrm", "CWRM 加密数据长度无效");
    std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
    if (!ctx || EVP_DecryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key, nullptr) != 1 ||
        EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1)
        throw GeometryError("cwrm_decode_failed", "无法初始化 CWRM 解码器");
    std::string plain(cipher.size() + 16, '\0');
    int count{}, tail{};
    if (EVP_DecryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(plain.data()), &count,
                          reinterpret_cast<const unsigned char *>(cipher.data()),
                          static_cast<int>(cipher.size())) != 1 ||
        EVP_DecryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(plain.data()) + count, &tail) != 1)
        throw GeometryError("invalid_cwrm", "CWRM 解码失败");
    plain.resize(count + tail);
    const auto nul = plain.find('\0');
    if (nul == std::string::npos || plain.size() - nul > 16 ||
        !std::all_of(plain.begin() + nul, plain.end(), [](char c) { return c == '\0'; }))
        throw GeometryError("invalid_cwrm", "CWRM 文件尾不完整或格式不兼容");
    plain.resize(nul);
    constexpr std::string_view begin = "BREP_HEADER_START", end = "BREP_HEADER_END";
    const auto endAt = plain.find(end, begin.size());
    if (!plain.starts_with(begin) || endAt == std::string::npos || endAt > 65536)
        throw GeometryError("unsupported_cwrm", "不支持此 CWRM 编码或文件已损坏");
    const auto header = nlohmann::json::parse(plain.substr(begin.size(), endAt - begin.size()), nullptr, false);
    if (!header.is_object() || !header.contains("file_version") || header["file_version"] != 2 ||
        !header.contains("shape_type") || header["shape_type"] != "brep")
        throw GeometryError("unsupported_cwrm", "当前支持 CWRM v2 BRep 模型文件");
    const auto body = plain.substr(endAt + end.size());
    if (body.find("CASCADE Topology V") == std::string::npos)
        throw GeometryError("invalid_cwrm", "CWRM 缺少 BRep 模型数据");
    return body;
}
} // namespace reverse::detail

namespace reverse
{
std::shared_ptr<const Model> loadCadModel(const std::filesystem::path &path, std::stop_token stop,
                                          const ProgressCallback &progress)
{
    const auto checkpoint = [&](double p, std::string_view stage)
    {
        if (stop.stop_requested())
            throw GeometryError("cancelled", "操作已取消");
        if (progress)
            progress(p, stage);
    };
    checkpoint(0, "读取模型文件");
    auto extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (extension != ".step" && extension != ".stp" && extension != ".cwrm")
        throw GeometryError("unsupported_format", "支持的模型格式为 STEP/STP、CWRM");
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw GeometryError("input_not_readable", "无法打开模型文件，请检查路径和权限");
    TopoDS_Shape shape;
    try
    {
        if (extension == ".cwrm")
        {
            if (std::filesystem::file_size(path) > 512ULL * 1024 * 1024)
                throw GeometryError("invalid_cwrm", "CWRM 超过 512 MiB 解码限制");
            const std::string encoded{std::istreambuf_iterator<char>(input), {}};
            std::istringstream stream(detail::decodeCwrm(encoded));
            checkpoint(0.2, "解析 CWRM BRep 模型");
            BRep_Builder builder;
            BRepTools::Read(shape, stream, builder);
            if (shape.IsNull() || stream.fail())
                throw GeometryError("invalid_cwrm", "CWRM 中的 BRep 模型不完整");
        }
        else
        {
            STEPControl_Reader reader;
            if (reader.ReadStream("import.step", input) != IFSelect_RetDone)
                throw GeometryError("invalid_step", "STEP 文件解析失败");
            checkpoint(0.2, "转换 STEP 模型到毫米");
            reader.SetSystemLengthUnit(1.0);
            if (reader.TransferRoots() <= 0)
                throw GeometryError("invalid_step", "STEP 文件没有可转换的模型");
            shape = reader.OneShape();
        }
        checkpoint(0.45, "检查模型并生成部件网格");
        return detail::modelFromShape(shape, extension == ".cwrm" ? "cwrm" : "step", stop,
                                      [&](double p, auto m) { checkpoint(0.45 + 0.55 * p, m); });
    }
    catch (const Standard_Failure &failure)
    {
        throw GeometryError("cad_import_failed", std::string("模型转换失败：") + failure.what());
    }
}
} // namespace reverse
