/*
 * 文件描述：
 * 使用 PCL 读取已有 PCD 文件并保留源点索引，在 double 局部坐标中计算 PCA 框架与包围尺寸初值。
 * 文件流使用 filesystem 路径以支持 Windows 中文路径；PCA 初值不表示自动识别板厚或肋板。
 */
#include "geometry/Geometry.h"

#include <pcl/PCLPointCloud2.h>
#include <pcl/io/pcd_io.h>
#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <unordered_set>

namespace reverse {
namespace {

void checkpoint(std::stop_token stop, const ProgressCallback& progress,
                double fraction, std::string_view stage)
{
    if (stop.stop_requested()) throw GeometryError("cancelled", "操作已取消");
    if (progress) progress(fraction, stage);
}

Vec3 fromEigen(const Eigen::Vector3d& value)
{
    return {value.x(), value.y(), value.z()};
}

Eigen::Vector3d toEigen(const Vec3& value)
{
    return {value.x, value.y, value.z};
}

template<class T> double scalarValue(const std::uint8_t* data)
{
    T value{};
    std::memcpy(&value, data, sizeof(T));
    return static_cast<double>(value);
}

std::size_t scalarSize(std::uint8_t type)
{
    switch (type) {
    case pcl::PCLPointField::INT8: case pcl::PCLPointField::UINT8: return 1;
    case pcl::PCLPointField::INT16: case pcl::PCLPointField::UINT16: return 2;
    case pcl::PCLPointField::INT32: case pcl::PCLPointField::UINT32:
    case pcl::PCLPointField::FLOAT32: return 4;
    case pcl::PCLPointField::FLOAT64: return 8;
    default: throw GeometryError("unsupported_field", "PCD 包含无法读取的标量类型");
    }
}

double readScalar(const std::uint8_t* point, const pcl::PCLPointField& field)
{
    const auto* value = point + field.offset;
    switch (field.datatype) {
    case pcl::PCLPointField::INT8: return scalarValue<std::int8_t>(value);
    case pcl::PCLPointField::UINT8: return scalarValue<std::uint8_t>(value);
    case pcl::PCLPointField::INT16: return scalarValue<std::int16_t>(value);
    case pcl::PCLPointField::UINT16: return scalarValue<std::uint16_t>(value);
    case pcl::PCLPointField::INT32: return scalarValue<std::int32_t>(value);
    case pcl::PCLPointField::UINT32: return scalarValue<std::uint32_t>(value);
    case pcl::PCLPointField::FLOAT32: return scalarValue<float>(value);
    case pcl::PCLPointField::FLOAT64: return scalarValue<double>(value);
    default: throw GeometryError("unsupported_field", "PCD 包含无法读取的标量类型");
    }
}

std::uint64_t headerNumber(const std::string& text)
{
    std::uint64_t result{};
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw GeometryError("invalid_pcd", "PCD 头部整数无效");
    return result;
}

// PCL 会按头部声明分配内存；必须先检查 COUNT/SIZE、乘法溢出及可验证的文件体长度。
void inspectHeader(std::ifstream& input, std::uint64_t fileBytes)
{
    std::array<char, 65537> line{};
    std::uint64_t points = 0, width = 0, height = 0;
    std::size_t headerBytes = 0;
    bool hasData = false;
    std::vector<std::string> fields, sizes, types, counts;
    std::unordered_set<std::string> seen;
    std::string encoding;
    while (input.getline(line.data(), static_cast<std::streamsize>(line.size()))) {
        headerBytes += static_cast<std::size_t>(input.gcount());
        if (headerBytes > 65536) throw GeometryError("invalid_pcd", "PCD 头部过大");
        std::istringstream values(line.data());
        std::string key;
        values >> key;
        if (key.empty() || key.front() == '#') continue;
        if (key == "COLUMNS") key = "FIELDS";
        if (key != "FIELDS" && key != "SIZE" && key != "TYPE" && key != "COUNT" &&
            key != "POINTS" && key != "WIDTH" && key != "HEIGHT" && key != "DATA") continue;
        if (!seen.insert(key).second) throw GeometryError("invalid_pcd", "PCD 头部字段重复");
        std::vector<std::string> tokens;
        std::string token;
        while (values >> token) tokens.push_back(token);
        if (key == "FIELDS") fields = std::move(tokens);
        else if (key == "SIZE") sizes = std::move(tokens);
        else if (key == "TYPE") types = std::move(tokens);
        else if (key == "COUNT") counts = std::move(tokens);
        else {
            if (tokens.size() != 1) throw GeometryError("invalid_pcd", "PCD 标量头部字段缺失或包含多余值");
            if (key == "POINTS") points = headerNumber(tokens.front());
            if (key == "WIDTH") width = headerNumber(tokens.front());
            if (key == "HEIGHT") height = headerNumber(tokens.front());
            if (key == "DATA") { encoding = tokens.front(); hasData = true; break; }
        }
    }
    constexpr std::uint64_t maximumPoints = 100000000;
    if (!hasData || width == 0 || height == 0 || width > maximumPoints ||
        height > maximumPoints || width > maximumPoints / height ||
        points != width * height)
        throw GeometryError("invalid_pcd", "PCD 点数/宽高不一致、为空或超过当前 1 亿点限额");
    if (fields.empty() || fields.size() > 256 || sizes.size() != fields.size() || types.size() != fields.size())
        throw GeometryError("invalid_pcd", "PCD FIELDS/SIZE/TYPE 长度不一致或字段过多");
    if (counts.empty() && !seen.contains("COUNT")) counts.assign(fields.size(), "1");
    if (counts.size() != fields.size()) throw GeometryError("invalid_pcd", "PCD COUNT 长度与字段数不一致");
    std::unordered_set<std::string> names;
    std::uint64_t stride = 0;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (fields[index] != "_" && !names.insert(fields[index]).second)
            throw GeometryError("invalid_pcd", "PCD 存在重复的点属性名称");
        const auto size = headerNumber(sizes[index]);
        const auto count = headerNumber(counts[index]);
        if ((size != 1 && size != 2 && size != 4 && size != 8) || count == 0 || count > 4096 ||
            (types[index] != "I" && types[index] != "U" && types[index] != "F") ||
            (types[index] == "F" && size != 4 && size != 8))
            throw GeometryError("invalid_pcd", "PCD 属性类型、SIZE 或 COUNT 无效或超出限额");
        stride += size * count;
        if (stride > 65536) throw GeometryError("resource_limit", "PCD 单点记录超过当前 64 KiB 限额");
    }
    constexpr std::uint64_t maximumDecodedBytes = 2ULL * 1024 * 1024 * 1024;
    if (points > maximumDecodedBytes / stride)
        throw GeometryError("resource_limit", "PCD 解码数据超过当前 2 GiB 限额");
    const std::uint64_t decodedBytes = points * stride;
    if (headerBytes > fileBytes) throw GeometryError("invalid_pcd", "PCD 头部超出文件大小");
    const std::uint64_t payloadBytes = fileBytes - headerBytes;
    if (encoding == "binary") {
        if (payloadBytes != decodedBytes)
            throw GeometryError("invalid_pcd", "PCD binary 数据长度与声明不一致");
    } else if (encoding == "binary_compressed") {
        std::uint32_t compressed{}, uncompressed{};
        if (payloadBytes < 8 || !input.read(reinterpret_cast<char*>(&compressed), 4) ||
            !input.read(reinterpret_cast<char*>(&uncompressed), 4) ||
            compressed != payloadBytes - 8 || uncompressed != decodedBytes)
            throw GeometryError("invalid_pcd", "PCD 压缩数据长度与声明不一致");
        if (compressed > maximumDecodedBytes)
            throw GeometryError("resource_limit", "PCD 压缩数据超过当前 2 GiB 限额");
    } else if (encoding == "ascii") {
        // 每点至少要有一个字符；截断/畸形的巨大声明不进入 PCL 分配路径。
        if (payloadBytes < points) throw GeometryError("invalid_pcd", "PCD ASCII 数据不足以容纳声明的点数");
    } else {
        throw GeometryError("unsupported_pcd", "不支持的 PCD DATA 编码");
    }
    input.clear();
    input.seekg(0);
}

const pcl::PCLPointField* findField(const pcl::PCLPointCloud2& data, std::string_view name)
{
    const auto found = std::find_if(data.fields.begin(), data.fields.end(),
        [name](const auto& field) { return field.name == name; });
    return found == data.fields.end() ? nullptr : &*found;
}

} // namespace

