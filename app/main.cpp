/*
 * Qournal
 *
 * @license GNU GPLv2 or later
 */

#include <QCommandLineParser>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFileOpenEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <cstdio>

#include "CrashHandler.h"
#include "Document.h"
#include "Export.h"
#include "Localization.h"
#include "Platform.h"
#include "Theme.h"
#include "XoppLoader.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace Qt::StringLiterals;

namespace {

/// Documents the system asks to be opened while the application runs: a double click in the file manager of macOS,
/// "open in" on iOS. They are passed on to the window, which asks about unsaved changes first
class FileOpenFilter: public QObject {
public:
    explicit FileOpenFilter(QQmlApplicationEngine* engine): QObject(engine), m_engine(engine) {}

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::FileOpen && !m_engine->rootObjects().isEmpty()) {
            const QUrl url = static_cast<QFileOpenEvent*>(event)->url();
            QMetaObject::invokeMethod(m_engine->rootObjects().first(), "openRequested", Q_ARG(QVariant, url));
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    QQmlApplicationEngine* m_engine;
};

/// Converts a document without any window, with the options of Xournal++ (--create-pdf, --create-img)
int exportFile(const QString& xoppPath, const QString& target, bool pdf, const QCommandLineParser& parser) {
    Document doc;
    QString error;
    if (!loadXopp(xoppPath, doc, &error)) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }

    ExportOptions options;
    const QString pages = parser.value(u"export-range"_s);
    if (!pages.isEmpty() && !ElementRange::parse(pages, static_cast<int>(doc.pages.size()), options.pages, &error)) {
        std::fprintf(stderr, "--export-range: %s\n", qPrintable(error));
        return 1;
    }
    const QString layers = parser.value(u"export-layer-range"_s);
    if (!layers.isEmpty()) {
        size_t maxLayers = 1;
        for (const Page& page: doc.pages) {
            maxLayers = std::max(maxLayers, page.layers.size());
        }
        if (!ElementRange::parse(layers, static_cast<int>(maxLayers), options.layers, &error)) {
            std::fprintf(stderr, "--export-layer-range: %s\n", qPrintable(error));
            return 1;
        }
    }
    if (parser.isSet(u"export-no-background"_s)) {
        options.background = ExportOptions::Background::None;
    } else if (parser.isSet(u"export-no-ruling"_s)) {
        options.background = ExportOptions::Background::NoRuling;
    }
    options.progressiveLayers = parser.isSet(u"export-layers-progressively"_s);
    if (parser.isSet(u"export-png-dpi"_s)) {
        options.dpi = parser.value(u"export-png-dpi"_s).toDouble();
    }
    options.width = parser.value(u"export-png-width"_s).toInt();
    options.height = parser.value(u"export-png-height"_s).toInt();
    if (options.dpi <= 0) {
        std::fprintf(stderr, "--export-png-dpi needs a positive number\n");
        return 1;
    }

    const bool ok =
            pdf ? Export::toPdf(doc, target, options, &error) : Export::toImages(doc, target, options, nullptr, &error);
    if (!ok) {
        std::fprintf(stderr, "%s\n", qPrintable(error));
        return 1;
    }
    return 0;
}

/// Set by the test that starts the application (tests/CMakeLists.txt): it uses settings of its own, quits after
/// the start and fails if QML warned about something
/// Copies a folder with what is in it, unless the target exists
void copyFolder(const QString& from, const QString& to) {
    if (from.isEmpty() || to.isEmpty() || !QDir(from).exists() || QDir(to).exists()) {
        return;
    }
    QDirIterator files(from, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString source = files.next();
        const QString target = to + source.mid(from.size());
        QDir().mkpath(QFileInfo(target).path());
        QFile::copy(source, target);
    }
}

/**
 * The application was called "xournalpp-qt" until it got a name of its own. At the first start with the new
 * name the settings, the toolbars, the plugins and the recordings of the old one are taken over; the old ones
 * stay where they are. Not on Android, where the application with the new name is another one for the system
 */
void takeOverFromOldName() {
    const QStandardPaths::StandardLocation folders[] = {
            QStandardPaths::AppConfigLocation, QStandardPaths::AppLocalDataLocation, QStandardPaths::AppDataLocation};
    QSettings settings;
    if (!settings.allKeys().isEmpty()) {
        return;
    }
    const QString name = QCoreApplication::applicationName();
    const QString organization = QCoreApplication::organizationName();
    QStringList targets;
    for (const auto folder: folders) {
        targets.append(QStandardPaths::writableLocation(folder));
    }
    QCoreApplication::setApplicationName(u"xournalpp-qt"_s);
    QCoreApplication::setOrganizationName(u"xournalpp"_s);
    QStringList sources;
    for (const auto folder: folders) {
        sources.append(QStandardPaths::writableLocation(folder));
    }
    QCoreApplication::setApplicationName(name);
    QCoreApplication::setOrganizationName(organization);

    const QSettings old(u"xournalpp"_s, u"xournalpp-qt"_s);
    const QStringList keys = old.allKeys();
    if (keys.isEmpty()) {
        return;
    }
    for (const QString& key: keys) {
        QVariant value = old.value(key);
        // The toolbars that come with the application were named after it
        if (value.typeId() == QMetaType::QString && value.toString().startsWith(u"Xournal++ Qt")) {
            value = u"Qournal"_s + value.toString().mid(12);
        }
        settings.setValue(key, value);
    }
    for (qsizetype i = 0; i < sources.size(); ++i) {
        copyFolder(sources[i], targets[i]);
    }
}

bool isStartTest() { return qEnvironmentVariableIsSet("QOURNAL_START_TEST"); }

int qmlWarnings = 0;
QtMessageHandler defaultMessageHandler = nullptr;

