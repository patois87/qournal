/*
 * Qournal
 *
 * The toolbar configurations: the predefined ones (those of this application and those of Xournal++) and the
 * ones of the user, which are kept in a toolbar.ini in the format of Xournal++
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QUrl>
#include <QVariant>

#include <QtQml/qqmlregistration.h>

#include "ToolbarIni.h"

class ToolbarModel: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// All configurations: maps with id, name and predefined
    Q_PROPERTY(QVariantList configs READ configs NOTIFY configsChanged)
    /// The id of the configuration in use. Empty, or one that does not exist: the fallback is used
    Q_PROPERTY(QString current READ current WRITE setCurrent NOTIFY currentChanged)
    /// The configuration for a fresh installation; it follows the layout (desktop or tablet)
    Q_PROPERTY(QString fallback READ fallback WRITE setFallback NOTIFY currentChanged)
    /// The id of the configuration that is shown: current, or the fallback
    Q_PROPERTY(QString shown READ shown NOTIFY currentChanged)
    Q_PROPERTY(QString shownName READ shownName NOTIFY barsChanged)
    Q_PROPERTY(bool shownPredefined READ shownPredefined NOTIFY barsChanged)
    /// The items of the toolbars of the shown configuration: lists of ids by "top1", "left1", ..., see ToolbarIni
    Q_PROPERTY(QVariantMap bars READ bars NOTIFY barsChanged)
    /// The file with the configurations of the user
    Q_PROPERTY(QString file READ file WRITE setFile NOTIFY fileChanged)
    /// The file with the predefined configurations of Xournal++
    Q_PROPERTY(QString predefinedFile READ predefinedFile WRITE setPredefinedFile NOTIFY fileChanged)

public:
    /// Ids of the configurations of this application
    static const QString DESKTOP_ID;
    static const QString TABLET_ID;

    explicit ToolbarModel(QObject* parent = nullptr);

    QVariantList configs() const;
    QString current() const { return m_current; }
    void setCurrent(const QString& id);
    QString fallback() const { return m_fallback; }
    void setFallback(const QString& id);
    QString shown() const;
    QString shownName() const;
    bool shownPredefined() const;
    QVariantMap bars() const;
    QString file() const { return m_file; }
    void setFile(const QString& file);
    QString predefinedFile() const { return m_predefinedFile; }
    void setPredefinedFile(const QString& file);

    /// Reads the predefined configurations and those of the user
    Q_INVOKABLE void load();
    /**
     * Changes a toolbar of the shown configuration. A predefined configuration cannot be changed: a copy of it
     * is made, changed and shown instead.
     */
    Q_INVOKABLE void setItems(const QString& bar, const QStringList& items);
    /// Makes a copy of the shown configuration and shows it. Returns its id
    Q_INVOKABLE QString copyShown();
    /// Deletes the shown configuration, if it is one of the user
    Q_INVOKABLE void removeShown();
    Q_INVOKABLE void renameShown(const QString& name);
    /// Adds a configuration of the user and shows it. Returns its id
    Q_INVOKABLE QString add(const QString& name, const QVariantMap& bars);
    /// Takes over the configurations of a toolbar.ini of Xournal++. Returns how many were added
    Q_INVOKABLE int importFile(const QUrl& file);
    /// The toolbar.ini of an installation of Xournal++ of this user, empty if there is none
    Q_INVOKABLE QUrl xournalppFile() const;

signals:
    void configsChanged();
    void currentChanged();
    void barsChanged();
    void fileChanged();

private:
    const ToolbarIni::Config* find(const QString& id) const;
    ToolbarIni::Config* shownConfig();
    QString uniqueId(const QString& wanted) const;
    void save() const;

    QList<ToolbarIni::Config> m_configs;
    QString m_current;
    QString m_fallback;
    QString m_file;
    QString m_predefinedFile;
};
