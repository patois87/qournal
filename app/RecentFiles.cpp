#include "RecentFiles.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QSettings>
#include <QUrl>

namespace {

const QString FILES_KEY = QStringLiteral("recentFiles/files");

/// Paths can contain characters that are not allowed in keys of the settings
QString pageKey(const QString& path) {
    const QByteArray hash = QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex();
    return QStringLiteral("recentFiles/pages/") + QString::fromLatin1(hash);
}

}  // namespace

RecentFiles::RecentFiles(QObject* parent): QObject(parent) {}

QStringList RecentFiles::files() const {
    QStringList existing;
    const QStringList stored = QSettings().value(FILES_KEY).toStringList();
    for (const QString& path: stored) {
        if (QFileInfo::exists(path)) {
            existing.append(path);
        }
    }
    return existing;
}

void RecentFiles::add(const QString& path) {
    if (path.isEmpty()) {
        return;
    }
    QSettings settings;
    QStringList stored = settings.value(FILES_KEY).toStringList();
    stored.removeAll(path);
    stored.prepend(path);
    while (stored.size() > MAX_FILES) {
        settings.remove(pageKey(stored.takeLast()));
    }
    settings.setValue(FILES_KEY, stored);
    emit changed();
}

void RecentFiles::clear() {
    QSettings settings;
    settings.remove(QStringLiteral("recentFiles"));
    emit changed();
}

int RecentFiles::pageOf(const QString& path) const { return QSettings().value(pageKey(path), 0).toInt(); }

void RecentFiles::setPage(const QString& path, int page) {
    if (!path.isEmpty()) {
        QSettings().setValue(pageKey(path), page);
    }
}

QString RecentFiles::fileName(const QString& path) const { return QFileInfo(path).fileName(); }

QUrl RecentFiles::locationUrl(const QString& location) const {
    return location.contains(u"://") ? QUrl(location) : QUrl::fromLocalFile(location);
}
