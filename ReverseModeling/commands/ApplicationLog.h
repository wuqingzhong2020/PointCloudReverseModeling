/*
 * 文件描述：
 * 进程运行日志、Qt 消息接收及 Windows 崩溃诊断入口；文件日志不依赖工作台生命周期。
 */
#pragma once
#include <QString>
#include <QtLogging>

namespace reverse::logging
{
// 在 QApplication 之前启动，捕获启动及退出阶段；空目录自动选择 output/log 或用户数据目录。
bool start(const QString &directory = {});
QString directory();
QString filePath();
void write(QtMsgType level, const char *category, const QString &message) noexcept;
void stop() noexcept;
} // namespace reverse::logging
