/*
 * Qournal
 *
 * The licenses of the application and of the parts in it that are made by others, to show them in the application
 * (the stores have no place for files next to it): THIRD-PARTY.md, LICENSE and the folder licenses/ of the sources
 * are resources
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include <QtQml/qqmlregistration.h>

class Licenses: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// The names of the documents: the overview first, then the license of the application, then the others
    Q_PROPERTY(QStringList documents READ documents CONSTANT)

public:
    explicit Licenses(QObject* parent = nullptr): QObject(parent) {}

    QStringList documents() const;
    /// The text of one of documents()
    Q_INVOKABLE QString text(const QString& document) const;
};
