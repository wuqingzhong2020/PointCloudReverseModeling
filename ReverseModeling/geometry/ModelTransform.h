/* 文件描述：定义部件刚体移动/定轴旋转及坐标换算，供几何、命令和实时预览共用。 */
#pragma once
#include "core/Types.h"

namespace reverse
{
enum class ModelTransformKind
{
    Move,
    Rotate
};
enum class RotationAxis
{
    X,
    Y,
    Z
};
struct ModelTransform
{
    ModelTransformKind kind{ModelTransformKind::Move};
    Vec3 offsetMm;
    RotationAxis axis{RotationAxis::Z};
    double angleDegrees{}; // 右手规则；单次操作限定 [-180, 180]。
    Vec3 centerMm;         // 项目坐标；多部件使用同一个旋转中心。
};

[[nodiscard]] BeamFrame modelTransformFrame(const ModelTransform &request);
[[nodiscard]] bool isIdentityTransform(const ModelTransform &request);
[[nodiscard]] Vec3 transformPoint(const BeamFrame &transform, Vec3 point);
[[nodiscard]] Vec3 transformVector(const BeamFrame &transform, Vec3 vector);
[[nodiscard]] Vec3 inverseTransformPoint(const BeamFrame &transform, Vec3 point);
[[nodiscard]] BeamFrame composeTransforms(const BeamFrame &outer, const BeamFrame &inner);
[[nodiscard]] BeamFrame modelPartFrame(const ModelSummary &model, const std::string &partId);
} // namespace reverse
