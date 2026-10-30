/*
 * 文件描述：
 * 最近文件按成功导入时间排序，去重并保留十条；失效路径保留提示，清空仅删除历史记录。
 */
#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QSettings>

#include "workbench/RecentFilesMenu.h"
#include "workbench/WorkbenchTheme.h"

namespace reverse::workbench
{
namespace
{
constexpr qsizetype maximumRecentFiles = 10;
const QString settingsKey = QStringLiteral("files/recentPointClouds");

QString normalizedPath(const QString &path)
{
    const QFileInfo info(path);
    const auto canonical = info.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? info.absoluteFilePath() : canonical);
}
} // namespace

RecentFilesMenu::RecentFilesMenu(QWidget *parent) : QMenu(QStringLiteral("最近打开文件"), parent)
{
    setObjectName(QStringLiteral("menu.recentFiles"));
    setIcon(WorkbenchTheme::icon(QStringLiteral("folder")));
    setToolTipsVisible(true);
    connect(this, &QMenu::aboutToShow, this, &RecentFilesMenu::rebuild);
    rebuild();
}

void RecentFilesMenu::recordOpened(const QString &path)
{
    if (path.isEmpty())
    {
        return;
    }
    const auto normalized = normalizedPath(path);
    QSettings settings;
    auto files = settings.value(settingsKey).toStringList();
#if defined(_WIN32)
    constexpr auto pathCase = Qt::CaseInsensitive;
#else
    constexpr auto pathCase = Qt::CaseSensitive;
#endif
    files.removeIf([&normalized](const QString &entry)
                   { return entry.isEmpty() || normalizedPath(entry).compare(normalized, pathCase) == 0; });
    files.prepend(normalized);
    files = files.mid(0, maximumRecentFiles);
    settings.setValue(settingsKey, files);
}

void RecentFilesMenu::rebuild()
{
    clear();
    QSettings settings;
    const auto files = settings.value(settingsKey).toStringList().mid(0, maximumRecentFiles);
    if (files.isEmpty())
    {
        addAction(QStringLiteral("暂无最近打开文件"))->setEnabled(false);
    }
    for (qsizetype i = 0; i < files.size(); ++i)
    {
        const auto &path = files[i];
        const auto displayPath = QDir::toNativeSeparators(path);
        const bool available = QFileInfo(path).isFile();
        auto title = QStringLiteral("%1 | %2").arg(i + 1).arg(displayPath);
        if (!available)
        {
            title += QStringLiteral("（文件不存在）");
        }
        title.replace(QStringLiteral("&"), QStringLiteral("&&"));
        auto *action = addAction(title);
        action->setObjectName(QStringLiteral("action.recentFile.%1").arg(i + 1));
        action->setData(path);
        action->setToolTip(displayPath);
        action->setEnabled(available);
    }
    addSeparator();
    auto *clearHistory = addAction(QStringLiteral("清空记录"));
    clearHistory->setObjectName(QStringLiteral("action.clearRecentFiles"));
    clearHistory->setEnabled(!files.isEmpty());
    connect(clearHistory, &QAction::triggered, this,
            []
            {
                QSettings historySettings;
                historySettings.remove(settingsKey);
            });
}

} // namespace reverse::workbench
