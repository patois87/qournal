#include "Theme.h"

#include <QColor>
#include <QDir>
#include <QGuiApplication>
#include <QImageReader>
#include <QPalette>
#include <QStyleHints>
#include <utility>

#include "Platform.h"

namespace {

struct Role {
    const char* name;
    const char* light;
    const char* dark;
};

// The colours of the Fusion style, and a dark counterpart
const Role ROLES[] = {
        {"window", "#efefef", "#353535"},      {"windowText", "#000000", "#ffffff"},
        {"base", "#ffffff", "#232323"},        {"alternateBase", "#f7f7f7", "#2d2d2d"},
        {"text", "#000000", "#ffffff"},        {"button", "#efefef", "#454545"},
        {"buttonText", "#000000", "#ffffff"},  {"brightText", "#ffffff", "#ff5555"},
        {"highlight", "#308cc6", "#2a82da"},   {"highlightedText", "#ffffff", "#ffffff"},
        {"light", "#ffffff", "#5f5f5f"},       {"midlight", "#cacaca", "#4b4b4b"},
        {"mid", "#a0a0a0", "#5a5a5a"},         {"dark", "#9f9f9f", "#1e1e1e"},
        {"shadow", "#767676", "#141414"},      {"toolTipBase", "#ffffdc", "#454545"},
        {"toolTipText", "#000000", "#ffffff"}, {"placeholderText", "#80000000", "#80ffffff"},
        {"link", "#0000ff", "#5aa9ff"},        {"disabledText", "#808080", "#7f7f7f"},
};

/// The colour of a role on electronic paper: black on white, with black lines. Grays are dithered there and look
/// dirty, and dark areas leave traces
const char* einkColor(const char* role) {
    for (const char* black:
         {"windowText", "text", "buttonText", "highlight", "mid", "dark", "shadow", "toolTipText", "link"}) {
        if (qstrcmp(role, black) == 0) {
            return "#000000";
        }
    }
    if (qstrcmp(role, "placeholderText") == 0 || qstrcmp(role, "disabledText") == 0) {
        return "#707070";
    }
    return "#ffffff";
}

}  // namespace

Theme::Theme(QObject* parent): QObject(parent) {
    connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this, &Theme::changed);
    connect(this, &Theme::changed, this, &Theme::applyPalette);
    applyPalette();
}

void Theme::applyPalette() {
    // The palette of the application, from which every window, menu and dialog has its colours. The main window
    // sets its own in QML, but menus and dialogs are windows of their own for the palette and did not get the
    // colours of disabled controls from it: disabled entries looked like enabled ones
    const bool isDark = dark();
    const bool isEink = eink();
    QPalette palette = QGuiApplication::palette();
    static const std::pair<const char*, QPalette::ColorRole> PALETTE_ROLES[] = {
            {"window", QPalette::Window},
            {"windowText", QPalette::WindowText},
            {"base", QPalette::Base},
            {"alternateBase", QPalette::AlternateBase},
            {"text", QPalette::Text},
            {"button", QPalette::Button},
            {"buttonText", QPalette::ButtonText},
            {"brightText", QPalette::BrightText},
            {"highlight", QPalette::Highlight},
            {"highlightedText", QPalette::HighlightedText},
            {"light", QPalette::Light},
            {"midlight", QPalette::Midlight},
            {"mid", QPalette::Mid},
            {"dark", QPalette::Dark},
            {"shadow", QPalette::Shadow},
            {"toolTipBase", QPalette::ToolTipBase},
            {"toolTipText", QPalette::ToolTipText},
            {"placeholderText", QPalette::PlaceholderText},
            {"link", QPalette::Link},
    };
    QColor disabledText;
    for (const Role& role: ROLES) {
        const QColor color(QLatin1StringView(isEink ? einkColor(role.name) : isDark ? role.dark : role.light));
        if (qstrcmp(role.name, "disabledText") == 0) {
            disabledText = color;
        }
        for (const auto& [name, paletteRole]: PALETTE_ROLES) {
            if (qstrcmp(role.name, name) == 0) {
                palette.setColor(QPalette::All, paletteRole, color);
            }
        }
    }
    for (const QPalette::ColorRole role: {QPalette::Text, QPalette::WindowText, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, disabledText);
    }
    QGuiApplication::setPalette(palette);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // Electronic paper is light whatever the system says: also what Qt draws by the scheme (the status bar)
    if (isEink && QGuiApplication::styleHints()->colorScheme() != Qt::ColorScheme::Light) {
        QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Light);
    }
#endif
    Platform::setSystemBarsOnLight(!isDark);
}

void Theme::setMode(const QString& mode) {
    if (m_mode == mode || (mode != u"system" && mode != u"light" && mode != u"dark" && mode != u"eink")) {
        return;
    }
    m_mode = mode;
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // For the styles that have a dark variant of their own (Material, iOS) and for the window decoration
    QGuiApplication::styleHints()->setColorScheme(mode == u"dark"                   ? Qt::ColorScheme::Dark :
                                                  mode == u"light" || einkFor(mode) ? Qt::ColorScheme::Light :
                                                                                      Qt::ColorScheme::Unknown);
#endif
    emit changed();
}

bool Theme::einkFor(const QString& mode) { return mode == u"eink" || (mode == u"system" && Platform::isEinkDevice()); }

bool Theme::dark() const {
    if (eink()) {
        return false;
    }
    if (m_mode == u"system") {
        return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
    }
    return m_mode == u"dark";
}

QVariantMap Theme::colors() const {
    QVariantMap colors;
    const bool isDark = dark();
    const bool isEink = eink();
    for (const Role& role: ROLES) {
        colors.insert(QString::fromLatin1(role.name), QColor(QLatin1StringView(isEink ? einkColor(role.name) :
                                                                               isDark ? role.dark :
                                                                                        role.light)));
    }
    return colors;
}

void Theme::setIconTheme(const QString& theme) {
    if (m_iconTheme != theme && (theme == u"lucide" || theme == u"color" || theme == u"none")) {
        m_iconTheme = theme;
        emit changed();
    }
}

bool Theme::iconsSupported() const { return QImageReader::supportedImageFormats().contains("svg"); }

QVariantMap Theme::icons() const {
    QVariantMap icons;
    if (m_iconTheme == u"none" || !iconsSupported()) {
        return icons;
    }
    // The icons are drawn for a light or for a dark background. What a theme lacks comes from the Lucide icons
    const QString variant = dark() ? QStringLiteral("dark") : QStringLiteral("light");
    for (const QString& theme: {QStringLiteral("lucide"), m_iconTheme}) {
        QString dir = QStringLiteral(":/icons/%1-%2").arg(theme, variant);
        // On electronic paper the Lucide icons are black, if the build has them so
        if (eink() && theme == u"lucide" && QDir(QStringLiteral(":/icons/lucide-eink")).exists()) {
            dir = QStringLiteral(":/icons/lucide-eink");
        }
        const QStringList files = QDir(dir).entryList({QStringLiteral("xopp-*.svg")}, QDir::Files);
        for (const QString& file: files) {
            icons.insert(file.mid(5, file.size() - 9), QStringLiteral("qrc") + dir + u'/' + file);
        }
    }
    return icons;
}
