/*
 * Qournal
 *
 * The toolbar configurations of Xournal++ (toolbar.ini): named arrangements of items in up to twelve toolbars.
 * Ported from Xournal++ 1.3 (ToolbarModel, ToolbarData).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>

namespace ToolbarIni {

/// One arrangement. The items are the ids this application uses for them, see toId()
struct Config {
    QString id;    ///< the name of the group in the file; unique
    QString name;  ///< shown to the user
    bool predefined = false;
    /// The items of each toolbar, by the names of barNames()
    QMap<QString, QStringList> bars;
};

/// The toolbars a configuration can fill: top1, top2, left1, left2, right1, right2, bottom1, bottom2, float1 to 4
const QStringList& barNames();

/**
 * Reads a toolbar.ini.
 * @param language for the translated names in the file ("name[de]"), e.g. "de_CH"
 */
QList<Config> parse(const QByteArray& data, bool predefined, const QString& language = QString());

/// Writes configurations as Xournal++ does; the predefined ones are left out
QByteArray write(const QList<Config>& configs);

/**
 * The id of an item of Xournal++ here. Items that have no counterpart here keep their name, as
 * "xournalpp:NAME": they are not shown, but are written back.
 */
QString toId(const QString& xournalppName);

/// The name of an item in the file. Items Xournal++ does not have are written as "QT(id)"
QString fromId(const QString& id);

}  // namespace ToolbarIni
