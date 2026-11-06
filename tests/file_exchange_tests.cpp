/*
 * 文件描述：
 * 验证中文路径下 PCD、STEP、CWRM 的读写、模型位置和单位、异常保护与命令状态切换。
 * 合成模型和加密数据在临时目录构造；可选本机样本只读，导出均写入自建临时目录。
 */
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepTools.hxx>
#include <BRep_Builder.hxx>
#include <DESTEP_Parameters.hxx>
#include <STEPCAFControl_Writer.hxx>
#include <TDocStd_Document.hxx>
#include <TopoDS_Compound.hxx>
#include <XCAFDoc_DocumentTool.hxx>
#include <XCAFDoc_ShapeTool.hxx>
#include <gp_Pln.hxx>
#include <gtest/gtest.h>
#include <openssl/evp.h>

#include "commands/CommandService.h"
#include "commands/Json.h"
#include "geometry/CloudEditing.h"
#include "geometry/ModelImport.h"
#include "geometry/ModelExchange.h"
#include "workbench/ModelDisplayData.h"
namespace
{
class FileExchange : public testing::Test
{
  protected:
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("reverse-exchange-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    void SetUp() override
    {
        ASSERT_TRUE(std::filesystem::create_directory(directory));
    }
    void TearDown() override
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
    std::filesystem::path file(const std::u8string &name) const
    {
        return directory / std::filesystem::path(name);
    }
    static reverse::TaskSnapshot finish(reverse::CommandService &service, const std::string &id)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (std::chrono::steady_clock::now() < deadline)
        {
            const auto task = service.task(id);
            if (task.finished())
                return task;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        (void)service.cancel(id);
        throw std::runtime_error("Unit test task timed out");
    }
    static void write(const std::filesystem::path &path, const std::string &data)
    {
        std::ofstream stream(path, std::ios::binary);
        stream << data;
        if (!stream)
            throw std::runtime_error("Fixture write failed");
    }
    static std::string encode(const std::string &body)
    {
        auto plain = body;
        plain.push_back('\0');
        plain.resize((plain.size() + 15) / 16 * 16, '\0');
        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> ctx(EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);
        const unsigned char key[] = "5ec6-868b-48f2-b";
        if (!ctx || EVP_EncryptInit_ex(ctx.get(), EVP_aes_128_ecb(), nullptr, key, nullptr) != 1 ||
            EVP_CIPHER_CTX_set_padding(ctx.get(), 0) != 1)
            throw std::runtime_error("Fixture encryption failed");
        std::string cipher(plain.size() + 16, '\0');
        int length{}, tail{};
        if (EVP_EncryptUpdate(ctx.get(), reinterpret_cast<unsigned char *>(cipher.data()), &length,
                              reinterpret_cast<const unsigned char *>(plain.data()),
                              static_cast<int>(plain.size())) != 1 ||
            EVP_EncryptFinal_ex(ctx.get(), reinterpret_cast<unsigned char *>(cipher.data()) + length, &tail) != 1)
            throw std::runtime_error("Fixture encryption failed");
        cipher.resize(length + tail);
        std::string base64((cipher.size() + 2) / 3 * 4 + 1, '\0');
        base64.resize(EVP_EncodeBlock(reinterpret_cast<unsigned char *>(base64.data()),
                                      reinterpret_cast<const unsigned char *>(cipher.data()),
                                      static_cast<int>(cipher.size())));
        std::string wrapped;
        for (std::size_t i = 0; i < base64.size(); i += 72)
            wrapped += base64.substr(i, 72) + "\n";
        return wrapped;
    }
    static std::string cwrm(const TopoDS_Shape &shape)
    {
        std::ostringstream stream;
        stream
            << R"(BREP_HEADER_START{"file_version":2,"shape_type":"brep","is_multi_workpiece":false}BREP_HEADER_END)";
        BRepTools::Write(shape, stream);
        return encode(stream.str());
    }
    static reverse::CloudData cloud()
    {
        reverse::CloudData result;
        result.originMm = {1234567.125, -987654.5, 45.25};
        result.hasIntensity = true;
        result.points = {{{-10, -4, -2}, 8.25, 0}, {{20, 3, 7}, 15, 1}, {{1, 6, -3}, 4, 2}, {{2, 8, 11}, 9, 3}};
        result.sourcePointCount = result.points.size();
        return result;
    }
};

TEST_F(FileExchange, PcdExportsCurrentSubsetInGlobalMillimetersWithIntensity)
{
    const auto original = cloud();
    const auto edited =
        reverse::editCloudPoints(original, reverse::CloudSelection{0, 2}, reverse::CloudEditMode::KeepSelection);
    const auto target = file(u8"截取结果（毫米）.pcd");
    const auto result = reverse::exportPointCloud(*edited, target);
    EXPECT_EQ(result.pointCount, 2);
    EXPECT_TRUE(result.hasIntensity);
    const auto loaded = reverse::loadPointCloud(target, reverse::LengthUnit::Millimeter);
    ASSERT_EQ(loaded->points.size(), 2);
    EXPECT_DOUBLE_EQ(loaded->points[0].position.x + loaded->originMm.x,
                     original.points[0].position.x + original.originMm.x);
    EXPECT_DOUBLE_EQ(loaded->points[1].intensity, original.points[2].intensity);
    EXPECT_THROW((void)reverse::exportPointCloud(original, target), reverse::GeometryError);
    EXPECT_EQ(std::filesystem::file_size(target), result.fileBytes);
}

TEST_F(FileExchange, PcdCancellationLeavesNoFileAndXyzOnlyWorks)
{
    auto source = cloud();
    source.hasIntensity = false;
    std::stop_source stop;
    const auto target = file(u8"取消导出.pcd");
    EXPECT_THROW((void)reverse::exportPointCloud(source, target, stop.get_token(),
                                                 [&](double p, auto)
                                                 {
                                                     if (p >= 0.6)
                                                         stop.request_stop();
                                                 }),
                 reverse::GeometryError);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(std::filesystem::is_empty(directory));
    EXPECT_FALSE(reverse::exportPointCloud(source, target).hasIntensity);
    EXPECT_FALSE(reverse::loadPointCloud(target, reverse::LengthUnit::Millimeter)->hasIntensity);
}

TEST_F(FileExchange, StepImportsNestedPlacedInstancesAndConvertsDeclaredUnits)
{
    const Handle(TDocStd_Document) doc = new TDocStd_Document("BinXCAF");
    const auto tool = XCAFDoc_DocumentTool::ShapeTool(doc->Main());
    XCAFDoc_DocumentTool::SetLengthUnit(doc, 0.001);
    const auto part = tool->AddShape(BRepPrimAPI_MakeBox(10, 20, 30).Shape(), false);
    const auto nested = tool->NewShape();
    gp_Trsf local;
    local.SetTranslation(gp_Vec(100, 0, 0));
    tool->AddComponent(nested, part, TopLoc_Location(local));
    const auto root = tool->NewShape();
    gp_Trsf outer;
    outer.SetTranslation(gp_Vec(0, 200, 0));
    tool->AddComponent(root, nested, TopLoc_Location(outer));
    local.SetTranslation(gp_Vec(-50, 0, 0));
    tool->AddComponent(root, part, TopLoc_Location(local));
    tool->UpdateAssemblies();
    STEPCAFControl_Writer writer;
    DESTEP_Parameters options;
    options.WriteUnit = UnitsMethods_LengthUnit_Meter;
    ASSERT_TRUE(writer.Transfer(doc, options, STEPControl_AsIs));
    const auto target = file(u8"模型（米单位）.STP");
    {
        std::ofstream stream(target, std::ios::binary);
        ASSERT_EQ(writer.WriteStream(stream), IFSelect_RetDone);
    }
    const auto model = reverse::loadCadModel(target);
    EXPECT_EQ(model->summary().importedFormat, "step");
    EXPECT_EQ(model->summary().solidCount, 2);
    EXPECT_NEAR(model->summary().volumeMm3, 12000, 1e-5);
    EXPECT_NEAR(model->summary().frame.originMm.x, 30, 1e-5);
    EXPECT_NEAR(model->summary().frame.originMm.y, 110, 1e-5);
    const auto result = reverse::exportStep(*model, file(u8"再导出.step"));
    EXPECT_EQ(result.solidCount, 2);
    EXPECT_TRUE(result.valid);
    const auto display =
        reverse::workbench::makeModelDisplayParts(model->mesh(), model->summary(), model->summary().frame.originMm);
    ASSERT_EQ(display.size(), 2);
    EXPECT_EQ(display[0].colorSrgb, display[1].colorSrgb); // 统一浅灰实体，使用深色轮廓区分部件。
    EXPECT_FALSE(display[0].thicknessMm);
    EXPECT_FALSE(display[0].thicknessUnresolved);
    EXPECT_THROW((void)reverse::evaluateDeviation(cloud(), *model), reverse::GeometryError);
}

TEST_F(FileExchange, CwrmV2PreservesSolidsStandaloneFacesAndPlacement)
{
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(100, 200, 300), 10, 20, 30).Shape());
    builder.Add(compound, BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1)), 0, 10, 0, 20).Shape());
    const auto input = file(u8"柏楚模型（中文）.cwrm");
    write(input, cwrm(compound));
    const auto model = reverse::loadCadModel(input);
    EXPECT_EQ(model->summary().importedFormat, "cwrm");
    EXPECT_EQ(model->summary().solidCount, 1);
    EXPECT_EQ(model->summary().surfaceCount, 1);
    EXPECT_NEAR(model->summary().volumeMm3, 6000, 1e-6);
    EXPECT_NEAR(model->summary().frame.originMm.z, 165, 1e-6);
    const auto exported = reverse::exportStep(*model, file(u8"转换.step"));
    EXPECT_TRUE(exported.valid);
    EXPECT_EQ(exported.surfaceCount, 1);
}

