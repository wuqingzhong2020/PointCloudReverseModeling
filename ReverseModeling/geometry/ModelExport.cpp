/* 文件描述：创建导入模型快照，写出 CWRM/STEP，独立回读检查后提交输出文件。 */

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include <BRepBuilderAPI_Copy.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRep_Builder.hxx>
#include <DESTEP_Parameters.hxx>
#include <Quantity_Color.hxx>
#include <STEPCAFControl_Reader.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <Standard_Failure.hxx>
#include <TDataStd_Name.hxx>
#include <TDocStd_Document.hxx>
#include <TopExp_Explorer.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ColorTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>

#include "geometry/ModelExchange.h"
#include "geometry/ModelImport.h"
#include "geometry/ModelState.h"
#include "geometry/StagedFile.h"

namespace reverse
{
using detail::model::appendMesh;
using detail::model::bounds;
using detail::model::checkedVolume;
using detail::model::checkpoint;
using detail::model::publishModel;

std::shared_ptr<const Model> detail::modelFromShape(const TopoDS_Shape &shape, const std::string &format,
                                                    std::stop_token stop, const ProgressCallback &progress)
{
    if (shape.IsNull() || !BRepCheck_Analyzer(shape).IsValid())
        throw GeometryError("invalid_model", "导入模型未通过几何有效性检查");
    auto result = std::make_shared<Model::Impl>();
    result->summary.importedFormat = format;
    BRep_Builder builder;
    builder.MakeCompound(result->compound);
    std::vector<TopoDS_Shape> parts;
    for (TopExp_Explorer it(shape, TopAbs_SOLID); it.More(); it.Next())
        parts.push_back(it.Current());
    for (TopExp_Explorer it(shape, TopAbs_FACE, TopAbs_SOLID); it.More(); it.Next())
        parts.push_back(it.Current());
    if (parts.empty())
        throw GeometryError("empty_model", "模型不包含可显示的实体或曲面");
    for (std::size_t index = 0; index < parts.size(); ++index)
    {
        checkpoint(stop, progress, static_cast<double>(index) / parts.size(), "生成导入部件网格");
        const auto &part = parts[index];
        const auto box = bounds(part);
        if (!std::all_of(box.begin(), box.end(), [](double v) { return std::isfinite(v); }))
            throw GeometryError("invalid_model", "模型包含非有限坐标");
        const auto id = "imported-part-" + std::to_string(index + 1);
        result->plates.push_back({id, {box[0], box[1], box[2]}, {box[3], box[4], box[5]}, part});
        result->summary.partIds.push_back(id);
        result->mesh.partIds.push_back(id);
        if (part.ShapeType() == TopAbs_SOLID)
        {
            result->summary.volumeMm3 += checkedVolume(part);
            ++result->summary.solidCount;
        }
        else
            ++result->summary.surfaceCount;
        builder.Add(result->compound, part);
        appendMesh(part, static_cast<std::uint32_t>(index), result->mesh);
    }
    const auto box = bounds(result->compound);
    result->summary.frame.originMm = {(box[0] + box[3]) / 2, (box[1] + box[4]) / 2, (box[2] + box[5]) / 2};
    result->summary.warnings.push_back(
        "导入几何按实体/独立面分组；等厚实体的板厚由平行主面计算，不包含原始点云或重建参数。");
    checkpoint(stop, progress, 1, "模型导入完成");
    return publishModel(std::move(result), stop);
}
StepExportResult exportCwrm(const Model &model, const std::filesystem::path &path, std::stop_token stop,
                            const ProgressCallback &progress)
{
    return detail::writeCwrmShape(model.implementation_->compound, model.summary(), path, stop, progress);
}

StepExportResult exportStep(const Model &model, const std::filesystem::path &requestedPath, std::stop_token stop,
                            const ProgressCallback &progress)
{
    checkpoint(stop, progress, 0.0, "准备 STEP 装配导出");
    if (requestedPath.empty())
        throw GeometryError("invalid_output", "必须指定 STEP 输出路径");
    const auto target = std::filesystem::absolute(requestedPath);
    std::error_code error;
    if (std::filesystem::exists(target, error))
        throw GeometryError("output_exists", "输出文件已存在，请选择新文件名");
    if (error || !std::filesystem::is_directory(target.parent_path()))
        throw GeometryError("output_not_writable", "STEP 输出目录不存在或不可访问");
    detail::StagedFile staged(target.parent_path());
    const auto &source = *model.implementation_;
    StepExportResult result;
    try
    {
        const Handle(TDocStd_Document) document = new TDocStd_Document("BinXCAF");
        const auto shapeTool = XCAFDoc_DocumentTool::ShapeTool(document->Main());
        const auto colorTool = XCAFDoc_DocumentTool::ColorTool(document->Main());
        XCAFDoc_DocumentTool::SetLengthUnit(document, 0.001);
        const auto assembly = shapeTool->NewShape();
        TDataStd_Name::Set(assembly, !source.summary.importedFormat.empty() ? "ImportedModel"
                                     : source.summary.fromPointCloud        ? "RecognizedPlates"
                                                                            : "AssistedBeam");
        for (std::size_t index = 0; index < source.plates.size(); ++index)
        {
            const auto &plate = source.plates[index];
            // 导入装配可能多次引用同一拓扑；独立复制拓扑以保留各部件名称和实例位置。
            const auto exportedShape = source.summary.importedFormat.empty()
                                           ? plate.shape
                                           : BRepBuilderAPI_Copy(plate.shape, false, false).Shape();
            const auto part = shapeTool->AddShape(exportedShape, false);
            TDataStd_Name::Set(part, TCollection_ExtendedString(plate.id.c_str()));
            const auto instance = shapeTool->AddComponent(assembly, part, TopLoc_Location());
            TDataStd_Name::Set(instance, TCollection_ExtendedString(plate.id.c_str()));
            colorTool->SetColor(part, Quantity_Color(index < 3 ? 0.35 : 0.85, 0.65, 0.85, Quantity_TOC_RGB),
                                XCAFDoc_ColorGen);
        }
        shapeTool->UpdateAssemblies();
        STEPCAFControl_Writer writer;
        writer.SetNameMode(true);
        writer.SetColorMode(true);
        DESTEP_Parameters options;
        options.WriteSchema = DESTEP_Parameters::WriteMode_StepSchema_AP214IS;
        options.WriteUnit = UnitsMethods_LengthUnit_Millimeter;
        if (!writer.Transfer(document, options, STEPControl_AsIs))
            throw GeometryError("step_transfer_failed", "XCAF 到 STEP 转换失败");
        checkpoint(stop, progress, 0.35, "写出临时 STEP 文件");
        std::ofstream output(staged.file, std::ios::binary | std::ios::trunc);
        if (!output || writer.WriteStream(output) != IFSelect_RetDone)
            throw GeometryError("step_write_failed", "STEP 文件写出失败");
        output.flush();
        if (!output)
            throw GeometryError("step_write_failed", "STEP 文件刷新失败");
        output.close();
        if (output.fail())
            throw GeometryError("step_write_failed", "STEP 文件关闭失败");
        checkpoint(stop, progress, 0.6, "独立回读 STEP 并检查实体");
        STEPCAFControl_Reader reader;
        reader.SetNameMode(true);
        reader.SetColorMode(true);
        std::ifstream input(staged.file, std::ios::binary);
        if (!input || reader.ReadStream("verified.step", input) != IFSelect_RetDone)
            throw GeometryError("step_readback_failed", "STEP 回读解析失败");
        reader.ChangeReader().SetSystemLengthUnit(1.0);
        const Handle(TDocStd_Document) restoredDocument = new TDocStd_Document("BinXCAF");
        XCAFDoc_DocumentTool::SetLengthUnit(restoredDocument, 0.001);
        if (!reader.Transfer(restoredDocument))
            throw GeometryError("step_readback_failed", "STEP 回读文档转换失败");
        const auto restoredTool = XCAFDoc_DocumentTool::ShapeTool(restoredDocument->Main());
        const auto restoredShape = restoredTool->GetOneShape();
        NCollection_Sequence<TDF_Label> roots;
        restoredTool->GetFreeShapes(roots);
        NCollection_Sequence<TDF_Label> components;
        if (roots.Length() != 1 || !XCAFDoc_ShapeTool::GetComponents(roots.Value(1), components) ||
            components.Length() != static_cast<int>(source.plates.size()))
            throw GeometryError("step_validation_failed", "STEP 回读的根装配或板件实例数量不一致");
        std::vector<bool> namedParts(source.plates.size(), false);
        for (int index = 1; index <= components.Length(); ++index)
        {
            TDF_Label referred;
            if (!XCAFDoc_ShapeTool::GetReferredShape(components.Value(index), referred))
                throw GeometryError("step_validation_failed", "STEP 装配包含无法解析的板件引用");
            Handle(TDataStd_Name) name;
            if (!referred.FindAttribute(TDataStd_Name::GetID(), name))
                throw GeometryError("step_validation_failed", "STEP 回读丢失板件名称");
            bool matched = false;
            for (std::size_t part = 0; part < source.plates.size(); ++part)
            {
                if (name->Get().IsEqual(TCollection_ExtendedString(source.plates[part].id.c_str())))
                {
                    if (namedParts[part])
                        throw GeometryError("step_validation_failed", "STEP 回读板件名称重复");
                    namedParts[part] = true;
                    matched = true;
                    break;
                }
            }
            if (!matched)
                throw GeometryError("step_validation_failed", "STEP 回读板件名称与原模型不一致");
        }
        for (TopExp_Explorer solids(restoredShape, TopAbs_SOLID); solids.More(); solids.Next())
        {
            result.volumeMm3 += checkedVolume(solids.Current());
            ++result.solidCount;
        }
        for (TopExp_Explorer faces(restoredShape, TopAbs_FACE, TopAbs_SOLID); faces.More(); faces.Next())
        {
            ++result.surfaceCount;
        }
        if (result.surfaceCount != source.summary.surfaceCount)
            throw GeometryError("step_validation_failed", "STEP 回读的独立观测面数量不一致");
        if (result.solidCount != source.summary.solidCount ||
            std::abs(result.volumeMm3 - source.summary.volumeMm3) > std::max(1e-4, source.summary.volumeMm3 * 1e-8))
            throw GeometryError("step_validation_failed", "STEP 回读的实体数量或体积与原模型不一致");
        const auto before = bounds(source.compound);
        const auto after = bounds(restoredShape);
        for (std::size_t index = 0; index < before.size(); ++index)
        {
            if (!std::isfinite(after[index]) || std::abs(before[index] - after[index]) > 1e-4)
                throw GeometryError("step_validation_failed", "STEP 回读的位置或包围尺寸与原模型不一致");
        }
        double restoredUnit{};
        if (!XCAFDoc_DocumentTool::GetLengthUnit(restoredDocument, restoredUnit) ||
            std::abs(restoredUnit - 0.001) > 1e-12)
            throw GeometryError("step_validation_failed", "STEP 回读文档单位不是毫米");
        input.close();
        result.fileBytes = std::filesystem::file_size(staged.file);
        checkpoint(stop, progress, 0.95, "提交已验证的 STEP 文件");
        detail::commitWithoutOverwrite(staged.file, target, stop);
        result.valid = true;
    }
    catch (const Standard_Failure &failure)
    {
        throw GeometryError("step_failed", failure.what());
    }
    // 提交后不再处理取消；文件成功落盘必须返回成功状态。
    return result;
}

} // namespace reverse
