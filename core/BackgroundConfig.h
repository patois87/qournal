/*
 * Qournal
 *
 * The configuration of a page background, as stored in the "config" attribute of the .xopp format
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QHash>
#include <QString>

/// The "config" attribute of a background: comma separated key=value pairs
class BackgroundConfig {
public:
    explicit BackgroundConfig(const QString& config) {
        const auto entries = QStringView(config).split(u',', Qt::SkipEmptyParts);
        for (const QStringView entry: entries) {
            const qsizetype pos = entry.lastIndexOf(u'=');
            if (pos >= 0) {
                m_values.insert(entry.left(pos).toString(), entry.mid(pos + 1).toString());
            }
        }
    }

    double number(const QString& key, double fallback) const {
        bool ok = false;
        const double value = m_values.value(key).toDouble(&ok);
        return ok ? value : fallback;
    }

    QColor color(const QString& key, const QColor& fallback) const {
        bool ok = false;
        const uint value = m_values.value(key).toUInt(&ok, 16);
        return ok ? QColor::fromRgb(value & 0xffffffU) : fallback;
    }

private:
    QHash<QString, QString> m_values;
};