void countQmlWarnings(QtMsgType type, const QMessageLogContext& context, const QString& message) {
    if (type != QtDebugMsg && type != QtInfoMsg &&
        (message.startsWith(u"qrc:"_s) || message.startsWith(u"file:"_s) || qstrcmp(context.category, "qml") == 0)) {
        ++qmlWarnings;
    }
    defaultMessageHandler(type, context, message);
}

}  // namespace

int main(int argc, char* argv[]) {
#ifdef Q_OS_WIN
    // The program has no console of its own. Started from one, the messages of the export options go there
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
        std::freopen("CONOUT$", "w", stdout);
        std::freopen("CONOUT$", "w", stderr);
    }
#endif
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    // In the AppImage there is no theme of Qt for the desktop (they need its libraries), and the dialogs for files
    // would be the ones of Qt Quick, which do not look like the desktop and have odd colours in the dark mode:
    // the desktop is asked for its own dialogs, through its portal
    if (qEnvironmentVariableIsSet("APPIMAGE") && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORMTHEME")) {
        qputenv("QT_QPA_PLATFORMTHEME", "xdgdesktopportal");
    }
#endif
    QGuiApplication app(argc, argv);
    // The name of the desktop file: Wayland compositors find the icon of the window by it
    QGuiApplication::setDesktopFileName(u"ch.vereo.qournal"_s);
    QGuiApplication::setWindowIcon(QIcon(u":/qournal.png"_s));
    QGuiApplication::setApplicationName(u"qournal"_s);
    QGuiApplication::setOrganizationName(u"qournal"_s);
    QGuiApplication::setOrganizationDomain(u"vereo.ch"_s);
    QGuiApplication::setApplicationVersion(QStringLiteral(APP_VERSION));

    if (isStartTest()) {
        QStandardPaths::setTestModeEnabled(true);
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope,
                           QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation));
        defaultMessageHandler = qInstallMessageHandler(countQmlWarnings);
    } else {
        takeOverFromOldName();
    }

    CrashHandler::install();
    Localization::install(&app);
    // The same look on every desktop, with colours that can be light or dark (see Theme). Mobile platforms keep
    // their own style, except on electronic paper: the style of Android has shadows, coloured areas and animations,
    // which such screens show badly, and it does not take the colours of the theme
    bool ownStyle = true;
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    ownStyle = Theme::einkFor(QSettings().value(u"ui/theme"_s, u"system"_s).toString());
#endif
    if (ownStyle && qEnvironmentVariableIsEmpty("QT_QUICK_CONTROLS_STYLE")) {
        QQuickStyle::setStyle(u"Fusion"_s);
    }

    QCommandLineParser parser;
    parser.setApplicationDescription(u"Qournal"_s);
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addPositionalArgument(u"file"_s, u"The .xopp or PDF file to open"_s, u"[file]"_s);
    parser.addOptions({
            {{u"p"_s, u"create-pdf"_s}, u"Export <file> as PDF and exit"_s, u"pdf"_s},
            {{u"i"_s, u"create-img"_s},
             u"Export <file> as image files and exit, one per page. The extension chooses the format: .png or .svg"_s,
             u"image"_s},
            {u"export-no-background"_s, u"Export without the background of the pages"_s},
            {u"export-no-ruling"_s, u"Export without the ruling of the paper"_s},
            {u"export-layers-progressively"_s, u"Export one page per layer, each showing the layers up to it"_s},
            {u"export-range"_s, u"Only export the pages of the range, e.g. \"1-3,5,7-\""_s, u"range"_s},
            {u"export-layer-range"_s, u"Only export the layers of the range, e.g. \"1-3,5,7-\""_s, u"range"_s},
            {u"export-png-dpi"_s, u"Resolution of PNG files (default: 300)"_s, u"dpi"_s},
            {u"export-png-width"_s, u"Width of PNG files in pixels"_s, u"pixels"_s},
            {u"export-png-height"_s, u"Height of PNG files in pixels"_s, u"pixels"_s},
            {{u"n"_s, u"page"_s}, u"Show this page after opening the file"_s, u"number"_s},
    });
    parser.process(app);

    const QStringList files = parser.positionalArguments();
    for (const QString& option: {u"create-pdf"_s, u"create-img"_s}) {
        if (parser.isSet(option)) {
            if (files.isEmpty()) {
                std::fprintf(stderr, "--%s needs a file to export\n", qPrintable(option));
                return 1;
            }
            return exportFile(files.first(), parser.value(option), option == u"create-pdf", parser);
        }
    }

    QQmlApplicationEngine engine;
    app.installEventFilter(new FileOpenFilter(&engine));
    QVariantMap properties;
    if (const QUrl document = Platform::startupDocument(); document.isValid()) {
        properties.insert(u"initialFile"_s, document);
    } else if (!files.isEmpty()) {
        properties.insert(u"initialFile"_s,
                          QUrl::fromUserInput(files.first(), QDir::currentPath(), QUrl::AssumeLocalFile));
    }
    if (parser.isSet(u"page"_s)) {
        properties.insert(u"initialPage"_s, parser.value(u"page"_s).toInt());
    }
    // A crash of the last session is told about once
    properties.insert(u"crashLogs"_s, CrashHandler::takeNewLogs());
    engine.setInitialProperties(properties);
    QObject::connect(
            &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
            Qt::QueuedConnection);
    engine.loadFromModule("Qournal", "Main");
    if (isStartTest()) {
        QTimer::singleShot(2000, &app, [] {
            std::fprintf(stderr, "%d warnings of QML\n", qmlWarnings);
            QCoreApplication::exit(qmlWarnings > 0 ? 2 : 0);
        });
    }

    return QGuiApplication::exec();
}
