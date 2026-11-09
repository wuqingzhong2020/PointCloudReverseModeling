/*
 * 文件描述：
 * 参考 ProjectManager 的按次日志与 Qt 分类格式，统一接收 UTF-8 消息并逐条刷新。
 * 每卷 10 MiB、保留 30 天；原子串行写入，无日志工作线程，异常时不依赖事件循环。
 * Windows 预先准备崩溃路径，记录未处理异常/terminate/abort 与尽力生成的小型转储。
 */
#include "commands/ApplicationLog.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <mutex>
#ifdef _WIN32
#include <qt_windows.h>
#include <DbgHelp.h>
#endif

namespace reverse::logging
{
namespace
{
std::atomic_bool running{};
struct State
{
    std::mutex mutex;
    QFile file;
    QString directory, stem;
    int volume{};
    QtMessageHandler previous{};
    bool installed{};
};
State &state()
{
    // 持续到进程结束，允许其他静态对象析构时输出日志。
    static auto *value = new State;
    return *value;
}
const char *levelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg: return "DEBUG";
    case QtInfoMsg: return "INFO";
    case QtWarningMsg: return "WARN";
    case QtCriticalMsg: return "ERROR";
    case QtFatalMsg: return "FATAL";
    }
    return "UNKNOWN";
}
void fallback(const char *text) noexcept
{
    std::fprintf(stderr, "%s\n", text);
    std::fflush(stderr);
}
#ifdef _WIN32
wchar_t crashTextPath[32768]{}, crashDumpPath[32768]{};
std::atomic_flag crashing = ATOMIC_FLAG_INIT;
LPTOP_LEVEL_EXCEPTION_FILTER previousFilter{};
std::terminate_handler previousTerminate{};
using SignalHandler = void (*)(int);
SignalHandler previousAbort{};