TEST_F(FileExchange, InvalidCwrmAndStepAreRejected)
{
    for (const auto &bad :
         {std::string("garbage"), std::string("AAAA===="),
          encode(R"(BREP_HEADER_START{"file_version":3,"shape_type":"brep"}BREP_HEADER_END)"),
          encode(
              R"(BREP_HEADER_START{"file_version":2,"shape_type":"brep"}BREP_HEADER_END CASCADE Topology V1, broken)")})
    {
        write(file(u8"坏文件.cwrm"), bad);
        EXPECT_THROW((void)reverse::loadCadModel(file(u8"坏文件.cwrm")), reverse::GeometryError);
    }
    write(file(u8"坏文件.step"), "not a step file");
    EXPECT_THROW((void)reverse::loadCadModel(file(u8"坏文件.step")), reverse::GeometryError);
    EXPECT_THROW((void)reverse::loadCadModel(file(u8"不存在.step")), reverse::GeometryError);
}

TEST_F(FileExchange, CwrmHistoryRestoresSolidsAndStandaloneFacesAfterDeleteAll)
{
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(10, 20, 30).Shape());
    builder.Add(compound, BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0, 0, 40), gp_Dir(0, 0, 1)), 0, 10, 0, 20).Shape());
    const auto input = file(u8"基线.cwrm");
    write(input, cwrm(compound));
    reverse::CommandService service;
    ASSERT_TRUE(finish(service, service.submitImport(input)).result.ok());
    const auto baseline = service.project();
    ASSERT_TRUE(finish(service, service.submitDeleteModelParts(baseline.model->partIds, baseline.revision)).result.ok());
    EXPECT_FALSE(service.project().model);
    ASSERT_TRUE(finish(service, service.submitLine("project.undo")).result.ok());
    EXPECT_EQ(service.project().model, baseline.model);
    ASSERT_TRUE(finish(service, service.submitLine("project.redo")).result.ok());
    EXPECT_FALSE(service.project().model);
    ASSERT_TRUE(finish(service, service.submitLine("project.reset")).result.ok());
    ASSERT_TRUE(finish(service, service.submitExport(file(u8"恢复.step"), service.project().revision)).result.ok());
    ASSERT_TRUE(service.project().lastExport);
    EXPECT_EQ(service.project().lastExport->solidCount, 1);
    EXPECT_EQ(service.project().lastExport->surfaceCount, 1);
    ASSERT_TRUE(finish(service, service.submitLine("project.undo")).result.ok());
    EXPECT_FALSE(service.project().model);
}

