#include "Licenses.h"

#include <QDir>
#include <QFile>

namespace {
const QString FOLDER = QStringLiteral(":/licenses");
const QString OVERVIEW = QStringLiteral("THIRD-PARTY.md");
const QString OWN = QStringLiteral("LICENSE");
}  // namespace

QStringList Licenses::documents() const {
    QStringList names =
            QDir(FOLDER + QStringLiteral("/licenses")).entryList(QDir::Files, QDir::Name | QDir::IgnoreCase);
    names.prepend(OWN);
    names.prepend(OVERVIEW);
    return names;
}

QString Licenses::text(const QString& document) const {
    if (!documents().contains(document)) {
        return {};
    }
    const bool top = document == OVERVIEW || document == OWN;
    QFile file(FOLDER + (top ? QStringLiteral("/") : QStringLiteral("/licenses/")) + document);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}
