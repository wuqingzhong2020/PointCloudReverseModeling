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
    const auto command = reverse::parseStartupArguments({"-c", "cloud.import", "D:/中文 点云.pcd", "--unit", "mm"});
    EXPECT_EQ(command.mode, reverse::StartupMode::Command);
    EXPECT_EQ(command.arguments, (std::vector<std::string>{"cloud.import", "D:/中文 点云.pcd", "--unit", "mm"}));
    const auto expression = reverse::parseStartupArguments({"-c", "print('hello world')"});
    EXPECT_EQ(expression.arguments, (std::vector<std::string>{"print('hello world')"}));
}
TEST(StartupArguments, EchoPreservesSpacesQuotesJsonAndTrailingBackslashes)
{
    const std::vector<std::string> arguments{
        "example", "D:\\中文 目录\\", "", "O'Brien", "aa\"bb", R"({"a":"b c"})", "--arg", "-2"};
    EXPECT_EQ(rm::tokenize(reverse::commandArgumentsText(arguments)), arguments);
}
