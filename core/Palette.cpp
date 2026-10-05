#include "Palette.h"

#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>

namespace {

const char DEFAULT_PALETTE[] = "GIMP Palette\n"
                               "Name: Xournal++ Palette\n"
                               "#\n"
                               "255 225 107 Yellow\n"
                               "255 161  84 Orange\n"
                               "205 158 247 Purple\n"
                               "155 219  77 Light Green\n"
                               "100 186 255 Light Blue\n"
                               "128 128 128 Gray\n"
                               " 58 145   4 Dark Green\n"
                               "237  83  83 Red\n"
                               "  0  46 153 Dark Blue\n"
                               "  0   0   0 Black\n"
                               "255 255 255 White\n";

}  // namespace

Palette Palette::defaultPalette() {
    Palette palette;
    parse(QByteArray(DEFAULT_PALETTE), palette);
    return palette;
}

bool Palette::parse(const QByteArray& data, Palette& palette, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    const QStringList lines = QString::fromUtf8(data).split(u'\n');
    if (lines.isEmpty() || lines.first().trimmed() != u"GIMP Palette") {
        return fail(QCoreApplication::translate("Palette",
                "A palette file has to start with the line \"GIMP Palette\""));
    }

    // "red green blue name": three numbers, the name is optional
    static const QRegularExpression colorLine(QStringLiteral(R"(^\s*(\d+)\s+(\d+)\s+(\d+)(?:\s+(.*))?$)"));
    static const QRegularExpression headerLine(QStringLiteral(R"(^([^:]*):(.*)$)"));

    Palette result;
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString line = lines[i].endsWith(u'\r') ? lines[i].chopped(1) : lines[i];
        if (line.trimmed().isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (const auto match = colorLine.match(line); match.hasMatch()) {
            const int red = match.captured(1).toInt();
            const int green = match.captured(2).toInt();
            const int blue = match.captured(3).toInt();
            if (red > 255 || green > 255 || blue > 255) {
                return fail(QCoreApplication::translate("Palette",
                        "Line %1: the values of a colour have to be between 0 and 255").arg(i + 1));
            }
            result.colors.append({match.captured(4).trimmed(), QColor(red, green, blue)});
        } else if (const auto header = headerLine.match(line); header.hasMatch()) {
            result.header.insert(header.captured(1).trimmed(), header.captured(2).trimmed());
        } else {
            return fail(QCoreApplication::translate("Palette",
                    "Line %1 is neither a colour nor a header: \"%2\"").arg(i + 1).arg(line.trimmed()));
        }
    }
    if (result.colors.isEmpty()) {
        return fail(QCoreApplication::translate("Palette", "The palette does not contain any colour"));
    }
    palette = result;
    return true;
}

bool Palette::load(const QString& path, Palette& palette, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QCoreApplication::translate("Palette", "Could not read \"%1\": %2").arg(path, file.errorString());
        }
        return false;
    }
    return parse(file.readAll(), palette, error);
}
