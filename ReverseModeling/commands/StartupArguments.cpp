/*
 * 文件描述：先提取全局路径键值对，再解析命令或脚本请求；路径按启动工作目录解析，保留业务参数边界。
 */
#include <filesystem>

#include "commands/StartupArguments.h"

namespace reverse
{
StartupRequest parseStartupArguments(const std::vector<std::string> &arguments)
{
    StartupRequest result;
    std::vector<std::string> remaining;
    const auto fail = [&result](std::string message) {
        result.mode = StartupMode::Error;
        result.error = std::move(message);
        return result;
    };
    for (std::size_t i = 0; i < arguments.size(); ++i)
    {
        const auto &token = arguments[i];
        const auto equal = token.find('=');
        const auto key = token.substr(0, equal);
        if (key != "--input_file" && key != "--output_dir" && key != "--record_file")
        {
            remaining.push_back(token);
            continue;
        }
        auto &path = key == "--input_file" ? result.inputFile :
                     key == "--output_dir" ? result.outputDirectory : result.recordFile;
        if (path)
        {
            return fail("参数 " + key + " 重复提供。");
        }
        std::string value;
        if (equal != std::string::npos)
        {
            value = token.substr(equal + 1);
        }
        else
        {
            if (i + 1 == arguments.size() || arguments[i + 1].starts_with("--") ||
                arguments[i + 1] == "-c" || arguments[i + 1] == "-f" || arguments[i + 1] == "-h")
            {
                return fail("参数 " + key + " 后缺少路径。");
            }
            value = arguments[++i];
        }
        if (value.find_first_not_of(" \t\r\n") == std::string::npos)
        {
            return fail("参数 " + key + " 的路径不能为空。");
        }
        try
        {
            path = std::filesystem::absolute(std::filesystem::path(std::u8string(value.begin(), value.end())))
                       .lexically_normal();
        }
        catch (const std::exception &error)
        {
            return fail("参数 " + key + " 的路径无效：" + value + "；" + error.what());
        }
    }
    if (remaining.empty())
    {
        return result;
    }
    const auto &option = remaining.front();
    if (option == "-h" || option == "--help")
    {
        if (remaining.size() != 1)
        {
            return fail(option + " 不接受额外参数。");
        }
        result.mode = StartupMode::Help;
        return result;
    }
    if (option != "-c" && option != "-f")
    {
        return fail("未知启动参数：" + option);
    }
    if (remaining.size() < 2 || remaining[1].find_first_not_of(" \t\r\n") == std::string::npos)
    {
        return fail(option == "-c" ? "-c 后缺少命令。" : "-f 后缺少脚本路径。");
    }
    result.mode = option == "-c" ? StartupMode::Command : StartupMode::Script;
    result.arguments.assign(remaining.begin() + 1, remaining.end());
    if (result.mode == StartupMode::Script && result.recordFile)
    {
        std::error_code error;
        const auto script = std::filesystem::path(std::u8string(remaining[1].begin(), remaining[1].end()));
        if (std::filesystem::equivalent(*result.recordFile, script, error) && !error)
            return fail("--record_file 不能与 -f 待执行脚本指向同一个文件，请使用另一个录制文件。");
    }
    return result;
}
std::string startupUsage()
{
    return "用法：\n"
           "  ReverseModeler.exe [--input_file 点云.pcd] [--output_dir 输出目录] [--record_file 录制.py]\n"
           "                     [-c 命令 [参数...] | -f 脚本.py [参数...]]\n"
           "  ReverseModeler.exe -c \"完整命令或 Python 语句\"\n"
           "  ReverseModeler.exe -h | --help    打印用法并退出，不打开窗口\n\n"
           "参数：\n"
           "  --input_file 路径   自动导入 PCD（mm），不自动重建；可独立省略\n"
           "  --output_dir 目录  初始化 result.json，启用固定路径的“导出并退出”\n"
           "  --record_file 路径  启动即录制到 .py；已有文件直接覆盖并打印日志\n"
           "  -c 命令 [参数...]  执行一条命令；导出并退出命令成功时关闭工作台\n"
           "  -f 脚本.py [参数...]  执行脚本；参数通过 sys.argv[1:] 读取\n\n"
           "示例：\n"
           "  ReverseModeler.exe --input_file \"D:/data/中文 点云.pcd\" --output_dir \"D:/jobs/任务 1\"\n"
           "  ReverseModeler.exe --output_dir ./result -c help cloud.import\n"
           "  ReverseModeler.exe -c cloud.import \"D:/data/点云.pcd\" --unit mm\n"
           "  ReverseModeler.exe -c \"print('hello')\"\n"
           "  ReverseModeler.exe -f \"D:/scripts/建模.py\" aa \"bb cc\" --count 2 --output_dir ./result\n\n"
           "先从全部参数提取 --input_file/--output_dir/--record_file（也支持 --键=值），再解析 -c/-f。\n"
           "其余 -c/-f 后的内容保留给命令或脚本；sys.argv[0] 是脚本路径。\n"
           "三个全局路径键不可重复、不可为空；中文和空格路径请加引号，相对路径按启动工作目录解析。\n"
           "先开始录制，再初始化输出结果、自动导入，最后执行 -c/-f；--input_file 仅支持点云。\n"
           "“导出并退出”写出 reserveModel.step 和 export_success=true 的 result.json 后退出。\n"
           "未设置输出目录时禁用该按钮，也可执行 project.set_output_dir 设置；普通导出或关闭不标记成功。每个任务请使用独立输出目录。\n"
           "-h/--help 返回 0；启动参数错误打印到 stderr 并返回 2，均不打开窗口。\n";
}
} // namespace reverse
