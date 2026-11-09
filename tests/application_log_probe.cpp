/*
 * 文件描述：
 * 日志回归专用子进程，验证多线程日志、分卷和真实异常退出，不给产品添加崩溃命令。
 */
#include "commands/ApplicationLog.h"
#include <QCoreApplication>
#include <QDebug>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef _WIN32
#include <qt_windows.h>
#endif
#ifdef _MSC_VER
#include <crtdbg.h>
#endif

int main(int argc, char **argv)
{
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
#ifdef _MSC_VER
    // 崩溃探针必须自动退出，Debug CRT 的交互弹窗会阻塞父进程测试。
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef _DEBUG
    for (const int report : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT})
    {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
#endif
#endif
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() != 3) return 2;
    if (!reverse::logging::start(args[1])) return 3;
    const auto mode = args[2];
    qInfo("probe.begin");
    if (mode == "terminate") throw std::runtime_error("probe unhandled C++ exception");
    if (mode == "fatal") qFatal("probe Qt fatal");
    if (mode == "abort") std::abort();
#ifdef _WIN32
    if (mode == "seh") RaiseException(EXCEPTION_ACCESS_VIOLATION, EXCEPTION_NONCONTINUABLE, 0, nullptr);
#endif
    if (mode == "rotate")
    {
        const QString block(1024 * 1024, QLatin1Char('x'));
        for (int i = 0; i < 12; ++i) qInfo().noquote() << i << block;
    }
    else
    {
        std::vector<std::jthread> workers;
        for (int t = 0; t < 4; ++t)
            workers.emplace_back([t] {
                for (int i = 0; i < 50; ++i) qInfo("thread-record %d/%d", t, i);
            });
    }
    qWarning("封板诊断 UTF-8");
    qCritical("probe.error");
    qInfo("probe.end");
    reverse::logging::stop();
    return 0;
}