TEST_F(FileExchange, ServiceSwitchesCloudAndCadAndPreservesStateOnFailure)
{
    reverse::CommandService service;
    (void)reverse::exportPointCloud(cloud(), file(u8"点云.PCD"));
    ASSERT_EQ(finish(service, service.submitImport(file(u8"点云.PCD"))).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitExportCloud(file(u8"点云导出.pcd"), service.project().revision)).state,
              reverse::TaskState::Succeeded);
    write(file(u8"模型.cwrm"), cwrm(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    ASSERT_EQ(finish(service, service.submitImport(file(u8"模型.cwrm"))).state,
              reverse::TaskState::Succeeded);
    const auto cad = service.project();
    ASSERT_TRUE(cad.model);
    EXPECT_FALSE(cad.cloud);
    EXPECT_FALSE(cad.canUndoCloud);
    EXPECT_FALSE(cad.lastCloudExport);
    EXPECT_EQ(reverse::toJson(cad)["model"]["method"], "imported_cad");
    EXPECT_EQ(finish(service, service.submitExportCloud(file(u8"无点云.pcd"), cad.revision)).errorCode, "no_cloud");
    ASSERT_EQ(finish(service, service.submitExport(file(u8"导出.step"), cad.revision)).state,
              reverse::TaskState::Succeeded);
    write(file(u8"损坏.cwrm"), "broken");
    EXPECT_EQ(finish(service, service.submitImport(file(u8"损坏.cwrm"))).state,
              reverse::TaskState::Failed);
    EXPECT_EQ(service.project().model, cad.model);
    EXPECT_EQ(service.project().revision, cad.revision);
    ASSERT_EQ(finish(service, service.submitImport(file(u8"点云.PCD"))).state,
              reverse::TaskState::Succeeded);
    EXPECT_FALSE(service.project().model);
    EXPECT_TRUE(service.project().cloud);
}

TEST_F(FileExchange, OptionalLocalSamplesReadAndCadRoundtrip)
{
    const char *sampleDirectory = std::getenv("RM_SAMPLE_DIRECTORY");
    if (!sampleDirectory)
        GTEST_SKIP() << "No local sample directory supplied";
    const auto samples = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(sampleDirectory)));
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::directory_iterator(samples))
    {
        const auto extension = entry.path().extension().string();
        if (extension == ".step" || extension == ".cwrm")
        {
            SCOPED_TRACE(entry.path().filename().wstring());
            const auto model = reverse::loadCadModel(entry.path());
            ASSERT_GT(model->summary().solidCount, 0);
            const auto exported =
                reverse::exportStep(*model, directory / ("sample-" + std::to_string(++count) + ".step"));
            EXPECT_TRUE(exported.valid);
            const auto cwrmPath = directory / ("sample-" + std::to_string(count) + ".cwrm");
            EXPECT_TRUE(reverse::exportCwrm(*model, cwrmPath).valid);
            const auto restored = reverse::loadCadModel(cwrmPath);
            EXPECT_EQ(restored->summary().solidCount, model->summary().solidCount);
            EXPECT_NEAR(restored->summary().volumeMm3, model->summary().volumeMm3,
                        std::max(1e-4, model->summary().volumeMm3 * 1e-8));
            const auto sampled = reverse::sampleModelSurface(*restored, 10.0);
            EXPECT_FALSE(sampled->points.empty());
        }
        else if (extension == ".pcd")
        {
            SCOPED_TRACE(entry.path().filename().wstring());
            const auto source = reverse::loadPointCloud(entry.path(), reverse::LengthUnit::Millimeter);
            EXPECT_GT(source->points.size(), 100000);
        }
    }
    EXPECT_GT(count, 0) << "Sample directory must contain at least one CAD model";
}

