#include "ToolbarIni.h"

#include <utility>

namespace {

/// Names of items in Xournal++ and the ids of what does the same here
const std::pair<const char*, const char*> ITEMS[] = {
        {"NEW", "new"},
        {"OPEN", "open"},
        {"SAVE", "save"},
        {"SAVEPDF", "export"},
        {"PRINT", "print"},
        {"UNDO", "undo"},
        {"REDO", "redo"},
        {"CUT", "cut"},
        {"COPY", "copy"},
        {"PASTE", "paste"},
        {"SEARCH", "find"},
        {"DELETE", "delete"},
        {"ROTATION_SNAPPING", "snapRotation"},
        {"GRID_SNAPPING", "snapGrid"},
        {"PAIRED_PAGES", "pairedPages"},
        {"PRESENTATION_MODE", "present"},
        {"FULLSCREEN", "fullScreen"},
        {"SHOW_SIDEBAR", "sidebar"},
        {"MANAGE_TOOLBAR", "manageToolbars"},
        {"CUSTOMIZE_TOOLBAR", "customizeToolbars"},
        {"ZOOM_OUT", "zoomOut"},
        {"ZOOM_IN", "zoomIn"},
        {"ZOOM_FIT", "fitWidth"},
        {"ZOOM_100", "zoom100"},
        {"ZOOM_SLIDER", "zoomSlider"},
        {"GOTO_FIRST", "firstPage"},
        {"GOTO_BACK", "previousPage"},
        {"GOTO_PAGE", "goToPage"},
        {"GOTO_NEXT", "nextPage"},
        {"GOTO_LAST", "lastPage"},
        {"GOTO_PREVIOUS_LAYER", "previousLayer"},
        {"GOTO_NEXT_LAYER", "nextLayer"},
        {"GOTO_TOP_LAYER", "topLayer"},
        {"GOTO_NEXT_ANNOTATED_PAGE", "nextAnnotatedPage"},
        {"INSERT_NEW_PAGE", "insertPage"},
        {"DELETE_CURRENT_PAGE", "deletePage"},
        {"PEN", "pen"},
        {"ERASER", "eraser"},
        {"HIGHLIGHTER", "highlighter"},
        {"LASER_POINTER", "laserPen"},
        {"TEXT", "text"},
        {"MATH_TEX", "latex"},
        {"IMAGE", "imageTool"},
        {"DEFAULT_TOOL", "defaultTool"},
        {"SHAPE_RECOGNIZER", "shapeRecognizer"},
        {"SELECT_PDF_TEXT_LINEAR", "pdfText"},
        {"SELECT_PDF_TEXT_RECT", "pdfTextRect"},
        {"PDF_TOOL", "pdfTool"},
        {"DRAW", "drawingType"},
        {"DRAW_RECTANGLE", "drawRectangle"},
        {"DRAW_ELLIPSE", "drawEllipse"},
        {"DRAW_ARROW", "drawArrow"},
        {"DRAW_DOUBLE_ARROW", "drawDoubleArrow"},
        {"DRAW_COORDINATE_SYSTEM", "drawCoordinateSystem"},
        {"RULER", "drawLine"},
        {"DRAW_SPLINE", "drawSpline"},
        {"SELECT", "selectTool"},
        {"SELECT_REGION", "selectRegion"},
        {"SELECT_RECTANGLE", "selectRect"},
        {"SELECT_MULTILAYER_REGION", "selectRegionAllLayers"},
        {"SELECT_MULTILAYER_RECTANGLE", "selectRectAllLayers"},
        {"SELECT_OBJECT", "selectObject"},
        {"VERTICAL_SPACE", "verticalSpace"},
        {"PLAY_OBJECT", "playObject"},
        {"HAND", "hand"},
        {"SETSQUARE", "setsquare"},
        {"COMPASS", "compass"},
        {"SELECT_FONT", "font"},
        {"AUDIO_RECORDING", "record"},
        {"AUDIO_PAUSE_PLAYBACK", "audioPause"},
        {"AUDIO_STOP_PLAYBACK", "audioStop"},
        {"AUDIO_SEEK_FORWARDS", "audioSeekForwards"},
        {"AUDIO_SEEK_BACKWARDS", "audioSeekBackwards"},
        {"PAGE_SPIN", "page"},
        {"LAYER", "layer"},
        {"TOOL_FILL", "fill"},
        {"FILL_OPACITY", "fillOpacity"},
        {"COLOR_SELECT", "colorSelect"},
        {"TOGGLE_TOUCH_DRAWING", "fingerDraws"},
        {"VERY_FINE", "sizeVeryFine"},
        {"FINE", "sizeFine"},
        {"MEDIUM", "sizeMedium"},
        {"THICK", "sizeThick"},
        {"VERY_THICK", "sizeVeryThick"},
        {"PLAIN", "linePlain"},
        {"DASHED", "lineDashed"},
        {"DASH-/ DOTTED", "lineDashDotted"},
        {"DOTTED", "lineDotted"},
        {"SEPARATOR", "separator"},
        {"SPACER", "spacer"},
};

const QString UNKNOWN_PREFIX = QStringLiteral("xournalpp:");
const QString COLOR_PREFIX = QStringLiteral("color:");
const QString PLUGIN_PREFIX = QStringLiteral("Plugin::");

/// The key of a toolbar in the file: "top1" is "toolbarTop1"
QString keyOf(const QString& bar) { return QStringLiteral("toolbar") + bar.at(0).toUpper() + bar.mid(1); }

QString barOf(const QString& key) {
    if (!key.startsWith(u"toolbar") || key.size() < 8) {
        return {};
    }
    const QString bar = key.at(7).toLower() + key.mid(8);
    return ToolbarIni::barNames().contains(bar) ? bar : QString();
}

/// Names that items had in earlier versions of Xournal++ (ToolbarData::load)
QString currentName(const QString& name) {
    static const std::pair<const char*, const char*> RENAMED[] = {
            {"TWO_PAGES", "PAIRED_PAGES"},   {"RECSTOP", "AUDIO_RECORDING"},       {"HILIGHTER", "HIGHLIGHTER"},
            {"DRAW_CIRCLE", "DRAW_ELLIPSE"}, {"PEN_FILL_OPACITY", "FILL_OPACITY"},
    };
    for (const auto& [old, current]: RENAMED) {
        if (name == QLatin1StringView(old)) {
            return QString::fromLatin1(current);
        }
    }
    return name;
}

}  // namespace

