/*
 * 文件描述：解析 -c/-f 启动请求及帮助；不读取脚本、不执行命令，也不重新拆分已由系统解析的参数。
 * 启动入口仅消费首个选项，后面的 --help、负数、空字符串等均保留给业务命令或脚本。
 */
#include "commands/StartupArguments.h"

namespace reverse
{
StartupRequest parseStartupArguments(const std::vector<std::string> &arguments)
{
    if (arguments.empty())
        return {};
    const auto &option = arguments.front();
    if (option == "-h" || option == "--help")
    {
        if (arguments.size() != 1)
            return {StartupMode::Error, {}, option + " 不接受额外参数。"};
        return {StartupMode::Help, {}, {}};
    }
    if (option != "-c" && option != "-f")
        return {StartupMode::Error, {}, "未知启动参数：" + option};
    if (arguments.size() < 2 || arguments[1].find_first_not_of(" \t\r\n") == std::string::npos)
        return {StartupMode::Error, {}, option == "-c" ? "-c 后缺少命令。" : "-f 后缺少脚本路径。"};
    return {option == "-c" ? StartupMode::Command : StartupMode::Script,
            {arguments.begin() + 1, arguments.end()}, {}};
}
std::string startupUsage()
{
    return "用法：\n"
           "  ReverseModeler.exe                         打开工作台\n"
           "  ReverseModeler.exe -c 命令 [参数...]         执行一条命令\n"
           "  ReverseModeler.exe -c \"完整命令或 Python 语句\"\n"
           "  ReverseModeler.exe -f 脚本.py [参数...]      执行脚本，参数通过 sys.argv 读取\n"
           "  ReverseModeler.exe -h | --help              打印启动用法并退出，不打开窗口\n\n"
           "示例：\n"
           "  ReverseModeler.exe -c help cloud.import\n"
           "  ReverseModeler.exe -c cloud.import \"D:/data/中文 点云.pcd\" --unit mm\n"
           "  ReverseModeler.exe -c \"print('hello')\"\n"
           "  ReverseModeler.exe -f \"D:/scripts/建模.py\" aa \"bb cc\" --count 2\n\n"
           "-c/-f 后的其余内容全部交给对应命令或脚本，不再解析为程序启动选项。\n"
           "脚本中 sys.argv[0] 是脚本路径，sys.argv[1:] 是后续参数（例如 ['aa', 'bb cc', '--count', '2']）。\n"
           "含空格的路径或单个参数请用引号包围。-c/-f 打开工作台并显示执行结果，完成后保持打开。\n"
           "-h/--help 在终端打印本帮助后退出（状态码 0）；启动参数错误打印到 stderr 并退出（状态码 2）。\n";
}
} // namespace reverse