TEST_F(FileExchange, CwrmExportRoundtripPreservesPartsAndPlacedSurfaces)
{
    TopoDS_Compound compound;
    BRep_Builder builder;
    builder.MakeCompound(compound);
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(100,200,300),10,20,30).Shape());
    builder.Add(compound, BRepPrimAPI_MakeBox(gp_Pnt(-30,-40,0),4,5,6).Shape());
    builder.Add(compound, BRepBuilderAPI_MakeFace(gp_Pln(gp_Pnt(0,0,60),gp_Dir(0,0,1)),0,5,0,8).Shape());
    write(file(u8"源.cwrm"), cwrm(compound));
    const auto model=reverse::loadCadModel(file(u8"源.cwrm"));
    const auto output=file(u8"写出模型（中文）.cwrm");
    const auto result=reverse::exportCwrm(*model,output);
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.solidCount,2);
    EXPECT_EQ(result.surfaceCount,1);
    const auto restored=reverse::loadCadModel(output);
    EXPECT_NEAR(restored->summary().volumeMm3,6120,1e-6);
    EXPECT_NEAR(restored->summary().frame.originMm.x,40,1e-6);
    EXPECT_NEAR(restored->summary().frame.originMm.z,165,1e-6);
    EXPECT_TRUE(reverse::exportStep(*restored,file(u8"回读.step")).valid);
    EXPECT_THROW((void)reverse::exportCwrm(*model,output),reverse::GeometryError);
    EXPECT_EQ(std::filesystem::file_size(output),result.fileBytes);
}

