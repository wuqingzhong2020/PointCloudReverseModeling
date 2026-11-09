/*
 * 文件描述：
 * 先处理终端帮助与启动参数错误；其余请求创建 Qt 桌面工作台，业务由命令服务执行。
 */
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QDebug>
#include <QIcon>

#if defined(Q_OS_WIN)
#include <qt_windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <propkey.h>
#include <wrl/client.h>
#endif

#include "commands/StartupArguments.h"
#include "commands/ApplicationLog.h"
#include "workbench/MainWindow.h"
#include "workbench/WorkbenchTheme.h"

namespace
{

#if defined(Q_OS_WIN)
constexpr wchar_t applicationUserModelId[] = L"ReverseModeling.ReverseModeler";

// 进程标识负责分组；窗口属性明确指定任务栏按钮及固定快捷方式的图标与启动目标。
// 在窗口首次显示前写入，并在窗口销毁前清除，避免 Shell 属性资源泄漏。
class TaskbarIdentity final
{
public:
    explicit TaskbarIdentity(QWidget &window)
    {
        const HRESULT result = SHGetPropertyStoreForWindow(
            reinterpret_cast<HWND>(window.winId()), IID_PPV_ARGS(properties_.GetAddressOf()));
        if (FAILED(result))
        {
            qWarning("Failed to access ReverseModeler taskbar properties: 0x%08lx", result);
            return;
        }
        const QString executable = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        // 资源编号对应 ReverseModeler.rc.in；使用正在运行的 EXE，兼容安装目录变更。
        set(PKEY_AppUserModel_RelaunchIconResource, executable + QStringLiteral(",-101"));
        set(PKEY_AppUserModel_RelaunchDisplayNameResource,
            QStringLiteral("@%1,-102").arg(executable));
        set(PKEY_AppUserModel_RelaunchCommand, QStringLiteral("\"%1\"").arg(executable));
        // Microsoft 要求先设置 relaunch 信息，最后设置窗口 AppUserModelID。
        set(PKEY_AppUserModel_ID, QString::fromWCharArray(applicationUserModelId));
    }

    ~TaskbarIdentity()
    {
        if (!properties_) return;
        const PROPVARIANT empty{};
        for (const auto &key : {PKEY_AppUserModel_ID, PKEY_AppUserModel_RelaunchCommand,
                               PKEY_AppUserModel_RelaunchDisplayNameResource,
                               PKEY_AppUserModel_RelaunchIconResource})
            properties_->SetValue(key, empty);
    }

    TaskbarIdentity(const TaskbarIdentity &) = delete;
    TaskbarIdentity &operator=(const TaskbarIdentity &) = delete;

private:
    void set(const PROPERTYKEY &key, const QString &text)
    {
        auto value = text.toStdWString();
        PROPVARIANT property{};
        property.vt = VT_LPWSTR;
        property.pwszVal = value.data(); // SetValue 复制字符串；此处内存由 value 持有。
        const HRESULT result = properties_->SetValue(key, property);
        if (FAILED(result))
            qWarning("Failed to set ReverseModeler taskbar property %lu: 0x%08lx", key.pid, result);
    }

    Microsoft::WRL::ComPtr<IPropertyStore> properties_;
};
#endif

reverse::StartupRequest startupRequest(int argc, char *argv[])
{
    std::vector<std::string> arguments;
#if defined(Q_OS_WIN)
    // Use the original UTF-16 command line, before Qt consumes options or the CRT loses Chinese text.
    int count = 0;
    auto **values = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!values)
        return {reverse::StartupMode::Error, {}, "无法读取启动参数。"};
    for (int i = 1; i < count; ++i)
        arguments.push_back(QString::fromWCharArray(values[i]).toUtf8().toStdString());
    LocalFree(values);
    (void)argc;
    (void)argv;
#else
    for (int i = 1; i < argc; ++i)
        arguments.push_back(QString::fromLocal8Bit(argv[i]).toUtf8().toStdString());
#endif
    return reverse::parseStartupArguments(arguments);
}