namespace ToolbarIni {

const QStringList& barNames() {
    static const QStringList names = {
            QStringLiteral("top1"),   QStringLiteral("top2"),   QStringLiteral("left1"),   QStringLiteral("left2"),
            QStringLiteral("right1"), QStringLiteral("right2"), QStringLiteral("bottom1"), QStringLiteral("bottom2"),
            QStringLiteral("float1"), QStringLiteral("float2"), QStringLiteral("float3"),  QStringLiteral("float4"),
    };
    return names;
}

QString toId(const QString& xournalppName) {
    const QString name = currentName(xournalppName);
    for (const auto& [ini, id]: ITEMS) {
        if (name == QLatin1StringView(ini)) {
            return QString::fromLatin1(id);
        }
    }
    if (name.startsWith(u"COLOR(") && name.endsWith(u')')) {
        return COLOR_PREFIX + name.mid(6, name.size() - 7);
    }
    if (name.startsWith(u"QT(") && name.endsWith(u')')) {
        return name.mid(3, name.size() - 4);
    }
    if (name.startsWith(PLUGIN_PREFIX)) {
        return name;
    }
    return UNKNOWN_PREFIX + name;
}

QString fromId(const QString& id) {
    for (const auto& [ini, known]: ITEMS) {
        if (id == QLatin1StringView(known)) {
            return QString::fromLatin1(ini);
        }
    }
    if (id.startsWith(COLOR_PREFIX)) {
        return QStringLiteral("COLOR(%1)").arg(id.mid(COLOR_PREFIX.size()));
    }
    if (id.startsWith(UNKNOWN_PREFIX)) {
        return id.mid(UNKNOWN_PREFIX.size());
    }
    if (id.startsWith(PLUGIN_PREFIX)) {
        return id;
    }
    return QStringLiteral("QT(%1)").arg(id);
}

QList<Config> parse(const QByteArray& data, bool predefined, const QString& language) {
    QList<Config> configs;
    // "name[de_CH]" wins over "name[de]", which wins over "name"
    const QString shortLanguage = language.section(u'_', 0, 0);
    int nameRank = 0;
    int nextColor = 0;

    const QStringList lines = QString::fromUtf8(data).split(u'\n');
    for (const QString& raw: lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(u'#')) {
            continue;
        }
        if (line.startsWith(u'[') && line.endsWith(u']')) {
            Config config;
            config.id = line.mid(1, line.size() - 2);
            config.name = config.id;
            config.predefined = predefined;
            configs.append(config);
            nameRank = 0;
            nextColor = 0;
            continue;
        }
        const qsizetype equals = line.indexOf(u'=');
        if (equals <= 0 || configs.isEmpty()) {
            continue;
        }
        Config& config = configs.last();
        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();

        if (key == u"name" || (key.startsWith(u"name[") && key.endsWith(u']'))) {
            const QString locale = key.size() > 4 ? key.mid(5, key.size() - 6) : QString();
            const int rank = locale.isEmpty() ? 1 : locale == language ? 3 : locale == shortLanguage ? 2 : 0;
            if (rank > nameRank) {
                config.name = value;
                nameRank = rank;
            }
            continue;
        }
        const QString bar = barOf(key);
        if (bar.isEmpty()) {
            continue;
        }
        QStringList items;
        const QStringList names = value.split(u',', Qt::SkipEmptyParts);
        for (const QString& entry: names) {
            QString name = entry.trimmed();
            if (name.isEmpty()) {
                continue;
            }
            // Colours given by their value are from files of old versions: they stand for the next colour of the
            // palette
            if (name.startsWith(u"COLOR(0x")) {
                name = QStringLiteral("COLOR(%1)").arg(nextColor++);
            }
            items.append(toId(name));
        }
        config.bars[bar] = items;
    }
    return configs;
}

QByteArray write(const QList<Config>& configs) {
    QString out =
            QStringLiteral("# Qournal toolbar configuration, in the format of the toolbar.ini of Xournal++.\n"
                           "# Items Xournal++ does not have are written as QT(name).\n");
    for (const Config& config: configs) {
        if (config.predefined) {
            continue;
        }
        out += QStringLiteral("\n[%1]\n").arg(config.id);
        for (const QString& bar: barNames()) {
            const QStringList items = config.bars.value(bar);
            if (items.isEmpty()) {
                continue;
            }
            QStringList names;
            for (const QString& id: items) {
                names.append(fromId(id));
            }
            out += keyOf(bar) + u'=' + names.join(u',') + u'\n';
        }
        out += QStringLiteral("name=%1\n").arg(config.name);
    }
    return out.toUtf8();
}

}  // namespace ToolbarIni
