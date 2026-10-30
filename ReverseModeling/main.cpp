/*
 * 文件描述：
 * 创建 Qt 桌面应用，定位随程序部署的 OCCT 资源并启动工作台；业务初始化由命令服务负责。
 */
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
#if defined(Q_OS_WIN)
    // 让任务栏按产品标识关联窗口；在创建任何 Qt 窗口之前设置。
    if (FAILED(SetCurrentProcessExplicitAppUserModelID(applicationUserModelId)))
        qWarning("Failed to set ReverseModeler taskbar identity.");
#endif
    QApplication application(argc, argv);
    initializeRuntimeResources();
    QApplication::setApplicationName(QStringLiteral("ReverseModeler"));
    QApplication::setApplicationVersion(QStringLiteral(RM_APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("ReverseModeling"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/workbench/app/ReverseModeler.ico")));
    QApplication::setStyle(QStringLiteral("Fusion"));
    reverse::workbench::WorkbenchTheme::apply(application);
    reverse::workbench::MainWindow window;
#if defined(Q_OS_WIN)
    const TaskbarIdentity taskbarIdentity(window);
#endif
    window.show();
    return application.exec();
}
