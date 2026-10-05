/*
 * Qournal
 *
 * The documents that were opened last, and the page each of them was left at
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QStringList>
#include <QUrl>

#include <QtQml/qqmlregistration.h>

class RecentFiles: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// The paths of the files, the most recent one first; files that do not exist any more are left out
    Q_PROPERTY(QStringList files READ files NOTIFY changed)

public:
    static constexpr int MAX_FILES = 10;

    explicit RecentFiles(QObject* parent = nullptr);

    QStringList files() const;

    Q_INVOKABLE void add(const QString& path);
    Q_INVOKABLE void clear();
    /// The page a document was left at, 0 if it is not known
    Q_INVOKABLE int pageOf(const QString& path) const;
    Q_INVOKABLE void setPage(const QString& path, int page);
    Q_INVOKABLE QString fileName(const QString& path) const;
    /**
     * The URL of a location as the list has it: a path of a file, or a URL (content:// on Android). Not made by
     * putting "file://" in front of the path: "C:/Users/..." became the path "//c/Users/..." of a network drive
     * with that, and the document could not be opened on Windows
     */
    Q_INVOKABLE QUrl locationUrl(const QString& location) const;

signals:
    void changed();
};
