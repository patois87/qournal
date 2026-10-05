/*
 * Qournal
 *
 * The look of the user interface: light or dark colours and the icon theme
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

#include <QtQml/qqmlregistration.h>

class Theme: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// "system" (follows the desktop; black and white on electronic paper), "light", "dark" or "eink"
    Q_PROPERTY(QString mode READ mode WRITE setMode NOTIFY changed)
    /// Whether the colours are dark, whatever the mode
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    /// Whether the colours are those for electronic paper: black and white, without grays
    Q_PROPERTY(bool eink READ eink NOTIFY changed)
    /// The colours of the palette by the names of its roles ("window", "text", ...), and "disabledText"
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    /// "lucide", "color", or "none" for buttons with text
    Q_PROPERTY(QString iconTheme READ iconTheme WRITE setIconTheme NOTIFY changed)
    /// The icons by their names (those of Xournal++ without "xopp-"); empty if icons are not shown
    Q_PROPERTY(QVariantMap icons READ icons NOTIFY changed)
    /// Whether this build can show the icons, which are SVG files
    Q_PROPERTY(bool iconsSupported READ iconsSupported CONSTANT)

public:
    explicit Theme(QObject* parent = nullptr);

    QString mode() const { return m_mode; }
    void setMode(const QString& mode);
    bool dark() const;
    bool eink() const { return einkFor(m_mode); }
    /// Whether a mode means the colours for electronic paper on this device
    static bool einkFor(const QString& mode);
    QVariantMap colors() const;
    QString iconTheme() const { return m_iconTheme; }
    void setIconTheme(const QString& theme);
    QVariantMap icons() const;
    bool iconsSupported() const;

signals:
    void changed();

private:
    void applyPalette();

    QString m_mode = QStringLiteral("system");
    QString m_iconTheme = QStringLiteral("lucide");
};
