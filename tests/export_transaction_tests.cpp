/* 文件描述：用三种真实导出格式验证提交前取消、目标争用与异常清理，禁止覆盖用户文件。 */
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include "geometry/Geometry.h"
#include "geometry/ModelExchange.h"

namespace
{
using namespace reverse;

enum class ExportFormat
{
    Pcd,
    Step,
    Cwrm
};

class ExportTransactions : public testing::TestWithParam<ExportFormat>
{
  protected:
    void SetUp() override
    {
        ASSERT_TRUE(temporary.isValid());
        directory = std::filesystem::path(temporary.path().toStdWString());
        const auto extension = GetParam() == ExportFormat::Pcd    ? ".pcd"
                               : GetParam() == ExportFormat::Step ? ".step"
                                                                  : ".cwrm";
        target = directory / (std::string("result") + extension);
        model = buildBeam(BeamFrame{}, {100, 40, 60, 4, 5, 3, {}});
    }

    void exportTo(const std::filesystem::path &path, std::stop_token stop = {}, const ProgressCallback &progress = {})
    {
        if (GetParam() == ExportFormat::Pcd)
        {
            CloudData cloud;
            cloud.originMm = {1234567, -7654321, 100};
            cloud.points = {{{0, 0, 0}, 0, 0}, {{10, 20, 30}, 0, 1}, {{30, 10, 20}, 0, 2}};
            cloud.sourcePointCount = cloud.points.size();
            (void)exportPointCloud(cloud, path, stop, progress);
        }
        else if (GetParam() == ExportFormat::Step)
        {
            (void)exportStep(*model, path, stop, progress);
        }
        else
        {
            (void)exportCwrm(*model, path, stop, progress);
        }
    }

    bool beforeCommit(double progress) const
    {
        return std::abs(progress - (GetParam() == ExportFormat::Step ? 0.95 : 0.99)) < 1e-12;
    }

    void expectTargetPreserved() const
    {
        std::ifstream file(target, std::ios::binary);
        ASSERT_TRUE(file.is_open());
        const std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content, "user-owned content");
        EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()),
                  1);
    }

    QTemporaryDir temporary;
    std::filesystem::path directory, target;
    std::shared_ptr<const Model> model;
};

TEST_P(ExportTransactions, CancellationAtFinalCheckpointDoesNotPublishFile)
{
    std::stop_source cancellation;
    bool reachedCommit = false;
    try
    {
        exportTo(target, cancellation.get_token(),
                 [&](double progress, std::string_view)
                 {
                     if (beforeCommit(progress))
                     {
                         reachedCommit = true;
                         cancellation.request_stop();
                     }
                 });
        FAIL() << "Cancellation before publication must leave no output file";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "cancelled");
    }
    EXPECT_TRUE(reachedCommit);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

TEST_P(ExportTransactions, TargetCreatedDuringExportIsNeverOverwritten)
{
    bool createdTarget = false;
    try
    {
        exportTo(target, {},
                 [&](double progress, std::string_view)
                 {
                     if (beforeCommit(progress))
                     {
                         std::ofstream(target, std::ios::binary) << "user-owned content";
                         createdTarget = true;
                     }
                 });
        FAIL() << "Another writer's file must not be replaced";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "output_commit_failed");
    }
    EXPECT_TRUE(createdTarget);
    expectTargetPreserved();
}

TEST_P(ExportTransactions, CallbackFailureCleansStagingAndAllowsRetry)
{
    EXPECT_THROW(exportTo(target, {},
                          [&](double progress, std::string_view)
                          {
                              if (beforeCommit(progress))
                              {
                                  throw std::runtime_error("injected progress failure");
                              }
                          }),
                 std::runtime_error);
    EXPECT_FALSE(std::filesystem::exists(target));
    EXPECT_TRUE(std::filesystem::is_empty(directory));
    EXPECT_NO_THROW(exportTo(target));
    EXPECT_GT(std::filesystem::file_size(target), 0u);
    EXPECT_EQ(std::distance(std::filesystem::directory_iterator(directory), std::filesystem::directory_iterator()), 1);
}

TEST_P(ExportTransactions, ExistingUserFileIsRejectedWithoutChangingContent)
{
    std::ofstream(target, std::ios::binary) << "user-owned content";
    try
    {
        exportTo(target);
        FAIL() << "Existing output must be rejected";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "output_exists");
    }
    expectTargetPreserved();
}

TEST_P(ExportTransactions, MissingParentIsNotCreatedAsASideEffect)
{
    try
    {
        exportTo(directory / "missing" / target.filename());
        FAIL() << "Missing output directory must be reported";
    }
    catch (const GeometryError &error)
    {
        EXPECT_EQ(error.code(), "output_not_writable");
    }
    EXPECT_TRUE(std::filesystem::is_empty(directory));
}

INSTANTIATE_TEST_SUITE_P(AllFormats, ExportTransactions,
                         testing::Values(ExportFormat::Pcd, ExportFormat::Step, ExportFormat::Cwrm),
                         [](const testing::TestParamInfo<ExportFormat> &info)
                         {
                             return info.param == ExportFormat::Pcd    ? "Pcd"
                                    : info.param == ExportFormat::Step ? "Step"
                                                                       : "Cwrm";
                         });
} // namespace