void crash(const char *reason, EXCEPTION_POINTERS *exception = nullptr) noexcept
{
    if (crashing.test_and_set())
    {
        // MSVC 的未处理 C++ 异常先经过 SEH 筛选器，再进入 terminate。
        // 保留首次原始现场，追加随后才能取得的 what()，不重复写转储。
        HANDLE report = CreateFileW(crashTextPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (report != INVALID_HANDLE_VALUE)
        {
            char text[1700]{};
            std::snprintf(text, sizeof(text), "Termination detail: %.1500s\r\n", reason);
            DWORD written{};
            WriteFile(report, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
            FlushFileBuffers(report);
            CloseHandle(report);
        }
        return;
    }
    // 不使用 Qt、堆分配或常规日志锁，避免故障线程正持有日志锁而死锁。
    CONTEXT context{};
    EXCEPTION_RECORD record{};
    EXCEPTION_POINTERS captured{&record, &context};
    if (!exception)
    {
        RtlCaptureContext(&context);
        record.ExceptionCode = 0xE0000001; // 主动捕获的 C++/CRT 终止现场。
#if defined(_M_X64)
        record.ExceptionAddress = reinterpret_cast<void *>(context.Rip);
#elif defined(_M_ARM64)
        record.ExceptionAddress = reinterpret_cast<void *>(context.Pc);
#endif
        exception = &captured;
    }
    HANDLE report = CreateFileW(crashTextPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    SYSTEMTIME now{};
    GetLocalTime(&now);
    char text[2048]{};
    std::snprintf(text, sizeof(text),
        "%04u-%02u-%02u %02u:%02u:%02u.%03u [FATAL] pid=%lu tid=%lu code=0x%08lX address=%p\r\n%.1500s\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, now.wMilliseconds,
        GetCurrentProcessId(), GetCurrentThreadId(), exception->ExceptionRecord->ExceptionCode,
        exception->ExceptionRecord->ExceptionAddress, reason);
    DWORD written{};
    if (report != INVALID_HANDLE_VALUE)
    {
        WriteFile(report, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
        FlushFileBuffers(report);
    }
    HANDLE dump = CreateFileW(crashDumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD dumpError = GetLastError();
    if (dump != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        const bool ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
            static_cast<MINIDUMP_TYPE>(MiniDumpWithThreadInfo | MiniDumpWithIndirectlyReferencedMemory),
            &info, nullptr, nullptr) != FALSE;
        dumpError = ok ? ERROR_SUCCESS : GetLastError();
        FlushFileBuffers(dump);
        CloseHandle(dump);
    }
    if (report != INVALID_HANDLE_VALUE)
    {
        std::snprintf(text, sizeof(text), "MiniDumpWriteDump error=%lu (0=success)\r\n", dumpError);
        WriteFile(report, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
        FlushFileBuffers(report);
        CloseHandle(report);
    }
}
LONG WINAPI unhandled(EXCEPTION_POINTERS *exception)
{
    crash("Unhandled Windows exception", exception);
    return previousFilter ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}
void terminateHandler() noexcept
{
    const char *reason = "std::terminate without an active exception";
    try
    {
        if (auto error = std::current_exception()) std::rethrow_exception(error);
    }
    catch (const std::exception &error) { crash(error.what()); std::abort(); }
    catch (...) { reason = "std::terminate: non-std exception"; }
    crash(reason);
    std::abort();
}
void abortHandler(int) { crash("SIGABRT"); }
#endif

void receive(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    thread_local bool receiving = false;
    if (receiving) { fallback("Recursive log message suppressed"); return; }
    receiving = true;
    try
    {
        auto &s = state();
        const auto time = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
        QString line = QStringLiteral("%1 [%2] pid=%3 tid=%4 [%5] %6")
            .arg(time, QString::fromLatin1(levelName(type)))
            .arg(QCoreApplication::applicationPid())
            .arg(reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16)
            .arg(QString::fromUtf8(context.category ? context.category : "default"), message);
        if (context.file)
            line += QStringLiteral(" (%1:%2 %3)").arg(QString::fromUtf8(context.file)).arg(context.line)
                .arg(QString::fromUtf8(context.function ? context.function : ""));
        const auto bytes = (line + QLatin1Char('\n')).toUtf8();
        {
            std::lock_guard lock(s.mutex);
            if (s.file.isOpen() && s.file.size() > 0 && s.file.size() + bytes.size() > 10 * 1024 * 1024)
            {
                s.file.close();
                s.file.setFileName(s.stem + QStringLiteral(".%1.log").arg(++s.volume));
                if (!s.file.open(QIODevice::WriteOnly | QIODevice::Append))
                    fallback("Cannot rotate application log");
            }
            if (!s.file.isOpen() || s.file.write(bytes) != bytes.size() || !s.file.flush())
                fallback(bytes.constData());
        }
        // 保留已有处理器及终端输出；不向 UI 发信号，防止日志递归或跨线程控件访问。
#ifdef _WIN32
        if (type == QtFatalMsg) crash(bytes.constData());
#endif
        if (s.previous) s.previous(type, context, message);
    }
    catch (...) { fallback("Application log write failed"); }
    receiving = false;
}
QString defaultDirectory()
{
#ifdef _WIN32
    wchar_t executable[32768]{};
    GetModuleFileNameW(nullptr, executable, 32768);
    QDir dir(QFileInfo(QString::fromWCharArray(executable)).absolutePath());
#else
    QDir dir(QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget());
    dir.cdUp();
#endif
    do
    {
        if (dir.dirName() == QStringLiteral("output")) return dir.filePath(QStringLiteral("log"));
    } while (dir.cdUp());
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/ReverseModeling/ReverseModeler/log");
}
} // namespace

bool start(const QString &requested)
{
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (s.installed) return true;
    const auto stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const auto name = QStringLiteral("ReverseModeler-%1-%2").arg(stamp).arg(QCoreApplication::applicationPid());
    QStringList candidates{requested.isEmpty() ? defaultDirectory() : requested};
    if (requested.isEmpty()) candidates << QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
        + QStringLiteral("/ReverseModeling/ReverseModeler/log");
    for (const auto &path : candidates)
    {
        QDir dir(path);
        if (!dir.mkpath(QStringLiteral("."))) continue;
        s.directory = dir.absolutePath();
        s.stem = dir.absoluteFilePath(name);
        s.file.setFileName(s.stem + QStringLiteral(".log"));
        if (s.file.open(QIODevice::WriteOnly | QIODevice::Append)) break;
    }
    if (!s.file.isOpen()) { fallback("Cannot open application log file"); return false; }
    const QRegularExpression owned(QStringLiteral("^ReverseModeler-\\d{8}-\\d{6}-\\d{3}-\\d+(?:\\.\\d+)?\\.log$"));
    const auto cutoff = QDateTime::currentDateTime().addDays(-30);
    for (const auto &file : QDir(s.directory).entryInfoList({QStringLiteral("ReverseModeler-*.log")}, QDir::Files))
        if (owned.match(file.fileName()).hasMatch() && file.lastModified() < cutoff)
            QFile::remove(file.absoluteFilePath());
#ifdef _WIN32
    wcscpy_s(crashTextPath, (s.stem + QStringLiteral("-crash.txt")).toStdWString().c_str());
    wcscpy_s(crashDumpPath, (s.stem + QStringLiteral("-crash.dmp")).toStdWString().c_str());
    crashing.clear();
    previousFilter = SetUnhandledExceptionFilter(unhandled);
    previousTerminate = std::set_terminate(terminateHandler);
    previousAbort = std::signal(SIGABRT, abortHandler);
#endif
    s.previous = qInstallMessageHandler(receive);
    s.installed = true;
    running.store(true);
    return true;
}
QString directory() { return state().directory; }
QString filePath() { std::lock_guard lock(state().mutex); return state().file.fileName(); }
void write(QtMsgType level, const char *category, const QString &message) noexcept
{
    // 服务单测没有启动进程日志时，继续使用原 commands.log，不向 stderr 重复刷屏。
    // 未启动时不要构造 QFile/QObject；服务可先在非 Qt 工作线程上运行。
    if (running.load()) receive(level, QMessageLogContext(nullptr, 0, nullptr, category), message);
}
void stop() noexcept
{
    auto &s = state();
    std::lock_guard lock(s.mutex);
    if (!s.installed) return;
    running.store(false);
    qInstallMessageHandler(s.previous);
#ifdef _WIN32
    SetUnhandledExceptionFilter(previousFilter);
    std::set_terminate(previousTerminate);
    std::signal(SIGABRT, previousAbort);
#endif
    s.installed = false;
    s.file.close();
}
} // namespace reverse::logging
