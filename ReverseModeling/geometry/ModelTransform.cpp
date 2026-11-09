/* 文件描述：只允许平移或绕项目坐标单轴旋转，保持尺寸、板厚和体积不变。 */
#include <cmath>
#include <numbers>

#include "geometry/Geometry.h"
#include "geometry/ModelTransform.h"

namespace reverse
{
namespace
{
bool finite(Vec3 p)
{
    return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
}
double dot(Vec3 a, Vec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
} // namespace
Vec3 transformVector(const BeamFrame &t, Vec3 p)
{
    return {t.xAxis.x * p.x + t.yAxis.x * p.y + t.zAxis.x * p.z, t.xAxis.y * p.x + t.yAxis.y * p.y + t.zAxis.y * p.z,
            t.xAxis.z * p.x + t.yAxis.z * p.y + t.zAxis.z * p.z};
}
Vec3 transformPoint(const BeamFrame &t, Vec3 p)
{
    const auto r = transformVector(t, p);
    return {r.x + t.originMm.x, r.y + t.originMm.y, r.z + t.originMm.z};
}
Vec3 inverseTransformPoint(const BeamFrame &t, Vec3 p)
{
    const Vec3 d{p.x - t.originMm.x, p.y - t.originMm.y, p.z - t.originMm.z};
    return {dot(d, t.xAxis), dot(d, t.yAxis), dot(d, t.zAxis)};
}
BeamFrame composeTransforms(const BeamFrame &a, const BeamFrame &b)
{
    return {transformPoint(a, b.originMm), transformVector(a, b.xAxis), transformVector(a, b.yAxis),
            transformVector(a, b.zAxis)};
}
BeamFrame modelPartFrame(const ModelSummary &model, const std::string &partId)
{
    const auto initial = model.recognizedPartFrames.find(partId);
    const auto &frame = initial == model.recognizedPartFrames.end() ? model.frame : initial->second;
    const auto found = model.partTransforms.find(partId);
    return found == model.partTransforms.end() ? frame : composeTransforms(found->second, frame);
}
BeamFrame modelTransformFrame(const ModelTransform &request)
{
    BeamFrame result;
    if (request.kind == ModelTransformKind::Move)
    {
        if (!finite(request.offsetMm))
            throw GeometryError("invalid_offset", "参数 --offset 必须为三个有限的毫米数值。");
        result.originMm = request.offsetMm;
        return result;
    }
    if (request.kind != ModelTransformKind::Rotate)
        throw GeometryError("invalid_transform", "未知变换类型。");
    if (!finite(request.centerMm))
        throw GeometryError("invalid_center", "参数 --center 必须为三个有限的项目坐标。");
    if (!std::isfinite(request.angleDegrees) || std::abs(request.angleDegrees) > 180)
        throw GeometryError("invalid_angle", "参数 --angle 必须在 [-180, 180] 度范围内。");
    const auto a = request.angleDegrees * std::numbers::pi / 180;
    const auto c = std::cos(a), s = std::sin(a);
    switch (request.axis)
    {
    case RotationAxis::X:
        result.yAxis = {0, c, s};
        result.zAxis = {0, -s, c};
        break;
    case RotationAxis::Y:
        result.xAxis = {c, 0, -s};
        result.zAxis = {s, 0, c};
        break;
    case RotationAxis::Z:
        result.xAxis = {c, s, 0};
        result.yAxis = {-s, c, 0};
        break;
    default:
        throw GeometryError("invalid_axis", "参数 --axis 只能为 x、y、z。");
    }
    const auto rotated = transformVector(result, request.centerMm);
    result.originMm = {request.centerMm.x - rotated.x, request.centerMm.y - rotated.y, request.centerMm.z - rotated.z};
    return result;
}
bool isIdentityTransform(const ModelTransform &request)
{
    (void)modelTransformFrame(request); // 空操作也要经过完整参数检查。
    return request.kind == ModelTransformKind::Move
               ? request.offsetMm.x == 0 && request.offsetMm.y == 0 && request.offsetMm.z == 0
               : request.angleDegrees == 0;
}
} // namespace reverse