bool writeStartupOutput(const std::string &message, bool error)
{
#if defined(Q_OS_WIN)
    const auto stream = error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE;
    HANDLE output = GetStdHandle(stream);
    const auto valid = [](HANDLE handle) {
        return handle && handle != INVALID_HANDLE_VALUE && GetFileType(handle) != FILE_TYPE_UNKNOWN;
    };
    // A Windows GUI executable may have no CRT stdout. Preserve inherited pipes/files
    // (Git Bash, redirection); attach to the parent console only when no handle exists.
    if (!valid(output))
    {
        if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
        output = GetStdHandle(stream);
    }
    if (!valid(output)) return false;
    DWORD mode = 0;
    if (GetConsoleMode(output, &mode))
    {
        const auto wide = QString::fromUtf8(message.data(), static_cast<qsizetype>(message.size())).toStdWString();
        std::size_t offset = 0;
        while (offset < wide.size())
        {
            DWORD written = 0;
            const auto count = static_cast<DWORD>(std::min<std::size_t>(wide.size() - offset, 16384));
            if (!WriteConsoleW(output, wide.data() + offset, count, &written, nullptr) || written == 0)
                return false;
            offset += written;
        }
        return true;
    }
    std::size_t offset = 0;
    while (offset < message.size())
    {
        DWORD written = 0;
        const auto count = static_cast<DWORD>(std::min<std::size_t>(message.size() - offset, 65536));
        if (!WriteFile(output, message.data() + offset, count, &written, nullptr) || written == 0)
            return false;
        offset += written;
    }
    return true;
#else
    auto *output = error ? stderr : stdout;
    return std::fwrite(message.data(), 1, message.size(), output) == message.size() && std::fflush(output) == 0;
#endif
}

void initializeRuntimeResources()
{
    const QDir resources(QCoreApplication::applicationDirPath()
                         + QStringLiteral("/resources/opencascade"));
    const auto setDirectory = [&resources](const char *variable, const QString &relative)
    {
        const QString path = resources.filePath(relative);
        if (QFileInfo(path).isDir())
        {
            qputenv(variable, QDir::toNativeSeparators(path).toUtf8());
        }
    };
    setDirectory("CSF_ShadersDirectory", QStringLiteral("Shaders"));
    setDirectory("CSF_SHMessage", QStringLiteral("SHMessage"));
    setDirectory("CSF_XSMessage", QStringLiteral("XSMessage"));
    setDirectory("CSF_STEPDefaults", QStringLiteral("XSTEPResource"));
    setDirectory("CSF_IGESDefaults", QStringLiteral("XSTEPResource"));
    setDirectory("CSF_StandardDefaults", QStringLiteral("StdResource"));
    setDirectory("CSF_XCAFDefaults", QStringLiteral("StdResource"));
}

} // namespace

int main(int argc, char* argv[]) {
    reverse::logging::start();
    struct LogLifetime
    {
        ~LogLifetime() { reverse::logging::stop(); }
    } logLifetime;
    qInfo("Application starting; version=%s; build=%s %s", RM_APP_VERSION, __DATE__, __TIME__);
    const auto startup = startupRequest(argc, argv);
    if (startup.mode == reverse::StartupMode::Help || startup.mode == reverse::StartupMode::Error)
    {
        const bool error = startup.mode == reverse::StartupMode::Error;
        const auto message = (error ? startup.error + "\n\n" : std::string{}) + reverse::startupUsage();
        const bool printed = writeStartupOutput(message, error);
        return error ? 2 : (printed ? 0 : 1);
    }
#if defined(Q_OS_WIN)
    // 让任务栏按产品标识关联窗口；在创建任何 Qt 窗口之前设置。
    if (FAILED(SetCurrentProcessExplicitAppUserModelID(applicationUserModelId)))
        qWarning("Failed to set ReverseModeler taskbar identity.");
#endif
    // Product/script arguments such as -style and -platform must not be consumed by QApplication.
    int qtArgc = 1;
    char *qtArgv[] = {argv[0], nullptr};
    QApplication application(qtArgc, qtArgv);
    qInfo().noquote() << "Executable:" << QCoreApplication::applicationFilePath()
                     << "Log:" << reverse::logging::filePath();
    initializeRuntimeResources();
    QApplication::setApplicationName(QStringLiteral("ReverseModeler"));
    QApplication::setApplicationVersion(QStringLiteral(RM_APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("ReverseModeling"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/workbench/app/ReverseModeler.ico")));
    QApplication::setStyle(QStringLiteral("Fusion"));
    reverse::workbench::WorkbenchTheme::apply(application);
    reverse::workbench::MainWindow window(nullptr, startup);
#if defined(Q_OS_WIN)
    const TaskbarIdentity taskbarIdentity(window);
#endif
    window.show();
    const int result = application.exec();
    qInfo("Event loop finished; exitCode=%d", result);
    return result;
}