TEST_F(FileExchange, CwrmCancelNeverPublishesPartialFile)
{
    write(file(u8"源.cwrm"), cwrm(BRepPrimAPI_MakeBox(10,20,30).Shape()));
    const auto model=reverse::loadCadModel(file(u8"源.cwrm"));
    std::stop_source stop;
    EXPECT_THROW((void)reverse::exportCwrm(*model,file(u8"取消.cwrm"),stop.get_token(),
        [&](double p, auto){if(p>=0.5)stop.request_stop();}),reverse::GeometryError);
    EXPECT_FALSE(std::filesystem::exists(file(u8"取消.cwrm")));
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory),std::filesystem::directory_iterator{}),1);
}

TEST_F(FileExchange, ModelSamplingIsDeterministicAndOnWorldSpaceSurface)
{
    write(file(u8"源.cwrm"),cwrm(BRepPrimAPI_MakeBox(gp_Pnt(100,200,300),10,20,30).Shape()));
    const auto model=reverse::loadCadModel(file(u8"源.cwrm"));
    const auto a=reverse::sampleModelSurface(*model,2), b=reverse::sampleModelSurface(*model,2);
    ASSERT_EQ(a->points.size(),b->points.size());
    EXPECT_GT(a->points.size(),reverse::sampleModelSurface(*model,4)->points.size());
    for(std::size_t i=0;i<a->points.size();++i)
    {
        const auto &v=a->points[i];
        EXPECT_DOUBLE_EQ(v.position.x,b->points[i].position.x);
        EXPECT_EQ(v.sourceIndex,i);
        const double x=v.position.x+a->originMm.x, y=v.position.y+a->originMm.y, z=v.position.z+a->originMm.z;
        EXPECT_GE(x,100-1e-9); EXPECT_LE(x,110+1e-9);
        EXPECT_GE(y,200-1e-9); EXPECT_LE(y,220+1e-9);
        EXPECT_GE(z,300-1e-9); EXPECT_LE(z,330+1e-9);
        EXPECT_LT(std::min({std::abs(x-100),std::abs(x-110),std::abs(y-200),std::abs(y-220),std::abs(z-300),std::abs(z-330)}),1e-8);
    }
    EXPECT_FALSE(a->hasIntensity);
    EXPECT_THROW((void)reverse::sampleModelSurface(*model,0),reverse::GeometryError);
    EXPECT_THROW((void)reverse::sampleModelSurface(*model,0.01),reverse::GeometryError);
    std::stop_source stop;stop.request_stop();
    EXPECT_THROW((void)reverse::sampleModelSurface(*model,2,stop.get_token()),reverse::GeometryError);
}

TEST_F(FileExchange, CadConversionMatrixAndPcdInputBan)
{
    write(file(u8"源.CWRM"),cwrm(BRepPrimAPI_MakeBox(10,20,30).Shape()));
    EXPECT_GT(reverse::convertModelFile(file(u8"源.CWRM"),file(u8"源.STEP")).fileBytes,0);
    for(const auto &source:{file(u8"源.CWRM"),file(u8"源.STEP")})
    {
        for(const auto &suffix:{u8".step",u8".cwrm",u8".pcd"})
        {
            const auto output=directory/(source.stem().u8string()+source.extension().u8string()+suffix);
            const auto result=reverse::convertModelFile(source,output,3);
            EXPECT_GT(result.fileBytes,0);
            if(result.outputFormat=="pcd") EXPECT_EQ(reverse::loadPointCloud(output,reverse::LengthUnit::Millimeter)->points.size(),result.pointCount);
            else EXPECT_NEAR(reverse::loadCadModel(output)->summary().volumeMm3,6000,1e-6);
        }
    }
    (void)reverse::exportPointCloud(cloud(),file(u8"源.PCD"));
    for(const auto &suffix:{u8".step",u8".cwrm",u8".pcd"})
    {
        const auto output=directory/(std::u8string(u8"拒绝")+suffix);
        try { (void)reverse::convertModelFile(file(u8"源.PCD"),output); FAIL()<<"PCD conversion allowed"; }
        catch(const reverse::GeometryError &error) { EXPECT_EQ(error.code(),"pcd_conversion_disabled"); }
        EXPECT_FALSE(std::filesystem::exists(output));
    }
    // PCD 导入导出仍保持可用。
    EXPECT_EQ(reverse::exportPointCloud(*reverse::loadPointCloud(file(u8"源.PCD"),reverse::LengthUnit::Millimeter),file(u8"正常导出.pcd")).pointCount,4);
}

