#include "ColorPalette.h"

#include <QCoreApplication>

ColorPalette::ColorPalette(QObject* parent): QObject(parent), m_palette(Palette::defaultPalette()) {}

namespace {
// The names of the colours of the palette of Xournal++, for the translations. A palette file has its names in
// one language; those that are among these are shown in the language of the user
[[maybe_unused]] const char* const COLOR_NAMES[] = {
        QT_TRANSLATE_NOOP("Color", "Yellow"),     QT_TRANSLATE_NOOP("Color", "Orange"),
        QT_TRANSLATE_NOOP("Color", "Purple"),     QT_TRANSLATE_NOOP("Color", "Light Green"),
        QT_TRANSLATE_NOOP("Color", "Light Blue"), QT_TRANSLATE_NOOP("Color", "Gray"),
        QT_TRANSLATE_NOOP("Color", "Dark Green"), QT_TRANSLATE_NOOP("Color", "Red"),
        QT_TRANSLATE_NOOP("Color", "Dark Blue"),  QT_TRANSLATE_NOOP("Color", "Black"),
        QT_TRANSLATE_NOOP("Color", "White"),      QT_TRANSLATE_NOOP("Color", "Green"),
        QT_TRANSLATE_NOOP("Color", "Blue"),       QT_TRANSLATE_NOOP("Color", "Magenta"),
        QT_TRANSLATE_NOOP("Color", "Light Gray"), QT_TRANSLATE_NOOP("Color", "Dark Gray"),
};
}  // namespace

QVariantList ColorPalette::colors() const {
    QVariantList list;
    for (const Palette::NamedColor& entry: m_palette.colors) {
        const QString name = QCoreApplication::translate("Color", entry.name.toUtf8().constData());
        list.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("color"), entry.color}});
    }
    return list;
}

bool ColorPalette::load(const QUrl& url) {
    // Non-file URLs (e.g. content:// on Android) are passed on as they are: QFile understands them there
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    if (!Palette::load(path, m_palette, &error)) {
        emit loadFailed(error);
        return false;
    }
    emit changed();
    return true;
}

void ColorPalette::reset() {
    m_palette = Palette::defaultPalette();
    emit changed();
}
