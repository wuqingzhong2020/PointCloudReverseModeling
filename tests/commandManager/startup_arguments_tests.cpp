/* 文件描述：验证启动参数边界、错误帮助及操作系统参数到命令文本的无损回显。 */
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "commandManager/CommandHelp.h"
#include "commands/CommandFeedback.h"
#include "commands/StartupArguments.h"

TEST(StartupArguments, EmptyHelpAndMissingInputs)
{
    using reverse::StartupMode;
    EXPECT_EQ(reverse::parseStartupArguments({}).mode, StartupMode::None);
    for (const auto &option : {"-h", "--help"})
        EXPECT_EQ(reverse::parseStartupArguments({option}).mode, StartupMode::Help);
    for (const auto &args : std::vector<std::vector<std::string>>{
             {"-c"}, {"-f"}, {"-c", ""}, {"-f", " \t"}, {"--wrong"}, {"--help", "extra"}})
    {
        const auto request = reverse::parseStartupArguments(args);
        EXPECT_EQ(request.mode, StartupMode::Error);
        EXPECT_FALSE(request.error.empty());
    }
    EXPECT_NE(reverse::startupUsage().find("sys.argv"), std::string::npos);
}
TEST(StartupArguments, TailArgumentsRemainUntouched)
{
    const std::vector<std::string> tail{"D:/中文 路径/脚本.py", "aa", "bb cc", "", "--help", "-f", "-3", "-platform", "offscreen"};
    auto args = tail;
    args.insert(args.begin(), "-f");
    const auto script = reverse::parseStartupArguments(args);
    EXPECT_EQ(script.mode, reverse::StartupMode::Script);
    EXPECT_EQ(script.arguments, tail);
    const auto command = reverse::parseStartupArguments({"-c", "cloud.import", "D:/中文 点云.pcd"});
    EXPECT_EQ(command.mode, reverse::StartupMode::Command);
    EXPECT_EQ(command.arguments, (std::vector<std::string>{"cloud.import", "D:/中文 点云.pcd"}));
    const auto expression = reverse::parseStartupArguments({"-c", "print('hello world')"});
    EXPECT_EQ(expression.arguments, (std::vector<std::string>{"print('hello world')"}));
}
TEST(StartupArguments, EchoPreservesSpacesQuotesJsonAndTrailingBackslashes)
{
    const std::vector<std::string> arguments{
        "example", "D:\\中文 目录\\", "", "O'Brien", "aa\"bb", R"({"a":"b c"})", "--arg", "-2"};
    EXPECT_EQ(rm::tokenize(reverse::commandArgumentsText(arguments)), arguments);
}

TEST(StartupArguments, ExtractsOnlyKnownPairsBeforeCommandOrScript)
{
    using reverse::StartupMode;
    const auto request = reverse::parseStartupArguments({
        "-c", "cloud.import", "中文 点云.pcd", "--output_dir", "结果 目录",
        "--input_file=另一份.pcd", "--help"});
    ASSERT_EQ(request.mode, StartupMode::Command) << request.error;
    EXPECT_EQ(request.arguments, (std::vector<std::string>{"cloud.import", "中文 点云.pcd", "--help"}));
    EXPECT_EQ(*request.outputDirectory, std::filesystem::absolute(std::filesystem::path(u8"结果 目录")));
    EXPECT_EQ(*request.inputFile, std::filesystem::absolute(std::filesystem::path(u8"另一份.pcd")));
    const auto script = reverse::parseStartupArguments({
        "--output_dir", "out", "-f", "job.py", "a", "--input_file", "scan.pcd", "--unit", "m", "b", ""});
    EXPECT_EQ(script.mode, StartupMode::Script);
    EXPECT_EQ(script.arguments, (std::vector<std::string>{"job.py", "a", "--unit", "m", "b", ""}));
    EXPECT_EQ(reverse::parseStartupArguments({"--input_file", "a.pcd"}).mode, StartupMode::None);
    EXPECT_EQ(reverse::parseStartupArguments({"--output_dir", "out"}).mode, StartupMode::None);
    EXPECT_EQ(reverse::parseStartupArguments({"--output_dir", "out", "--help"}).mode, StartupMode::Help);
}
TEST(StartupArguments, RejectsEmptyMissingOrDuplicateKnownPairs)
{
    for (const auto &args : std::vector<std::vector<std::string>>{
             {"--input_file"}, {"--output_dir", ""}, {"--input_file=  "},
             {"--output_dir", "a", "--output_dir=b"}, {"--input_file=x", "--input_file", "x"},
             {"--input_file", "--output_dir", "a"}, {"-f", "job.py", "--output_dir", "-c", "help"}})
    {
        const auto request = reverse::parseStartupArguments(args);
        EXPECT_EQ(request.mode, reverse::StartupMode::Error);
        EXPECT_FALSE(request.error.empty());
    }
}

TEST(StartupArguments, RecordingPairIsReservedAndKeepsOtherArguments)
{
    const auto request = reverse::parseStartupArguments({"-f", "work.py", "--unit", "mm",
        "--record_file=中文 录制.py", "--output_dir", "out", "--input_file", "in.pcd"});
    ASSERT_EQ(request.mode, reverse::StartupMode::Script) << request.error;
    EXPECT_EQ(request.arguments, (std::vector<std::string>{"work.py", "--unit", "mm"}));
    EXPECT_EQ(*request.recordFile, std::filesystem::absolute(std::filesystem::path(u8"中文 录制.py")));
    for (const auto &args : std::vector<std::vector<std::string>>{
        {"--record_file"}, {"--record_file="}, {"--record_file", "a.py", "--record_file=b.py"},
        {"--record_file", "--input_file", "in.pcd"}})
        EXPECT_EQ(reverse::parseStartupArguments(args).mode, reverse::StartupMode::Error);
    EXPECT_EQ(reverse::parseStartupArguments({"--record_file", "a.py", "--help"}).mode, reverse::StartupMode::Help);
    EXPECT_NE(reverse::startupUsage().find("--record_file"), std::string::npos);
}
