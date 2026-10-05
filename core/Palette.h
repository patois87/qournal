/*
 * Qournal
 *
 * Colour palettes in the format of GIMP (.gpl), as Xournal++ uses them for the colours of the tools
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QByteArray>
#include <QColor>
#include <QHash>
#include <QList>
#include <QString>

struct Palette {
    struct NamedColor {
        QString name;
        QColor color;
    };

    QList<NamedColor> colors;
    QHash<QString, QString> header;  ///< e.g. "Name"

    QString name() const { return header.value(QStringLiteral("Name")); }

    /// The palette of Xournal++ (palettes/xournalpp.gpl)
    static Palette defaultPalette();

    /**
     * Reads a palette.
     * @param error receives a description of what is wrong with the file
     * @return false if the data is not a palette with at least one colour
     */
    static bool parse(const QByteArray& data, Palette& palette, QString* error = nullptr);
    static bool load(const QString& path, Palette& palette, QString* error = nullptr);
};