TEST_F(FileExchange, ConversionRejectsOverwriteBadExtensionAndInvalidModel)
{
    write(file(u8"源.cwrm"),cwrm(BRepPrimAPI_MakeBox(10,20,30).Shape()));
    write(file(u8"已有.step"),"keep existing");
    EXPECT_THROW((void)reverse::convertModelFile(file(u8"源.cwrm"),file(u8"已有.step")),reverse::GeometryError);
    EXPECT_EQ(std::filesystem::file_size(file(u8"已有.step")),13);
    EXPECT_THROW((void)reverse::convertModelFile(file(u8"源.cwrm"),file(u8"无效.stl")),reverse::GeometryError);
    write(file(u8"坏.step"),"broken");
    EXPECT_THROW((void)reverse::convertModelFile(file(u8"坏.step"),file(u8"拒绝.cwrm")),reverse::GeometryError);
    EXPECT_FALSE(std::filesystem::exists(file(u8"拒绝.cwrm")));
}

TEST_F(FileExchange, ConversionAndNewExportsRecordWithoutChangingProjectHistory)
{
    const auto source=file(u8"源.cwrm");
    write(source,cwrm(BRepPrimAPI_MakeBox(10,20,30).Shape()));
    reverse::CommandService service;
    const auto text=[](const std::filesystem::path &p){auto s=p.u8string();return std::string(reinterpret_cast<const char*>(s.data()),s.size());};
    ASSERT_TRUE(finish(service,service.submitImport(source)).result.ok());
    const auto before=service.project();
    ASSERT_TRUE(finish(service,service.submitStartRecording(file(u8"记录.py"))).result.ok());
    const auto count=service.recordingState().commandCount;
    auto task=finish(service,service.submitCommand("file.convert",{text(source),text(file(u8"转换.pcd"))},{{"spacing",4.0}}));
    ASSERT_TRUE(task.result.ok())<<task.result.message;
    ASSERT_TRUE(finish(service,service.submitExportCwrm(file(u8"导出.cwrm"),before.revision)).result.ok());
    ASSERT_TRUE(finish(service,service.submitExportModelCloud(file(u8"采样.pcd"),3,before.revision)).result.ok());
    EXPECT_EQ(service.recordingState().commandCount,count+3);
    EXPECT_EQ(service.project().model,before.model);
    EXPECT_EQ(service.project().revision,before.revision);
    EXPECT_EQ(service.project().canUndo,before.canUndo);
    EXPECT_FALSE(service.project().cloud);
    (void)reverse::exportPointCloud(cloud(),file(u8"点云.pcd"));
    task=finish(service,service.submitCommand("file.convert",{text(file(u8"点云.pcd")),text(file(u8"禁止.pcd"))}));
    EXPECT_EQ(task.errorCode,"pcd_conversion_disabled");
    EXPECT_EQ(service.recordingState().commandCount,count+3);
    ASSERT_TRUE(finish(service,service.submitStopRecording()).result.ok());
    // 中途重新录制不补写既往导出/转换副作用，也不破坏会话补写能力。
    ASSERT_TRUE(finish(service,service.submitStartRecording(file(u8"新记录.py"))).result.ok());
    EXPECT_EQ(service.recordingState().commandCount,count);
    ASSERT_TRUE(finish(service,service.submitStopRecording()).result.ok());
    EXPECT_TRUE(finish(service,service.submitLine("file.convert --help")).result.ok());
}

} // namespace
