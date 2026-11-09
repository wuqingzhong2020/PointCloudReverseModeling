/*
 * 文件描述：
 * 用隔离子进程验证日志按次记录、并发完整性、体积分卷及 Windows 崩溃文件。
 */
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#ifdef _WIN32
#include <qt_windows.h>
#endif

namespace
{
QString probePath()
{
#ifdef _WIN32
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    return QFileInfo(QString::fromWCharArray(path)).dir().filePath("ApplicationLogProbe.exe");
#else
    return QFileInfo(QFileInfo("/proc/self/exe").symLinkTarget()).dir().filePath("ApplicationLogProbe");
#endif
}
QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void run(const QString &directory, const QString &mode, bool crashes = false)
{
    QProcess process;
    process.start(probePath(), {directory, mode});
    ASSERT_TRUE(process.waitForStarted());
    ASSERT_TRUE(process.waitForFinished(30000));
    if (crashes) EXPECT_NE(process.exitCode(), 0);
    else EXPECT_EQ(process.exitCode(), 0) << process.readAllStandardError().constData();
}
TEST(ApplicationLog, PerRunUnicodeContextAndThreadRecords)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    run(dir.path(), "normal");
    run(dir.path(), "normal");
    const auto files = QDir(dir.path()).entryList({"*.log"}, QDir::Files);
    ASSERT_EQ(files.size(), 2);
    for (const auto &file : files)
    {
        const auto data = read(dir.filePath(file));
        EXPECT_EQ(data.count("thread-record"), 200);
        EXPECT_TRUE(data.contains("封板诊断 UTF-8"));
        EXPECT_TRUE(data.contains("[WARN]"));
        EXPECT_TRUE(data.contains("[ERROR]"));
        EXPECT_TRUE(data.contains("pid="));
        EXPECT_TRUE(data.contains("tid="));
        EXPECT_TRUE(data.contains("application_log_probe.cpp:"));
        EXPECT_TRUE(data.contains("probe.end"));
    }
}
TEST(ApplicationLog, RotationAndRetentionOnlyRemoveOwnedLogs)
{
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString old = "ReverseModeler-20000101-120000-000-123.log";
    for (const auto &name : {old, QString("commands.log"), QString("notes.log"), QString("old-crash.dmp")})
    {
        QFile file(dir.filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("keep");
        file.flush();
        ASSERT_TRUE(file.setFileTime(QDateTime::currentDateTime().addDays(-31), QFileDevice::FileModificationTime));
    }
    run(dir.path(), "rotate");
    EXPECT_FALSE(QFile::exists(dir.filePath(old)));
    EXPECT_TRUE(QFile::exists(dir.filePath("commands.log")));
    EXPECT_TRUE(QFile::exists(dir.filePath("notes.log")));
    EXPECT_TRUE(QFile::exists(dir.filePath("old-crash.dmp")));
    const auto files = QDir(dir.path()).entryList({"ReverseModeler-*.log"}, QDir::Files);
    ASSERT_EQ(files.size(), 2);
    QByteArray all;
    for (const auto &file : files)
    {
        const auto data = read(dir.filePath(file));
        EXPECT_LE(data.size(), 10 * 1024 * 1024);
        all += data;
    }
    EXPECT_TRUE(all.contains("probe.end"));
}
#ifdef _WIN32
TEST(ApplicationLog, CrashesLeaveTextAndReadableMinidump)
{
    for (const auto &mode : {"terminate", "fatal", "abort", "seh"})
    {
        SCOPED_TRACE(mode);
        QTemporaryDir dir;
        ASSERT_TRUE(dir.isValid());
        run(dir.path(), mode, true);
        const auto reports = QDir(dir.path()).entryList({"*-crash.txt"}, QDir::Files);
        const auto dumps = QDir(dir.path()).entryList({"*-crash.dmp"}, QDir::Files);
        ASSERT_EQ(reports.size(), 1);
        ASSERT_EQ(dumps.size(), 1);
        const auto report = read(dir.filePath(reports.front()));
        EXPECT_TRUE(report.contains("[FATAL]"));
        EXPECT_TRUE(report.contains("error=0"));
        if (QString(mode) == "terminate") EXPECT_TRUE(report.contains("probe unhandled C++ exception"));
        if (QString(mode) == "seh") EXPECT_TRUE(report.contains("0xC0000005"));
        EXPECT_TRUE(read(dir.filePath(dumps.front())).startsWith("MDMP"));
        const auto logs = QDir(dir.path()).entryList({"*.log"}, QDir::Files);
        ASSERT_EQ(logs.size(), 1);
        EXPECT_TRUE(read(dir.filePath(logs.front())).contains("probe.begin"));
    }
}
#endif
} // namespace
