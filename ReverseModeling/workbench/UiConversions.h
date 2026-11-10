/* 文件描述：工作台 UTF-8 文字与本机文件路径的边界转换。 */
#pragma once
#include <filesystem>
#include <string_view>
#include <QString>
#include <QByteArray>

namespace reverse::workbench
{
inline QString uiText(std::string_view value)
{
    return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
}

inline std::filesystem::path nativePath(const QString &value)
{
#if defined(_WIN32)
    return std::filesystem::path(value.toStdWString());
#else
    const auto utf8 = value.toUtf8();
    return std::filesystem::path(std::string(utf8.constData(), static_cast<std::size_t>(utf8.size())));
#endif
}

} // namespace reverse::workbench
