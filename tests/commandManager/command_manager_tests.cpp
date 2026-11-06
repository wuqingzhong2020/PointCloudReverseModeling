/* 文件描述：命令参数、帮助、执行包装和生成描述的单元测试。 */
#include "CommandModules.h"
#include "cloud/ImportCommand.h"
#include "commandManager/CommandHelp.h"
#include "commandManager/CommandManager.h"
#include <gtest/gtest.h>
#include <limits>
#include <thread>
namespace
{
rm::CommandDefinition sample()
{
    rm::CommandDefinition d;
    d.name = "demo.cmd";
    d.module = "demo";
    d.summary = "测试命令";
    d.usage = "demo.cmd <first> <second> --mode aa|bb --count <1..3 or 5..6> [--enabled true]";
    for (std::size_t i = 1; i <= 2; ++i)
    {
        rm::Parameter p;
        p.name = i == 1 ? "first" : "second";
        p.binding = rm::Binding::Positional;
        p.position = i;
        p.required = true;
        d.parameters.push_back(p);
    }
    rm::Parameter mode;
    mode.name = "mode";
    mode.option = "--mode";
    mode.required = true;
    mode.rule.type = rm::ValueType::Enum;
    mode.rule.choices = {"aa", "bb"};
    d.parameters.push_back(mode);
    rm::Parameter count;
    count.name = "count";
    count.option = "--count";
    count.required = true;
    count.rule.type = rm::ValueType::Integer;
    count.rule.ranges = {{std::int64_t{1}, std::int64_t{3}}, {std::int64_t{5}, std::int64_t{6}}};
    d.parameters.push_back(count);
    rm::Parameter enabled;
    enabled.name = "enabled";
    enabled.option = "--enabled";
    enabled.rule.type = rm::ValueType::Boolean;
    enabled.defaultValue = true;
    d.parameters.push_back(enabled);
    return d;
}
rm::CommandManager manager()
{
    rm::CommandManager result;
    result.registerDefinition(sample());
    return result;
}
} // namespace
TEST(CommandParsing, MixedOptionsPreservePositionalOrder)
{
    auto m = manager();
    const auto a = m.validateLine("demo.cmd first --mode aa second --count 5");
    const auto b = m.validateLine("demo.cmd --count=5 --mode aa first second");
    ASSERT_TRUE(a.command);
    ASSERT_TRUE(b.command);
    EXPECT_EQ(a.command->arguments, b.command->arguments);
    EXPECT_EQ(a.command->arguments.at("first").as<std::string>(), "first");
    EXPECT_TRUE(a.command->arguments.at("enabled").as<bool>());
}
TEST(CommandParsing, RejectsDuplicateMissingUnknownAndExcess)
{
    auto m = manager();
    EXPECT_EQ(m.validateLine("demo.cmd a b --mode aa --mode aa --count 1").result.code, "duplicate_option");
    EXPECT_EQ(m.validateLine("demo.cmd a b --mode --count 1").result.code, "missing_option_value");
    EXPECT_EQ(m.validateLine("demo.cmd a b --bad 1").result.code, "unknown_option");
    EXPECT_EQ(m.validateLine("demo.cmd a b c --mode aa --count 1").result.code, "extra_positional");
    EXPECT_EQ(m.validateLine("demo.cmd a --mode aa --count 1").result.code, "missing_argument");
    EXPECT_EQ(m.validateLine("demo.cmd 'unclosed").result.code, "unclosed_quote");
}
TEST(CommandParsing, UnicodeWindowsPathsQuotesAndTerminator)
{
    auto m = manager();
    const auto result = m.validateLine(R"(demo.cmd "D:\中文 目录\点云.pcd" "" --mode aa --count 1)");
    ASSERT_TRUE(result.command) << result.result.message;
    EXPECT_EQ(result.command->arguments.at("first").as<std::string>(), "D:\\中文 目录\\点云.pcd");
    EXPECT_EQ(result.command->arguments.at("second").as<std::string>(), "");
    const auto literal = m.validateLine("demo.cmd --mode aa --count 1 -- --help -h");
    ASSERT_TRUE(literal.command);
    EXPECT_EQ(literal.command->arguments.at("first").as<std::string>(), "--help");
    EXPECT_NE(m.validateLine("demo.cmd --mode=--help --count 1 a b").result.kind, rm::ResultKind::Help);
    const auto argv = m.validateArgv({"demo.cmd", "literal ' quotes", "b", "--mode", "aa", "--count", "1"});
    ASSERT_TRUE(argv.command);
    EXPECT_EQ(argv.command->arguments.at("first").as<std::string>(), "literal ' quotes");
}
TEST(CommandValidation, ErrorsIdentifyOnlyTheOffendingParameter)
{
    auto m = manager();
    const auto invalid = m.validateLine("demo.cmd a b --mode wrong --count 1").result;
    EXPECT_EQ(invalid.code, "invalid_value");
    EXPECT_NE(invalid.message.find("--mode"), std::string::npos);
    EXPECT_NE(invalid.message.find("wrong"), std::string::npos);
    EXPECT_NE(invalid.message.find("aa|bb"), std::string::npos);
    EXPECT_EQ(invalid.message.find("\n"), std::string::npos);
    EXPECT_EQ(invalid.message.find("用法"), std::string::npos);
    EXPECT_EQ(invalid.message.find("--count"), std::string::npos);
    EXPECT_EQ(invalid.value.as<rm::Arguments>().at("parameter").as<std::string>(), "--mode");
    const auto range = m.validateLine("demo.cmd a b --mode aa --count 4").result;
    EXPECT_NE(range.message.find("count"), std::string::npos);
    EXPECT_NE(range.message.find("[1, 3]"), std::string::npos);
    EXPECT_NE(range.message.find("[5, 6]"), std::string::npos);
    const auto missing = m.validateLine("demo.cmd a b --mode aa --count").result;
    EXPECT_EQ(missing.message, "参数「--count」缺少值。");
    EXPECT_EQ(m.validateLine("demo.cmd a b --bad 1").result.message, "未知参数「--bad」。");
    EXPECT_EQ(m.validateLine("demo.cmd a b --mode aa --mode bb").result.message, "参数「--mode」重复，只能填写一次。");
    const auto extra = m.validateLine("demo.cmd a b extra --mode aa --count 1").result;
    EXPECT_NE(extra.message.find("position 3"), std::string::npos);
    EXPECT_NE(extra.message.find("extra"), std::string::npos);
}
TEST(CommandValidation, ErrorExcerptIsBoundedButDetailsRemainComplete)
{
    auto m = manager();
    std::string longValue;
    for (int i = 0; i < 300; ++i)
    {
        longValue += "中文";
    }
    const auto result = m.validate("demo.cmd", {"a", "b"}, {{"mode", longValue}, {"count", 1}}).result;
    EXPECT_LT(result.message.size(), 400);
    EXPECT_NE(result.value.as<rm::Arguments>().at("actual").as<std::string>().find(longValue), std::string::npos);
    EXPECT_NO_THROW((void)rm::validateValue(rm::Rule{}, result.message, "message"));
}
TEST(CommandValidation, StrictTypesRangeUnionAndOverflow)
{
    auto m = manager();
    for (const auto number : {1, 3, 5, 6})
    {
        EXPECT_TRUE(m.validateLine("demo.cmd a b --mode aa --count " + std::to_string(number)).command);
    }
    for (const auto value : {"0", "4", "7", "9223372036854775808", "1x", "-1"})
    {
        EXPECT_EQ(m.validateLine(std::string("demo.cmd a b --mode aa --count ") + value).result.code, "invalid_value");
    }
    EXPECT_FALSE(m.validate("demo.cmd", {"a", "b"}, {{"mode", "aa"}, {"count", true}}).command);
    EXPECT_FALSE(m.validate("demo.cmd", {"a", "b"}, {{"mode", "aa"}, {"count", "3"}}).command);
    EXPECT_FALSE(m.validateLine("demo.cmd a b --mode AA --count 1").command);
    rm::Rule numeric;
    numeric.type = rm::ValueType::Number;
    EXPECT_THROW(rm::validateValue(numeric, std::numeric_limits<double>::infinity(), "x"), rm::InputError);
    EXPECT_THROW(rm::validateValue(numeric, true, "x"), rm::InputError);
    rm::Rule string;
    EXPECT_THROW(rm::validateValue(string, std::string("\xff"), "x"), rm::InputError);
}
TEST(CommandValidation, JsonNestedTypesAndDuplicateKeys)
{
    rm::Rule rule;
    rule.type = rm::ValueType::Object;
    rm::Rule array;
    array.type = rm::ValueType::Array;
    rm::Rule element;
    element.type = rm::ValueType::Integer;
    array.items = std::make_shared<const rm::Rule>(element);
    rule.properties.emplace("values", array);
    EXPECT_NO_THROW(rm::convertToken(rule, R"({"values":[1,-2,3]})", "obj"));
    EXPECT_THROW(rm::convertToken(rule, R"({"values":[true]})", "obj"), rm::InputError);
    EXPECT_THROW(rm::convertToken(rule, R"({"values":[],"values":[]})", "obj"), rm::InputError);
    EXPECT_THROW(rm::convertToken(rule, R"({"unknown":[]})", "obj"), rm::InputError);
}
TEST(CommandHelp, HelpNeverRunsBusinessOrWrapper)
{
    auto m = manager();
    int calls = 0;
    m.bindHandler("demo.cmd", [&](const auto &, const auto &) {
        ++calls;
        return rm::CommandResult{};
    });
    m.registerExecutor([&](const auto &, const auto &, const auto &next) {
        ++calls;
        return next();
    });
    for (const auto line : {"help", "help demo", "help demo.cmd", "demo.cmd -h", "demo.cmd --mode --help",
                            "demo.cmd --help --help", "help -h", "help help"})
    {
        EXPECT_EQ(m.executeLine(line).kind, rm::ResultKind::Help) << line;
    }
    EXPECT_EQ(m.executeLine("demo.cmd --help").message, m.help("demo.cmd").message);
    EXPECT_EQ(m.executeLine("missing --help").code, "unknown_command");
    EXPECT_EQ(m.executeLine("help missing").code, "unknown_help_target");
    EXPECT_EQ(calls, 0);
}
TEST(CommandRegistration, DefinitionOnlyAndFrozenRegistry)
{
    auto m = manager();
    EXPECT_EQ(m.executeLine("demo.cmd a b --mode aa --count 1").code, "handler_not_bound");
    EXPECT_EQ(m.help("demo.cmd").kind, rm::ResultKind::Help);
    EXPECT_THROW(m.registerDefinition(sample()), std::invalid_argument);
    auto empty = sample();
    empty.name = "demo.empty";
    empty.usage = " \t";
    EXPECT_THROW(m.registerDefinition(empty), std::invalid_argument);
    auto reserved = sample();
    reserved.parameters[2].option = "--help";
    EXPECT_THROW(m.registerDefinition(reserved), std::invalid_argument);
    m.freeze();
    EXPECT_THROW(m.bindHandler("demo.cmd", [](const auto &, const auto &) { return rm::CommandResult{}; }),
                 std::logic_error);
}
TEST(CommandExecution, DirectOrWrappedDispatchAndNoInvalidExecution)
{
    auto m = manager();
    std::string order;
    m.bindHandler("demo.cmd", [&](const auto &, const auto &) {
        order += 'H';
        return rm::CommandResult{};
    });
    EXPECT_TRUE(m.executeLine("demo.cmd a b --mode aa --count 1").ok());
    EXPECT_EQ(order, "H");
    order.clear();
    m.registerExecutor([&](const auto &, const auto &, const auto &next) {
        order += 'B';
        auto r = next();
        order += 'E';
        return r;
    });
    EXPECT_TRUE(m.executeLine("demo.cmd a b --mode aa --count 1").ok());
    EXPECT_EQ(order, "BHE");
    order.clear();
    EXPECT_FALSE(m.executeLine("demo.cmd a b --mode aa --count 4").ok());
    EXPECT_TRUE(order.empty());
}
TEST(CommandExecution, NextLifetimeReentrancyAndCommittedFailure)
{
    auto m = manager();
    int count = 0;
    rm::CommandManager::Next retained;
    m.bindHandler("demo.cmd", [&](const auto &, const auto &) {
        ++count;
        EXPECT_EQ(m.executeLine("help").code, "reentrant_execution");
        return rm::CommandResult{rm::ResultKind::Success, {}, {}, {}, true};
    });
    m.registerExecutor([&](const auto &, const auto &, const auto &next) {
        retained = next;
        auto result = next();
        (void)next();
        return result;
    });
    auto result = m.executeLine("demo.cmd a b --mode aa --count 1");
    EXPECT_EQ(result.code, "invalid_next");
    EXPECT_TRUE(result.committed);
    EXPECT_EQ(count, 1);
    EXPECT_EQ(retained().code, "invalid_next");
    EXPECT_EQ(count, 1);
    m.registerExecutor([](const auto &, const auto &, const auto &next) -> rm::CommandResult {
        (void)next();
        throw std::runtime_error("post log failure");
    });
    result = m.executeLine("demo.cmd a b --mode aa --count 1");
    EXPECT_EQ(result.code, "execution_exception");
    EXPECT_TRUE(result.committed);
    std::stop_source cancellation;
    cancellation.request_stop();
    rm::CommandContext context;
    context.stop = cancellation.get_token();
    EXPECT_EQ(m.executeLine("demo.cmd a b --mode aa --count 1", context).code, "cancelled");
}
TEST(CommandGeneration, ModulesTypedDescriptorsAndHelp)
{
    rm::CommandManager m;
    rmcmd::registerAllCommandModules(m);
    EXPECT_GE(m.definitions().size(), 10);
    const auto parsed = m.validateLine("cloud.import '中文.pcd'");
    ASSERT_TRUE(parsed.command);
    const auto args = rmcmd::cloud::ImportCommand::arguments(parsed.command->arguments);
    EXPECT_EQ(args.path, "中文.pcd");
    EXPECT_EQ(parsed.command->arguments.size(), 1);
    EXPECT_EQ(m.validateLine("cloud.import '中文.pcd' --unit mm").result.code, "unknown_option");
    EXPECT_EQ(m.help("model.reconstruct").kind, rm::ResultKind::Help);
}