std::shared_ptr<const CloudData> loadPointCloud(const std::filesystem::path& path,
    LengthUnit unit, std::stop_token stop, const ProgressCallback& progress)
{
    checkpoint(stop, progress, 0.0, "检查 PCD 文件");
    if constexpr (std::endian::native != std::endian::little)
        throw GeometryError("unsupported_endianness", "当前 PCD 解码仅支持小端主机");
    std::error_code ec;
    const auto fileBytes = std::filesystem::file_size(path, ec);
    if (ec) throw GeometryError("input_not_readable", "无法读取输入点云文件");
    const auto modified = std::filesystem::last_write_time(path, ec);
    if (ec || fileBytes == 0) throw GeometryError("input_not_readable", "点云为空或无法读取元数据");
    std::ifstream input(path, std::ios::binary);
    if (!input) throw GeometryError("input_not_readable", "无法打开点云文件");
    inspectHeader(input, fileBytes);

    pcl::PCDReader reader;
    pcl::PCLPointCloud2 data;
    Eigen::Vector4f sensorOrigin;
    Eigen::Quaternionf sensorOrientation;
    int version{}, encoding{};
    unsigned int dataOffset{};
    try {
        if (reader.readHeader(input, data, sensorOrigin, sensorOrientation,
                              version, encoding, dataOffset) < 0)
            throw GeometryError("invalid_pcd", "PCL 无法解析 PCD 头部");
    } catch (const std::bad_alloc&) {
        throw GeometryError("resource_limit", "PCD 声明的数据大小超过可用内存");
    }
    if (data.point_step == 0 || data.data.empty() || dataOffset > fileBytes)
        throw GeometryError("invalid_pcd", "PCD 字段或数据偏移无效");
    const std::uint64_t count = static_cast<std::uint64_t>(data.width) * data.height;
    if (data.data.size() != count * data.point_step)
        throw GeometryError("invalid_pcd", "PCD 数据布局与点数不一致");
    const auto* x = findField(data, "x");
    const auto* y = findField(data, "y");
    const auto* z = findField(data, "z");
    const auto* intensity = findField(data, "intensity");
    if (!x || !y || !z) throw GeometryError("missing_xyz", "PCD 必须包含 x、y、z 字段");
    for (const auto* field : {x, y, z, intensity}) {
        if (!field) continue;
        if (field->count != 1 || field->offset + scalarSize(field->datatype) > data.point_step)
            throw GeometryError("invalid_pcd", "坐标/强度字段必须是有效的单值字段");
    }
    input.clear();
    input.seekg(dataOffset);
    checkpoint(stop, progress, 0.1, "读取 PCD 点数据");
    if (encoding == 0) {
        if (reader.readBodyASCII(input, data, version) < 0)
            throw GeometryError("invalid_pcd", "PCD ASCII 数据不完整或格式错误");
    } else if (encoding == 1) {
        if (fileBytes - dataOffset != data.data.size())
            throw GeometryError("invalid_pcd", "PCD binary 数据长度与头部不一致");
        constexpr std::size_t blockSize = 1024 * 1024;
        for (std::size_t offset = 0; offset < data.data.size(); offset += blockSize) {
            const auto bytes = std::min(blockSize, data.data.size() - offset);
            if (!input.read(reinterpret_cast<char*>(data.data.data() + offset),
                            static_cast<std::streamsize>(bytes)))
                throw GeometryError("invalid_pcd", "PCD binary 数据被截断");
            checkpoint(stop, progress, 0.1 + 0.3 * offset / data.data.size(), "读取 PCD 点数据");
        }
    } else if (encoding == 2) {
        const auto payloadSize = fileBytes - dataOffset;
        if (payloadSize < 8 || payloadSize > std::numeric_limits<std::uint32_t>::max())
            throw GeometryError("invalid_pcd", "PCD 压缩数据大小无效");
        std::vector<unsigned char> payload(static_cast<std::size_t>(payloadSize));
        if (!input.read(reinterpret_cast<char*>(payload.data()), static_cast<std::streamsize>(payload.size())))
            throw GeometryError("invalid_pcd", "PCD 压缩数据被截断");
        std::uint32_t compressed{}, uncompressed{};
        std::memcpy(&compressed, payload.data(), 4);
        std::memcpy(&uncompressed, payload.data() + 4, 4);
        if (compressed != payload.size() - 8 || uncompressed != data.data.size() ||
            reader.readBodyBinary(payload.data(), data, version, true, 0) < 0)
            throw GeometryError("invalid_pcd", "PCD 压缩数据长度或解码结果无效");
    } else {
        throw GeometryError("unsupported_pcd", "不支持的 PCD DATA 编码");
    }

    auto cloud = std::make_shared<CloudData>();
    switch (unit) {
    case LengthUnit::Millimeter: cloud->sourceScaleToMm = 1.0; break;
    case LengthUnit::Centimeter: cloud->sourceScaleToMm = 10.0; break;
    case LengthUnit::Meter: cloud->sourceScaleToMm = 1000.0; break;
    default: throw GeometryError("invalid_unit", "必须明确指定 mm、cm 或 m 长度单位");
    }
    cloud->sourceUnit = unit;
    cloud->sourcePointCount = count;
    cloud->hasIntensity = intensity != nullptr;
    for (const auto& field : data.fields) cloud->fields.push_back(field.name);
    cloud->points.reserve(static_cast<std::size_t>(count));
    Eigen::Vector3d minimum = Eigen::Vector3d::Constant(std::numeric_limits<double>::infinity());
    Eigen::Vector3d maximum = -minimum;
    Eigen::Vector3d origin = Eigen::Vector3d::Zero();
    for (std::uint64_t index = 0; index < count; ++index) {
        if ((index % 16384) == 0)
            checkpoint(stop, progress, 0.4 + 0.59 * static_cast<double>(index) / count, "转换局部毫米坐标");
        const auto* point = data.data.data() + index * data.point_step;
        Eigen::Vector3d position(readScalar(point, *x), readScalar(point, *y), readScalar(point, *z));
        position *= cloud->sourceScaleToMm;
        if (!position.allFinite()) { ++cloud->invalidPointCount; continue; }
        if (cloud->points.empty()) origin = position;
        position -= origin;
        if (!position.allFinite()) throw GeometryError("coordinate_overflow", "坐标换算超出 double 范围");
        minimum = minimum.cwiseMin(position);
        maximum = maximum.cwiseMax(position);
        const auto strength = intensity ? readScalar(point, *intensity) : 0.0;
        cloud->points.push_back({fromEigen(position), strength, index});
    }
    if (cloud->points.empty()) throw GeometryError("empty_cloud", "PCD 不包含有效 XYZ 点");
    cloud->originMm = fromEigen(origin);
    cloud->boundsMinMm = fromEigen(minimum);
    cloud->boundsMaxMm = fromEigen(maximum);
    const auto finalBytes = std::filesystem::file_size(path, ec);
    if (ec || finalBytes != fileBytes || std::filesystem::last_write_time(path, ec) != modified || ec)
        throw GeometryError("input_changed", "导入期间源文件发生变化，请待文件写入完成后重试");
    checkpoint(stop, progress, 1.0, "点云导入完成");
    return cloud;
}

