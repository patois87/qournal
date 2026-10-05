/*
 * Qournal
 *
 * The colours offered for the tools: the palette of Xournal++, or one loaded from a .gpl file
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include "Palette.h"

class ColorPalette: public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString name READ name NOTIFY changed)
    /// The colours as a list of maps with "name" and "color"
    Q_PROPERTY(QVariantList colors READ colors NOTIFY changed)

public:
    explicit ColorPalette(QObject* parent = nullptr);

    QString name() const { return m_palette.name(); }
    QVariantList colors() const;

    /// Loads a .gpl file. The palette stays as it is if the file cannot be used: loadFailed() tells why
    Q_INVOKABLE bool load(const QUrl& url);
    /// Back to the palette of Xournal++
    Q_INVOKABLE void reset();

signals:
    void changed();
    void loadFailed(const QString& message);

private:
    Palette m_palette;
};
