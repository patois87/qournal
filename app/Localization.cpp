#include "Localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QSettings>
#include <QTranslator>

namespace {

const QString LANGUAGE_KEY = QStringLiteral("ui/language");
const QString DIRECTORY = QStringLiteral(":/i18n");
const QString PREFIX = QStringLiteral("qournal_");

}  // namespace

QString Localization::language() const { return QSettings().value(LANGUAGE_KEY).toString(); }

void Localization::setLanguage(const QString& language) {
    if (language != this->language()) {
        QSettings().setValue(LANGUAGE_KEY, language);
        emit languageChanged();
    }
}

QStringList Localization::available() {
    QStringList codes;
    const QStringList files = QDir(DIRECTORY).entryList({PREFIX + QStringLiteral("*.qm")}, QDir::Files);
    for (const QString& file: files) {
        codes.append(file.mid(PREFIX.size(), file.size() - PREFIX.size() - 3));
    }
    return codes;
}

QVariantList Localization::languages() const {
    QVariantList list;
    list.append(QVariantMap{{QStringLiteral("code"), QStringLiteral("en")},
                            {QStringLiteral("name"), QStringLiteral("English")}});
    for (const QString& code: available()) {
        const QLocale locale(code);
        QString name = locale.nativeLanguageName();
        if (code.contains(u'_')) {
            name += QStringLiteral(" (%1)").arg(locale.nativeTerritoryName());
        }
        if (name.isEmpty()) {
            name = code;
        }
        list.append(QVariantMap{{QStringLiteral("code"), code}, {QStringLiteral("name"), name}});
    }
    return list;
}

QString Localization::install(QCoreApplication* app) {
    const QString chosen = QSettings().value(LANGUAGE_KEY).toString();
    if (chosen == u"en") {
        return chosen;
    }
    auto* translator = new QTranslator(app);
    // With the language of the system the translation is found by its list of preferred languages
    const bool loaded = chosen.isEmpty() ? translator->load(QLocale(), QStringLiteral("qournal"),
                                                            QStringLiteral("_"), DIRECTORY) :
                                           translator->load(PREFIX + chosen, DIRECTORY);
    if (!loaded) {
        delete translator;
        return QStringLiteral("en");
    }
    QCoreApplication::installTranslator(translator);
    // The texts of Qt itself (the buttons of dialogs): they come with the application, because Qt is not
    // installed where it runs
    auto* ofQt = new QTranslator(app);
    if (ofQt->load(QLocale(translator->language()), QStringLiteral("qtbase"), QStringLiteral("_"),
                   QStringLiteral(":/i18n-qt"))) {
        QCoreApplication::installTranslator(ofQt);
    } else {
        delete ofQt;
    }
    return translator->language();
}