BeamEstimate estimateBeam(const CloudData& cloud, std::stop_token stop,
                         const ProgressCallback& progress)
{
    if (cloud.points.size() < 3) throw GeometryError("insufficient_points", "至少需要三个有效点计算初始框架");
    checkpoint(stop, progress, 0.0, "估计点云主方向");
    Eigen::Vector3d mean = Eigen::Vector3d::Zero();
    Eigen::Matrix3d scatter = Eigen::Matrix3d::Zero();
    // Welford 更新避免直接累加巨大二阶矩；输入已经减去局部原点。
    for (std::size_t index = 0; index < cloud.points.size(); ++index) {
        if (index % 16384 == 0)
            checkpoint(stop, progress, 0.5 * index / cloud.points.size(), "估计点云主方向");
        const auto value = toEigen(cloud.points[index].position);
        const Eigen::Vector3d delta = value - mean;
        mean += delta / static_cast<double>(index + 1);
        scatter += delta * (value - mean).transpose();
    }
    Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> solver(scatter / static_cast<double>(cloud.points.size()));
    if (solver.info() != Eigen::Success || !solver.eigenvalues().allFinite() ||
        solver.eigenvalues()[1] <= 1e-12)
        throw GeometryError("degenerate_cloud", "点云退化，无法建立稳定的梁截面框架");
    Eigen::Vector3d xAxis = solver.eigenvectors().col(2);
    Eigen::Vector3d yAxis = solver.eigenvectors().col(1);
    // 固定符号，使相同输入的框架和起点具有可重放的约定。
    Eigen::Index primary;
    xAxis.cwiseAbs().maxCoeff(&primary);
    if (xAxis[primary] < 0) xAxis = -xAxis;
    yAxis.cwiseAbs().maxCoeff(&primary);
    if (yAxis[primary] < 0) yAxis = -yAxis;
    const Eigen::Vector3d zAxis = xAxis.cross(yAxis).normalized();
    yAxis = zAxis.cross(xAxis).normalized();
    Eigen::Matrix3d axes;
    axes << xAxis, yAxis, zAxis;
    Eigen::Vector3d minimum = Eigen::Vector3d::Constant(std::numeric_limits<double>::infinity());
    Eigen::Vector3d maximum = -minimum;
    for (std::size_t index = 0; index < cloud.points.size(); ++index) {
        if (index % 16384 == 0)
            checkpoint(stop, progress, 0.5 + 0.49 * index / cloud.points.size(), "计算主方向包围尺寸");
        const Eigen::Vector3d projected = axes.transpose() * (toEigen(cloud.points[index].position) - mean);
        minimum = minimum.cwiseMin(projected);
        maximum = maximum.cwiseMax(projected);
    }
    const Eigen::Vector3d dimensions = maximum - minimum;
    if (!dimensions.allFinite() || dimensions.minCoeff() <= 1e-6)
        throw GeometryError("degenerate_cloud", "点云没有可用的三维包围尺寸");
    BeamEstimate estimate;
    estimate.frame = {fromEigen(toEigen(cloud.originMm) + mean + axes * ((minimum + maximum) * 0.5)),
                      fromEigen(xAxis), fromEigen(yAxis), fromEigen(zAxis)};
    estimate.length = dimensions.x();
    estimate.width = dimensions.y();
    estimate.height = dimensions.z();
    estimate.warnings.emplace_back("PCA 仅提供方向和包围尺寸初值；离群点与遮挡会影响结果，请确认方向和尺寸。");
    estimate.warnings.emplace_back("PCA 本步骤只估计方向；逐板厚度与站位见后续识别结果。孔、缺口或任意轮廓尚未恢复。");
    if (solver.eigenvalues()[2] < 1.2 * solver.eigenvalues()[1])
        estimate.warnings.emplace_back("最大两个主方向接近，梁长方向可能不稳定，须人工检查。");
    checkpoint(stop, progress, 1.0, "框架初值已计算，等待人工确认");
    return estimate;
}

} // namespace reverse
