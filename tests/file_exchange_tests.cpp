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
    EXPECT_NE(display[0].colorSrgb, display[1].colorSrgb);
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

TEST_F(FileExchange, ServiceSwitchesCloudAndCadAndPreservesStateOnFailure)
{
    reverse::CommandService service;
    (void)reverse::exportPointCloud(cloud(), file(u8"点云.PCD"));
    ASSERT_EQ(finish(service, service.submitImport(file(u8"点云.PCD"), reverse::LengthUnit::Millimeter)).state,
              reverse::TaskState::Succeeded);
    ASSERT_EQ(finish(service, service.submitExportCloud(file(u8"点云导出.pcd"), service.project().revision)).state,
              reverse::TaskState::Succeeded);
    write(file(u8"模型.cwrm"), cwrm(BRepPrimAPI_MakeBox(10, 20, 30).Shape()));
    ASSERT_EQ(finish(service, service.submitImport(file(u8"模型.cwrm"), reverse::LengthUnit::Millimeter)).state,
              reverse::TaskState::Succeeded);
    const auto cad = service.project();
    ASSERT_TRUE(cad.model);
    EXPECT_FALSE(cad.cloud);
    EXPECT_FALSE(cad.canUndoCloud);
    EXPECT_FALSE(cad.lastCloudExport);
    EXPECT_EQ(reverse::toJson(cad)["model"]["method"], "imported_cad");
    EXPECT_THROW((void)service.submitExportCloud(file(u8"无点云.pcd"), cad.revision), reverse::GeometryError);
    ASSERT_EQ(finish(service, service.submitExport(file(u8"导出.step"), cad.revision)).state,
              reverse::TaskState::Succeeded);
    write(file(u8"损坏.cwrm"), "broken");
    EXPECT_EQ(finish(service, service.submitImport(file(u8"损坏.cwrm"), reverse::LengthUnit::Millimeter)).state,
              reverse::TaskState::Failed);
    EXPECT_EQ(service.project().model, cad.model);
    EXPECT_EQ(service.project().revision, cad.revision);
    ASSERT_EQ(finish(service, service.submitImport(file(u8"点云.PCD"), reverse::LengthUnit::Millimeter)).state,
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
        }
        else if (extension == ".pcd")
        {
            SCOPED_TRACE(entry.path().filename().wstring());
            const auto source = reverse::loadPointCloud(entry.path(), reverse::LengthUnit::Millimeter);
            EXPECT_GT(source->points.size(), 100000);
        }
    }
    EXPECT_GE(count, 5);
}
} // namespace
