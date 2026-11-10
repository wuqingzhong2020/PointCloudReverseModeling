/* 文件描述：模型内部快照及共享几何构建函数；仅供 geometry 实现文件使用，不向服务层泄露 OCCT。 */
#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <TopoDS_Compound.hxx>
#include <TopoDS_Shape.hxx>

#include "geometry/Geometry.h"

namespace reverse::detail
{
struct LocalPlateSource;
namespace model
{
struct Plate
{
    std::string id;
    // 参数化板件为局部框架边界；导入/重建对象按相应入口保存既有边界语义。
    Vec3 minimum;
    Vec3 maximum;
    TopoDS_Shape shape;
};
} // namespace model
} // namespace reverse::detail

namespace reverse
{
// 拷贝状态不会深拷贝 OCCT TShape。修改/三角化前必须复制对应拓扑，不能触碰已发布快照。
struct Model::Impl
{
    std::map<std::string, std::shared_ptr<const detail::LocalPlateSource>> cloudSources;
    ModelSummary summary;
    MeshData mesh;
    std::vector<detail::model::Plate> plates;
    TopoDS_Compound compound;
};
} // namespace reverse

namespace reverse::detail::model
{
enum class PartBounds
{
    Preserve,
    World
};

// 重建显示派生数据，不改变板件身份、计数或编辑元数据。
void beginModelPresentation(Model::Impl &model);
// 三角化会写入 OCCT TShape，必须先复制所有部件，包括本次未编辑的部件。
void appendModelPart(Model::Impl &model, std::size_t index, PartBounds boundsPolicy = PartBounds::Preserve);

void checkpoint(std::stop_token stop, const ProgressCallback &progress, double fraction, std::string_view stage);
double checkedVolume(const TopoDS_Shape &shape);
std::array<double, 6> bounds(const TopoDS_Shape &shape);
void appendMesh(const TopoDS_Shape &shape, std::uint32_t partIndex, MeshData &mesh);
TopoDS_Shape recognizedShape(const RecognizedPlate &plate, std::stop_token stop);
std::shared_ptr<const Model> publishModel(std::shared_ptr<Model::Impl> result, std::stop_token stop);
} // namespace reverse::detail::model
