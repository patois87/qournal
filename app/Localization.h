/*
 * Qournal
 *
 * The language of the user interface. The translations are built into the application.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <QtQml/qqmlregistration.h>

class QCoreApplication;

class Localization: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// The code of the chosen language ("de", "pt_BR", ...); empty for the language of the system.
    /// A change takes effect when the application is started the next time
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    /// The languages there are translations for: entries with "code" and "name", English first
    Q_PROPERTY(QVariantList languages READ languages CONSTANT)

public:
    using QObject::QObject;

    QString language() const;
    void setLanguage(const QString& language);
    QVariantList languages() const;

    /// The codes of the languages with a translation
    static QStringList available();
    /// Installs the translation of the chosen language. @return the code of the language in use, "en" if none
    static QString install(QCoreApplication* app);

signals:
    void languageChanged();
};
