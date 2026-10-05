#include "PluginApi.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QImageReader>
#include <QRegularExpression>
#include <QScreen>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <iterator>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include "LuaBridge.h"
#include "Renderer.h"
#include "SplineSegment.h"
#include "TextBlock.h"

namespace {

QString tr(const char* text) { return QCoreApplication::translate("PluginController", text); }

/// The tools of Xournal++ in the order of its enumeration, which gives the values of app.C.Tool_...
struct ToolName {
    const char* name;
    int tool;  ///< the tool here, negative if it is something else
};
const ToolName TOOLS[] = {
        {"none", -1},
        {"pen", PageCanvas::Pen},
        {"eraser", PageCanvas::Eraser},
        {"highlighter", PageCanvas::Highlighter},
        {"text", PageCanvas::Text},
        {"image", PageCanvas::Image},
        {"selectRect", PageCanvas::SelectRect},
        {"selectRegion", PageCanvas::SelectRegion},
        {"selectMultiLayerRect", PageCanvas::SelectRect},
        {"selectMultiLayerRegion", PageCanvas::SelectRegion},
        {"selectObject", PageCanvas::SelectObject},
        {"playObject", PageCanvas::PlayObject},
        {"verticalSpace", PageCanvas::VerticalSpace},
        {"hand", PageCanvas::Hand},
        {"drawRect", -1},
        {"drawEllipse", -1},
        {"drawArrow", -1},
        {"drawDoubleArrow", -1},
        {"drawCoordinateSystem", -1},
        {"showFloatingToolbox", -1},
        {"drawSpline", -1},
        {"selectPdfTextLinear", PageCanvas::SelectPdfTextLinear},
        {"selectPdfTextRect", PageCanvas::SelectPdfTextRect},
        {"laserPointerPen", PageCanvas::LaserPen},
        {"laserPointerHighlighter", PageCanvas::LaserHighlighter},
        {"link", PageCanvas::Link},
        {"latex", PageCanvas::Latex},
};
constexpr int XOJ_TOOL_MULTILAYER_RECT = 8;
constexpr int XOJ_TOOL_MULTILAYER_REGION = 9;
constexpr int XOJ_TOOL_FLOATING_TOOLBOX = 19;

const char* const SIZE_NAMES[] = {"veryThin", "thin", "medium", "thick", "veryThick", "none"};
const char* const ERASER_TYPE_NAMES[] = {"none", "default", "whiteout", "deleteStroke"};
const char* const ORDER_CHANGE_NAMES[] = {"bringToFront", "bringForward", "sendBackward", "sendToBack"};
const char* const ALIGNMENT_NAMES[] = {"left", "center", "right"};
// In the order of PageCanvas::DrawingType
const char* const DRAWING_TYPE_NAMES[] = {
        "default",         "line", "rectangle", "ellipse", "arrow", "doubleArrow", "drawCoordinateSystem", "spline",
        "strokeRecognizer"};

/// The tool of Xournal++ that stands for a tool here
int xojTool(const PageCanvas& canvas) {
    const int tool = canvas.tool();
    if (canvas.selectAllLayers() && tool == PageCanvas::SelectRect) {
        return XOJ_TOOL_MULTILAYER_RECT;
    }
    if (canvas.selectAllLayers() && tool == PageCanvas::SelectRegion) {
        return XOJ_TOOL_MULTILAYER_REGION;
    }
    for (int i = 0; i < static_cast<int>(std::size(TOOLS)); ++i) {
        if (TOOLS[i].tool == tool) {
            return i;
        }
    }
    return 0;
}

QColor colorOf(const QVariant& value, const QColor& fallback) {
    bool ok = false;
    const qlonglong rgb = value.toLongLong(&ok);
    return value.isValid() && ok ? QColor::fromRgb(static_cast<QRgb>(rgb & 0xffffff)) : fallback;
}

qlonglong rgbOf(const QColor& color) { return static_cast<qlonglong>(color.rgb() & 0xffffffU); }

QVariantMap rectMap(const QRectF& rect) {
    return {{QStringLiteral("x"), rect.x()},
            {QStringLiteral("y"), rect.y()},
            {QStringLiteral("width"), rect.width()},
            {QStringLiteral("height"), rect.height()}};
}

/// The entries of a Lua table with integer keys, in the order of the keys
QList<QPair<int, QVariant>> indexed(const QVariant& table) {
    QList<QPair<int, QVariant>> entries;
    if (table.typeId() == QMetaType::QVariantList) {
        const QVariantList list = table.toList();
        for (qsizetype i = 0; i < list.size(); ++i) {
            entries.append({static_cast<int>(i + 1), list[i]});
        }
    } else {
        const QVariantMap map = table.toMap();
        for (auto it = map.begin(); it != map.end(); ++it) {
            bool ok = false;
            const int key = it.key().toInt(&ok);
            if (ok) {
                entries.append({key, it.value()});
            }
        }
        std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }
    return entries;
}

QString argumentText(const QVariantList& args, qsizetype index, const QString& fallback = {}) {
    return index < args.size() && args[index].isValid() ? args[index].toString() : fallback;
}

/// "plain" for a solid line, as Xournal++ names it
QString lineStyleName(const QString& style) {
    return style.isEmpty() || style == u"solid" ? QStringLiteral("plain") : style;
}

/// Calls a function of PluginApi: the arguments come from the Lua stack, the results go there
int callFunction(lua_State* L) {
    auto* controller = static_cast<PluginController*>(lua_touserdata(L, lua_upvalueindex(1)));
    const int plugin = static_cast<int>(lua_tointeger(L, lua_upvalueindex(2)));
    const int function = static_cast<int>(lua_tointeger(L, lua_upvalueindex(3)));
    int results = 0;
    bool failed = false;
    {
        // Nothing of this block may be alive when lua_error() leaves the function
        QVariantList args;
        const int count = lua_gettop(L);
        for (int i = 1; i <= count; ++i) {
            args.append(LuaBridge::toVariant(L, i));
        }
        try {
            PluginApi api(*controller, plugin);
            const QVariantList values = (api.*PluginApi::FUNCTIONS[function].function)(args);
            for (const QVariant& value: values) {
                LuaBridge::push(L, value);
                ++results;
            }
        } catch (const PluginApi::Error& error) {
            const QByteArray message =
                    QByteArray("app.") + PluginApi::FUNCTIONS[function].name + ": " + error.message.toUtf8();
            lua_pushlstring(L, message.constData(), static_cast<size_t>(message.size()));
            failed = true;
        }
    }
    if (failed) {
        return lua_error(L);
    }
    return results;
}

void setConstants(lua_State* L, const char* prefix, const char* const* names, int count, int firstValue = 0) {
    // As "Prefix_name", and as a table "Prefix" with the names, which some plugins use
    lua_newtable(L);
    for (int i = 0; i < count; ++i) {
        lua_pushinteger(L, firstValue + i);
        lua_setfield(L, -3, (QByteArray(prefix) + '_' + names[i]).constData());
        lua_pushinteger(L, firstValue + i);
        lua_setfield(L, -2, names[i]);
    }
    lua_setfield(L, -2, prefix);
}

}  // namespace

const PluginApi::Entry PluginApi::FUNCTIONS[] = {
        {"msgbox", &PluginApi::msgbox},
        {"openDialog", &PluginApi::openDialog},
        {"saveAs", &PluginApi::saveAs},
        {"fileDialogSave", &PluginApi::fileDialogSave},
        {"getFilePath", &PluginApi::getFilePath},
        {"fileDialogOpen", &PluginApi::fileDialogOpen},
        {"registerUi", &PluginApi::registerUi},
        {"registerPlaceholder", &PluginApi::registerPlaceholder},
        {"setPlaceholderValue", &PluginApi::setPlaceholderValue},
        {"getActionState", &PluginApi::getActionState},
        {"changeActionState", &PluginApi::changeActionState},
        {"activateAction", &PluginApi::activateAction},
        {"uiAction", &PluginApi::uiAction},
        {"sidebarAction", &PluginApi::sidebarAction},
        {"layerAction", &PluginApi::layerAction},
        {"getSidebarPageNo", &PluginApi::getSidebarPageNo},
        {"setSidebarPageNo", &PluginApi::setSidebarPageNo},
        {"showFloatingToolbox", &PluginApi::showFloatingToolbox},
        {"changeToolColor", &PluginApi::changeToolColor},
        {"getColorPalette", &PluginApi::getColorPalette},
        {"getToolInfo", &PluginApi::getToolInfo},
        {"getFonts", &PluginApi::getFonts},
        {"getFont", &PluginApi::getFont},
        {"setFont", &PluginApi::setFont},
        {"getDocumentStructure", &PluginApi::getDocumentStructure},
        {"getPageLabel", &PluginApi::getPageLabel},
        {"changeCurrentPageBackground", &PluginApi::changeCurrentPageBackground},
        {"changeBackgroundPdfPageNr", &PluginApi::changeBackgroundPdfPageNr},
        {"scrollToPage", &PluginApi::scrollToPage},
        {"scrollToPos", &PluginApi::scrollToPos},
        {"getScrollPos", &PluginApi::getScrollPos},
        {"setCurrentPage", &PluginApi::setCurrentPage},
        {"setPageSize", &PluginApi::setPageSize},
        {"setCurrentLayer", &PluginApi::setCurrentLayer},
        {"setLayerVisibility", &PluginApi::setLayerVisibility},
        {"setCurrentLayerName", &PluginApi::setCurrentLayerName},
        {"setBackgroundName", &PluginApi::setBackgroundName},
        {"getDisplayDpi", &PluginApi::getDisplayDpi},
        {"getZoom", &PluginApi::getZoom},
        {"setZoom", &PluginApi::setZoom},
        {"refreshPage", &PluginApi::refreshPage},
        {"export", &PluginApi::exportDocument},
        {"openFile", &PluginApi::openFile},
        {"getFolder", &PluginApi::getFolder},
        {"glib_rename", &PluginApi::rename},
        {"addStrokes", &PluginApi::addStrokes},
        {"addSplines", &PluginApi::addSplines},
        {"addTexts", &PluginApi::addTexts},
        {"addLinks", &PluginApi::addLinks},
        {"addImages", &PluginApi::addImages},
        {"getStrokes", &PluginApi::getStrokes},
        {"getTexts", &PluginApi::getTexts},
        {"getLinks", &PluginApi::getLinks},
        {"getImages", &PluginApi::getImages},
        {"addToSelection", &PluginApi::addToSelection},
        {"clearSelection", &PluginApi::clearSelection},
};
const int PluginApi::FUNCTION_COUNT = static_cast<int>(std::size(PluginApi::FUNCTIONS));

void PluginApi::open(lua_State* L, PluginController* controller, int plugin) {
    lua_newtable(L);
    for (int i = 0; i < FUNCTION_COUNT; ++i) {
        lua_pushlightuserdata(L, controller);
        lua_pushinteger(L, plugin);
        lua_pushinteger(L, i);
        lua_pushcclosure(L, callFunction, 3);
        lua_setfield(L, -2, FUNCTIONS[i].name);
    }

    // The constants of Xournal++
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(std::size(TOOLS)); ++i) {
        lua_pushinteger(L, i);
        lua_setfield(L, -2, (QByteArray("Tool_") + TOOLS[i].name).constData());
    }
    setConstants(L, "ToolSize", SIZE_NAMES, static_cast<int>(std::size(SIZE_NAMES)));
    setConstants(L, "EraserType", ERASER_TYPE_NAMES, static_cast<int>(std::size(ERASER_TYPE_NAMES)));
    setConstants(L, "OrderChange", ORDER_CHANGE_NAMES, static_cast<int>(std::size(ORDER_CHANGE_NAMES)));
    setConstants(L, "Alignment", ALIGNMENT_NAMES, static_cast<int>(std::size(ALIGNMENT_NAMES)));
    lua_setfield(L, -2, "C");

    lua_setglobal(L, "app");
}

PageCanvas& PluginApi::canvas() const {
    if (!c.m_canvas) {
        throw Error{tr("There is no document")};
    }
    return *c.m_canvas;
}

QVariant PluginApi::ref(const PageCanvas::ElementRef& element) {
    c.m_refs.push_back(element);
    // The number of the reference, in the form of a pointer as plugins expect it
    return QVariant::fromValue(reinterpret_cast<void*>(static_cast<quintptr>(c.m_refs.size())));
}

QVariantList PluginApi::refs(const std::vector<PageCanvas::ElementRef>& elements) {
    QVariantList list;
    for (const PageCanvas::ElementRef& element: elements) {
        list.append(ref(element));
    }
    return list;
}

// Dialogs

QVariantList PluginApi::msgbox(const QVariantList& args) {
    QVariantList buttons;
    for (const auto& [id, text]: indexed(args.value(1))) {
        buttons.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), text.toString()}});
    }
    const int request = c.addRequest(m_plugin, {});
    emit c.dialogRequested(request, plugin().name, argumentText(args, 0), buttons, false);
    return {c.waitFor(request).toInt()};
}

QVariantList PluginApi::openDialog(const QVariantList& args) {
    QVariantList buttons;
    for (const auto& [id, text]: indexed(args.value(1))) {
        buttons.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("text"), text.toString()}});
    }
    const int request = c.addRequest(m_plugin, argumentText(args, 2));
    emit c.dialogRequested(request, plugin().name, argumentText(args, 0), buttons, args.value(3).toBool());
    return {};
}

QVariantList PluginApi::saveAs(const QVariantList& args) {
    const int request = c.addRequest(m_plugin, {});
    emit c.fileDialogRequested(request, true, argumentText(args, 0, QStringLiteral("Untitled")), {});
    return {c.waitFor(request).toString()};
}

QVariantList PluginApi::fileDialogSave(const QVariantList& args) {
    const int request = c.addRequest(m_plugin, argumentText(args, 0));
    emit c.fileDialogRequested(request, true, argumentText(args, 1, QStringLiteral("Untitled")), {});
    return {};
}

QVariantList PluginApi::getFilePath(const QVariantList& args) {
    const int request = c.addRequest(m_plugin, {});
    emit c.fileDialogRequested(request, false, {}, args.value(0).toStringList());
    return {c.waitFor(request).toString()};
}

QVariantList PluginApi::fileDialogOpen(const QVariantList& args) {
    const int request = c.addRequest(m_plugin, argumentText(args, 0));
    emit c.fileDialogRequested(request, false, {}, args.value(1).toStringList());
    return {};
}

// User interface

QVariantList PluginApi::registerUi(const QVariantList& args) {
    if (!plugin().inInitUi) {
        throw Error{tr("registerUi needs to be called within initUi()")};
    }
    const QVariantMap options = args.value(0).toMap();
    PluginController::Entry entry;
    entry.plugin = m_plugin;
    entry.callback = options.value(QStringLiteral("callback")).toString();
    if (entry.callback.isEmpty()) {
        throw Error{tr("The option \"callback\" is missing")};
    }
    entry.menu = options.value(QStringLiteral("menu")).toString();
    entry.parentPath = options.value(QStringLiteral("parentPath")).toString();
    entry.accelerator = options.value(QStringLiteral("accelerator")).toString();
    entry.toolbarId = options.value(QStringLiteral("toolbarId")).toString();
    entry.iconName = options.value(QStringLiteral("iconName")).toString();
    if (options.contains(QStringLiteral("mode"))) {
        entry.mode = options.value(QStringLiteral("mode")).toLongLong();
    }
    c.m_entries.push_back(entry);
    return {QVariantMap{{QStringLiteral("menuId"), static_cast<qlonglong>(c.m_entries.size() - 1)}}};
}

QVariantList PluginApi::registerPlaceholder(const QVariantList& args) {
    const QString id = QStringLiteral("Plugin::") + argumentText(args, 0);
    if (!c.m_placeholders.contains(id)) {
        c.m_placeholders.insert(id, QVariantMap{{QStringLiteral("description"), argumentText(args, 1)},
                                                {QStringLiteral("value"), QString()}});
        emit c.placeholdersChanged();
    }
    return {};
}

QVariantList PluginApi::setPlaceholderValue(const QVariantList& args) {
    QString id = argumentText(args, 0);
    if (!id.startsWith(u"Plugin::")) {
        id.prepend(QStringLiteral("Plugin::"));
    }
    if (!c.m_placeholders.contains(id)) {
        qWarning("setPlaceholderValue: placeholder id '%s' does not exist", qPrintable(id));
        return {};
    }
    QVariantMap placeholder = c.m_placeholders.value(id).toMap();
    placeholder.insert(QStringLiteral("value"), argumentText(args, 1));
    c.m_placeholders.insert(id, placeholder);
    emit c.placeholdersChanged();
    return {};
}

QVariantList PluginApi::getActionState(const QVariantList& args) { return {actionState(argumentText(args, 0))}; }

QVariantList PluginApi::changeActionState(const QVariantList& args) {
    runAction(argumentText(args, 0), args.value(1));
    return {};
}

QVariantList PluginApi::activateAction(const QVariantList& args) {
    runAction(argumentText(args, 0), args.value(1));
    return {};
}

QVariantList PluginApi::uiAction(const QVariantList& args) {
    const QVariantMap options = args.value(0).toMap();
    const QString action = options.value(QStringLiteral("action")).toString();
    if (action.isEmpty()) {
        throw Error{tr("The option \"action\" is missing")};
    }
    legacyAction(action, options.value(QStringLiteral("enabled"), true).toBool());
    return {};
}

void PluginApi::legacyAction(const QString& name, bool enabled) {
    // The names of the actions before Xournal++ 1.3: "ACTION_TOOL_PEN", "ACTION_SIZE_FINE", "ACTION_RULER", ...
    static const QHash<QString, QString> renamed = {
            {QStringLiteral("NEW"), QStringLiteral("new-file")},
            {QStringLiteral("SETTINGS"), QStringLiteral("preferences")},
            {QStringLiteral("GOTO_BACK"), QStringLiteral("goto-previous")},
            {QStringLiteral("TOOL_DEFAULT"), QStringLiteral("select-default-tool")},
            {QStringLiteral("NEW_LAYER"), QStringLiteral("layer-new-above-current")},
            {QStringLiteral("NEW_LAYER_BELOW_CURRENT"), QStringLiteral("layer-new-below-current")},
            {QStringLiteral("DELETE_LAYER"), QStringLiteral("layer-delete")},
            {QStringLiteral("MERGE_LAYER_DOWN"), QStringLiteral("layer-merge-down")},
            {QStringLiteral("GOTO_NEXT_LAYER"), QStringLiteral("layer-goto-next")},
            {QStringLiteral("GOTO_PREVIOUS_LAYER"), QStringLiteral("layer-goto-previous")},
            {QStringLiteral("GOTO_TOP_LAYER"), QStringLiteral("layer-goto-top")},
            {QStringLiteral("RENAME_LAYER"), QStringLiteral("layer-rename")},
            {QStringLiteral("TOOL_DRAW_RECT"), QStringLiteral("tool-draw-rectangle")},
            {QStringLiteral("RULER"), QStringLiteral("tool-draw-line")},
            {QStringLiteral("SHAPE_RECOGNIZER"), QStringLiteral("tool-draw-shape-recognizer")},
            {QStringLiteral("VIEW_PAIRED_PAGES"), QStringLiteral("paired-pages-mode")},
            {QStringLiteral("VIEW_PRESENTATION_MODE"), QStringLiteral("presentation-mode")},
    };
    // Actions that are switched on or off
    static const QStringList switches = {
            QStringLiteral("setsquare"),
            QStringLiteral("compass"),
            QStringLiteral("tool-fill"),
            QStringLiteral("tool-pen-fill"),
            QStringLiteral("tool-highlighter-fill"),
            QStringLiteral("zoom-fit"),
            QStringLiteral("paired-pages-mode"),
            QStringLiteral("presentation-mode"),
            QStringLiteral("fullscreen"),
            QStringLiteral("show-sidebar"),
            QStringLiteral("audio-pause-playback"),
            QStringLiteral("audio-record"),
            QStringLiteral("grid-snapping"),
            QStringLiteral("rotation-snapping"),
    };
    QString key = name.startsWith(u"ACTION_") ? name.mid(7) : name;

    // A tool
    if (key.startsWith(u"TOOL_")) {
        const QString tool = key.mid(5).remove(u'_');
        for (int i = 0; i < static_cast<int>(std::size(TOOLS)); ++i) {
            if (tool.compare(QLatin1StringView(TOOLS[i].name), Qt::CaseInsensitive) == 0 && TOOLS[i].tool >= 0) {
                runAction(QStringLiteral("select-tool"), i);
                return;
            }
        }
    }
    // A size: "SIZE_FINE", "TOOL_PEN_SIZE_VERY_THICK"
    static const QRegularExpression size(
            QStringLiteral("^(TOOL_(?:PEN|ERASER|HIGHLIGHTER)_)?SIZE_(VERY_FINE|FINE|MEDIUM|THICK|VERY_THICK)$"));
    if (const auto match = size.match(key); match.hasMatch()) {
        static const QStringList sizes = {QStringLiteral("VERY_FINE"), QStringLiteral("FINE"), QStringLiteral("MEDIUM"),
                                          QStringLiteral("THICK"), QStringLiteral("VERY_THICK")};
        const QString action = match.captured(1).isEmpty() ?
                                       QStringLiteral("tool-size") :
                                       match.captured(1).toLower().replace(u'_', u'-') + QStringLiteral("size");
        runAction(action, sizes.indexOf(match.captured(2)));
        return;
    }
    const QString action = renamed.value(key, key.toLower().replace(u'_', u'-'));
    const bool isSwitch = switches.contains(action) || action.startsWith(u"tool-draw-");
    runAction(action, isSwitch ? QVariant(enabled) : QVariant());
}

QVariantList PluginApi::sidebarAction(const QVariantList& args) {
    // The pages are what the sidebar shows
    static const QHash<QString, QString> actions = {
            {QStringLiteral("MOVE_UP"), QStringLiteral("move-page-towards-beginning")},
            {QStringLiteral("MOVE_DOWN"), QStringLiteral("move-page-towards-end")},
            {QStringLiteral("COPY"), QStringLiteral("duplicate-page")},
            {QStringLiteral("DELETE"), QStringLiteral("delete-page")},
            {QStringLiteral("NEW_BEFORE"), QStringLiteral("new-page-before")},
            {QStringLiteral("NEW_AFTER"), QStringLiteral("new-page-after")},
    };
    const QString action = actions.value(argumentText(args, 0));
    if (action.isEmpty()) {
        throw Error{tr("Unknown action \"%1\"").arg(argumentText(args, 0))};
    }
    runAction(action, {});
    return {};
}

QVariantList PluginApi::layerAction(const QVariantList& args) {
    legacyAction(argumentText(args, 0), true);
    return {};
}

QVariantList PluginApi::getSidebarPageNo(const QVariantList&) { return {1}; }

QVariantList PluginApi::setSidebarPageNo(const QVariantList& args) {
    emit c.actionRequested(QStringLiteral("sidebar-page"), args.value(0).toInt());
    return {};
}

QVariantList PluginApi::showFloatingToolbox(const QVariantList& args) {
    emit c.floatingToolboxRequested(QPointF(args.value(0).toDouble(), args.value(1).toDouble()));
    return {};
}

// Actions

void PluginApi::runAction(const QString& name, const QVariant& state) {
    PageCanvas& v = canvas();
    const bool hasState = state.isValid();
    // The new state of a switch: the given one, or the opposite of the current one
    auto toggled = [&](bool current) { return hasState ? state.toBool() : !current; };
    auto drawingType = [&](PageCanvas::DrawingType type) {
        if (v.tool() != PageCanvas::Pen && v.tool() != PageCanvas::Highlighter) {
            v.setTool(PageCanvas::Pen);
        }
        v.setDrawingType(toggled(v.drawingType() == type) ? type : PageCanvas::Freehand);
    };
    auto geometryTool = [&](PageCanvas::GeometryToolType type) {
        v.setGeometryTool(toggled(v.geometryTool() == type) ? type : PageCanvas::NoGeometryTool);
    };
    auto toolSetting = [&](PageCanvas::Tool tool, const char* key, const QVariant& value) {
        v.setToolInfo(tool, {{QString::fromLatin1(key), value}});
    };
    const int page = v.currentPage();
    const int layer = v.currentLayer();
    const int layerCount = static_cast<int>(v.layers().size());
    auto annotatedPage = [&](int step) {
        for (int i = page + step; i >= 0 && i < v.pageCount(); i += step) {
            for (const Layer& l: v.document().pages[static_cast<size_t>(i)].layers) {
                if (!l.elements.empty()) {
                    v.setCurrentPage(i);
                    return;
                }
            }
        }
    };

    if (name == u"undo") {
        v.undo();
    } else if (name == u"redo") {
        v.redo();
    } else if (name == u"cut") {
        v.cut();
    } else if (name == u"copy") {
        v.copy();
    } else if (name == u"paste") {
        v.paste();
    } else if (name == u"select-all") {
        v.selectAll();
    } else if (name == u"delete") {
        v.deleteSelection();
    } else if (name == u"arrange-selection-order") {
        v.arrangeSelection(static_cast<PageCanvas::OrderChange>(std::clamp(state.toInt(), 0, 3)));
    } else if (name == u"move-selection-layer-up") {
        v.moveSelectionToLayer(layer + 1);
    } else if (name == u"move-selection-layer-down") {
        v.moveSelectionToLayer(layer - 1);
    } else if (name == u"rotation-snapping") {
        v.input()->setProperty("snapRotation", toggled(v.input()->snapRotation));
    } else if (name == u"grid-snapping") {
        v.input()->setProperty("snapGrid", toggled(v.input()->snapGrid));
    } else if (name == u"paired-pages-mode") {
        v.setPairedPages(toggled(v.pairedPages()));
    } else if (name == u"set-layout-vertical") {
        v.setLayoutVertical(toggled(v.layoutVertical()));
    } else if (name == u"set-layout-right-to-left") {
        v.setLayoutRightToLeft(toggled(v.layoutRightToLeft()));
    } else if (name == u"set-layout-bottom-to-top") {
        v.setLayoutBottomToTop(toggled(v.layoutBottomToTop()));
    } else if (name == u"set-columns-or-rows") {
        // Positive for columns, negative for rows
        const int count = state.toInt();
        if (count > 0) {
            v.setLayoutColumns(count);
        } else if (count < 0) {
            v.setLayoutRows(-count);
        }
    } else if (name == u"zoom-in") {
        v.zoomIn();
    } else if (name == u"zoom-out") {
        v.zoomOut();
    } else if (name == u"zoom-100") {
        v.zoomTo(1.0);
    } else if (name == u"zoom-fit") {
        if (toggled(false)) {
            v.fitWidth();
        }
    } else if (name == u"zoom") {
        v.zoomTo(state.toDouble());
    } else if (name == u"goto-first") {
        v.setCurrentPage(0);
    } else if (name == u"goto-previous") {
        v.setCurrentPage(page - 1);
    } else if (name == u"goto-next") {
        v.setCurrentPage(page + 1);
    } else if (name == u"goto-last") {
        v.setCurrentPage(v.pageCount() - 1);
    } else if (name == u"goto-next-annotated-page") {
        annotatedPage(1);
    } else if (name == u"goto-previous-annotated-page") {
        annotatedPage(-1);
    } else if (name == u"new-page-before") {
        v.insertPage(page);
    } else if (name == u"new-page-after") {
        v.insertPage(page + 1);
    } else if (name == u"new-page-at-end") {
        v.insertPage(v.pageCount());
    } else if (name == u"duplicate-page") {
        v.duplicatePage(page);
    } else if (name == u"move-page-towards-beginning") {
        v.movePage(page, page - 1);
    } else if (name == u"move-page-towards-end") {
        v.movePage(page, page + 1);
    } else if (name == u"delete-page") {
        v.deletePage(page);
    } else if (name == u"select-tool") {
        const int tool = state.toInt();
        if (tool == XOJ_TOOL_FLOATING_TOOLBOX) {
            emit c.floatingToolboxRequested(QPointF(-1, -1));
        } else if (tool > 0 && tool < static_cast<int>(std::size(TOOLS)) && TOOLS[tool].tool >= 0) {
            v.setTool(static_cast<PageCanvas::Tool>(TOOLS[tool].tool));
            if (TOOLS[tool].tool == PageCanvas::SelectRect || TOOLS[tool].tool == PageCanvas::SelectRegion) {
                v.setSelectAllLayers(tool == XOJ_TOOL_MULTILAYER_RECT || tool == XOJ_TOOL_MULTILAYER_REGION);
            }
        } else {
            throw Error{tr("There is no tool %1").arg(tool)};
        }
    } else if (name == u"select-default-tool") {
        v.setTool(PageCanvas::Pen);
    } else if (name == u"tool-draw-shape-recognizer") {
        drawingType(PageCanvas::ShapeRecognizer);
    } else if (name == u"tool-draw-rectangle") {
        drawingType(PageCanvas::Rectangle);
    } else if (name == u"tool-draw-ellipse") {
        drawingType(PageCanvas::Ellipse);
    } else if (name == u"tool-draw-arrow") {
        drawingType(PageCanvas::Arrow);
    } else if (name == u"tool-draw-double-arrow") {
        drawingType(PageCanvas::DoubleArrow);
    } else if (name == u"tool-draw-coordinate-system") {
        drawingType(PageCanvas::CoordinateSystem);
    } else if (name == u"tool-draw-line") {
        drawingType(PageCanvas::Line);
    } else if (name == u"tool-draw-spline") {
        drawingType(PageCanvas::Spline);
    } else if (name == u"setsquare") {
        geometryTool(PageCanvas::Setsquare);
    } else if (name == u"compass") {
        geometryTool(PageCanvas::Compass);
    } else if (name == u"tool-size") {
        v.setToolSize(static_cast<PageCanvas::ToolSize>(std::clamp(state.toInt(), 0, 4)));
    } else if (name == u"tool-pen-size") {
        toolSetting(PageCanvas::Pen, "size", state);
    } else if (name == u"tool-eraser-size") {
        toolSetting(PageCanvas::Eraser, "size", state);
    } else if (name == u"tool-highlighter-size") {
        toolSetting(PageCanvas::Highlighter, "size", state);
    } else if (name == u"tool-pen-line-style") {
        toolSetting(PageCanvas::Pen, "lineStyle", lineStyleName(state.toString()));
    } else if (name == u"tool-fill") {
        v.setFill(toggled(v.fill()));
    } else if (name == u"tool-pen-fill") {
        toolSetting(PageCanvas::Pen, "fill",
                    toggled(v.toolInfo(PageCanvas::Pen).value(QStringLiteral("fill")).toBool()));
    } else if (name == u"tool-highlighter-fill") {
        toolSetting(PageCanvas::Highlighter, "fill",
                    toggled(v.toolInfo(PageCanvas::Highlighter).value(QStringLiteral("fill")).toBool()));
    } else if (name == u"tool-fill-opacity" && hasState) {
        v.setFillAlpha(state.toInt());
    } else if (name == u"tool-pen-fill-opacity" && hasState) {
        toolSetting(PageCanvas::Pen, "fillAlpha", state);
    } else if (name == u"tool-highlighter-fill-opacity" && hasState) {
        toolSetting(PageCanvas::Highlighter, "fillAlpha", state);
    } else if (name == u"tool-eraser-type") {
        // Xournal++ counts from "none"
        v.setEraserType(static_cast<PageCanvas::EraserType>(std::clamp(state.toInt() - 1, 0, 2)));
    } else if (name == u"toggle-touch-drawing") {
        v.setFingerDraws(toggled(v.fingerDraws()));
    } else if (name == u"tool-color") {
        v.setColor(colorOf(state, v.color()));
    } else if (name == u"text-alignment") {
        v.setTextAlign(QString::fromLatin1(ALIGNMENT_NAMES[std::clamp(state.toInt(), 0, 2)]));
    } else if (name == u"layer-show-all" || name == u"layer-hide-all") {
        v.setAllLayersVisible(name == u"layer-show-all");
    } else if (name == u"layer-new-above-current") {
        v.addLayer();
    } else if (name == u"layer-new-below-current") {
        v.addLayer(true);
    } else if (name == u"layer-copy") {
        v.duplicateLayer(layer);
    } else if (name == u"layer-move-up") {
        v.moveLayer(layer, layer + 1);
    } else if (name == u"layer-move-down") {
        v.moveLayer(layer, layer - 1);
    } else if (name == u"layer-delete") {
        v.deleteLayer(layer);
    } else if (name == u"layer-merge-down") {
        v.mergeLayerDown(layer);
    } else if (name == u"layer-goto-next") {
        v.setCurrentLayer(std::min(layer + 1, layerCount - 1));
    } else if (name == u"layer-goto-previous") {
        v.setCurrentLayer(std::max(layer - 1, 0));
    } else if (name == u"layer-goto-top") {
        v.setCurrentLayer(layerCount - 1);
    } else if (name == u"layer-active") {
        // Counted from 1; 0 is the background
        v.setCurrentLayer(std::clamp(state.toInt() - 1, 0, layerCount - 1));
    } else {
        // Something of the window: its dialogs, the full screen, the audio controls, ...
        emit c.actionRequested(name, state);
    }
}

QVariant PluginApi::actionState(const QString& name) const {
    const PageCanvas& v = canvas();
    if (name == u"select-tool") {
        return xojTool(v);
    }
    if (name == u"rotation-snapping") {
        return const_cast<PageCanvas&>(v).input()->snapRotation;
    }
    if (name == u"grid-snapping") {
        return const_cast<PageCanvas&>(v).input()->snapGrid;
    }
    if (name == u"paired-pages-mode") {
        return v.pairedPages();
    }
    if (name == u"presentation-mode") {
        return v.presentationMode();
    }
    if (name == u"set-layout-vertical") {
        return v.layoutVertical();
    }
    if (name == u"set-layout-right-to-left") {
        return v.layoutRightToLeft();
    }
    if (name == u"set-layout-bottom-to-top") {
        return v.layoutBottomToTop();
    }
    if (name == u"set-columns-or-rows") {
        return v.layoutRows() > 0 ? -v.layoutRows() : v.layoutColumns();
    }
    if (name == u"zoom") {
        return v.zoom();
    }
    if (name == u"tool-color") {
        return rgbOf(v.color());
    }
    if (name == u"tool-size") {
        return static_cast<int>(v.toolSize());
    }
    if (name == u"tool-pen-size" || name == u"tool-eraser-size" || name == u"tool-highlighter-size") {
        const PageCanvas::Tool tool = name == u"tool-pen-size"    ? PageCanvas::Pen :
                                      name == u"tool-eraser-size" ? PageCanvas::Eraser :
                                                                    PageCanvas::Highlighter;
        return v.toolInfo(tool).value(QStringLiteral("size"));
    }
    if (name == u"tool-pen-line-style") {
        return lineStyleName(v.toolInfo(PageCanvas::Pen).value(QStringLiteral("lineStyle")).toString());
    }
    if (name == u"tool-fill") {
        return v.fill();
    }
    if (name == u"tool-pen-fill") {
        return v.toolInfo(PageCanvas::Pen).value(QStringLiteral("fill"));
    }
    if (name == u"tool-highlighter-fill") {
        return v.toolInfo(PageCanvas::Highlighter).value(QStringLiteral("fill"));
    }
    if (name == u"tool-eraser-type") {
        return static_cast<int>(v.eraserType()) + 1;
    }
    if (name == u"toggle-touch-drawing") {
        return v.fingerDraws();
    }
    if (name == u"setsquare") {
        return v.geometryTool() == PageCanvas::Setsquare;
    }
    if (name == u"compass") {
        return v.geometryTool() == PageCanvas::Compass;
    }
    if (name == u"text-alignment") {
        return std::max<int>(0, static_cast<int>(QStringList({QStringLiteral("left"), QStringLiteral("center"),
                                                              QStringLiteral("right")})
                                                         .indexOf(v.textAlign())));
    }
    if (name == u"layer-active") {
        return v.currentLayer() + 1;
    }
    if (name.startsWith(u"tool-draw-")) {
        static const QHash<QString, int> types = {
                {QStringLiteral("tool-draw-line"), PageCanvas::Line},
                {QStringLiteral("tool-draw-rectangle"), PageCanvas::Rectangle},
                {QStringLiteral("tool-draw-ellipse"), PageCanvas::Ellipse},
                {QStringLiteral("tool-draw-arrow"), PageCanvas::Arrow},
                {QStringLiteral("tool-draw-double-arrow"), PageCanvas::DoubleArrow},
                {QStringLiteral("tool-draw-coordinate-system"), PageCanvas::CoordinateSystem},
                {QStringLiteral("tool-draw-spline"), PageCanvas::Spline},
                {QStringLiteral("tool-draw-shape-recognizer"), PageCanvas::ShapeRecognizer},
        };
        return types.contains(name) && v.drawingType() == types.value(name);
    }
    return {};
}

// Tools

QVariantList PluginApi::changeToolColor(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("color"))) {
        throw Error{tr("The option \"color\" is missing")};
    }
    const QColor color = colorOf(options.value(QStringLiteral("color")), Qt::black);
    const QString toolName = options.value(QStringLiteral("tool")).toString();
    PageCanvas::Tool tool = v.tool();
    if (!toolName.isEmpty()) {
        static const QHash<QString, PageCanvas::Tool> tools = {
                {QStringLiteral("pen"), PageCanvas::Pen},
                {QStringLiteral("highlighter"), PageCanvas::Highlighter},
                {QStringLiteral("text"), PageCanvas::Text},
                {QStringLiteral("select_pdf_text_rect"), PageCanvas::SelectPdfTextRect},
                {QStringLiteral("select_pdf_text_linear"), PageCanvas::SelectPdfTextLinear},
        };
        if (!tools.contains(toolName.toLower())) {
            throw Error{tr("The tool \"%1\" has no colour").arg(toolName)};
        }
        tool = tools.value(toolName.toLower());
    }
    if (options.value(QStringLiteral("selection")).toBool() && tool == v.tool()) {
        v.setColor(color);  // the selected elements take it as well
    } else {
        v.setToolInfo(tool, {{QStringLiteral("color"), color}});
    }
    return {};
}

QVariantList PluginApi::getColorPalette(const QVariantList&) {
    QVariantList colors;
    for (const QVariant& entry: c.m_palette) {
        const QVariantMap map = entry.toMap();
        colors.append(QVariantMap{{QStringLiteral("color"), rgbOf(map.value(QStringLiteral("color")).value<QColor>())},
                                  {QStringLiteral("name"), map.value(QStringLiteral("name")).toString()}});
    }
    return {QVariant(colors)};
}

QVariantList PluginApi::getToolInfo(const QVariantList& args) {
    const PageCanvas& v = canvas();
    const QString mode = argumentText(args, 0);
    auto size = [&](PageCanvas::Tool tool) {
        const QVariantMap info = v.toolInfo(tool);
        return QVariantMap{
                {QStringLiteral("name"), QString::fromLatin1(SIZE_NAMES[info.value(QStringLiteral("size")).toInt()])},
                {QStringLiteral("value"), info.value(QStringLiteral("thickness"))}};
    };
    auto drawing = [&](PageCanvas::Tool tool) {
        const QVariantMap info = v.toolInfo(tool);
        return QVariantMap{
                {QStringLiteral("size"), size(tool)},
                {QStringLiteral("color"), rgbOf(info.value(QStringLiteral("color")).value<QColor>())},
                {QStringLiteral("filled"), info.value(QStringLiteral("fill"))},
                {QStringLiteral("fillOpacity"), info.value(QStringLiteral("fillAlpha"))},
                {QStringLiteral("drawingType"),
                 QString::fromLatin1(DRAWING_TYPE_NAMES[info.value(QStringLiteral("drawingType")).toInt()])},
                {QStringLiteral("lineStyle"), lineStyleName(info.value(QStringLiteral("lineStyle")).toString())}};
    };

    if (mode == u"active") {
        QVariantMap info = drawing(v.tool());
        info.remove(QStringLiteral("filled"));
        info.insert(QStringLiteral("type"), QString::fromLatin1(TOOLS[xojTool(v)].name));
        info.insert(QStringLiteral("thickness"), v.thickness());
        return {info};
    }
    if (mode == u"pen") {
        return {drawing(PageCanvas::Pen)};
    }
    if (mode == u"highlighter") {
        QVariantMap info = drawing(PageCanvas::Highlighter);
        info.remove(QStringLiteral("lineStyle"));
        return {info};
    }
    if (mode == u"eraser") {
        return {QVariantMap{{QStringLiteral("type"), QString::fromLatin1(ERASER_TYPE_NAMES[v.eraserType() + 1])},
                            {QStringLiteral("size"), size(PageCanvas::Eraser)}}};
    }
    if (mode == u"text") {
        const QColor color = v.toolInfo(PageCanvas::Text).value(QStringLiteral("color")).value<QColor>();
        return {QVariantMap{{QStringLiteral("font"), QVariantMap{{QStringLiteral("name"), v.textFontDescription()},
                                                                 {QStringLiteral("size"), v.textSize()}}},
                            {QStringLiteral("color"), rgbOf(color)}}};
    }
    if (mode == u"selection") {
        if (!v.hasSelection()) {
            throw Error{tr("There is no selection")};
        }
        // The frame is not padded here, and a transformation applies at once: the three are the same
        const QVariantMap bounds = rectMap(v.selectionRect());
        return {QVariantMap{{QStringLiteral("boundingBox"), bounds},
                            {QStringLiteral("originalBounds"), bounds},
                            {QStringLiteral("snappedBounds"), bounds},
                            {QStringLiteral("rotation"), v.selectionRotation()}}};
    }
    throw Error{tr("Unknown tool \"%1\"").arg(mode)};
}

QVariantList PluginApi::getFonts(const QVariantList&) {
    const QStringList families = QFontDatabase::families();
    const qsizetype current = families.indexOf(canvas().textFamily());
    QVariantMap result{{QStringLiteral("families"), families}};
    if (current >= 0) {
        result.insert(QStringLiteral("current"), static_cast<qlonglong>(current + 1));
    }
    return {result};
}

QVariantList PluginApi::getFont(const QVariantList&) {
    const PageCanvas& v = canvas();
    return {QVariantMap{{QStringLiteral("name"), v.textFontDescription()}, {QStringLiteral("size"), v.textSize()}}};
}

QVariantList PluginApi::setFont(const QVariantList& args) {
    PageCanvas& v = canvas();
    QString name;
    double size = 0;
    if (args.value(0).typeId() == QMetaType::QVariantMap) {
        const QVariantMap font = args.value(0).toMap();
        name = font.value(QStringLiteral("name")).toString();
        size = font.value(QStringLiteral("size")).toDouble();
    } else {
        // As Pango describes a font: the size comes last
        static const QRegularExpression withSize(QStringLiteral(R"(^(.*?)\s*(\d+(?:\.\d+)?)$)"));
        name = argumentText(args, 0).trimmed();
        if (const auto match = withSize.match(name); match.hasMatch()) {
            name = match.captured(1).trimmed();
            size = match.captured(2).toDouble();
        }
    }
    if (!name.isEmpty()) {
        if (!QFontDatabase::hasFamily(TextFont::family(name))) {
            throw Error{tr("The font \"%1\" is not available").arg(TextFont::family(name))};
        }
        v.setTextFontDescription(name);
    }
    if (size > 0) {
        v.setTextSize(size);
    }
    return {};
}

// Document and view

QVariantList PluginApi::getDocumentStructure(const QVariantList&) {
    const PageCanvas& v = canvas();
    const Document& doc = v.document();
    QVariantList pages;
    for (const Page& page: doc.pages) {
        bool annotated = false;
        // The background is the layer 0, as in Xournal++
        LuaIntTable layers;
        layers.insert(0, QVariantMap{{QStringLiteral("isVisible"), page.backgroundVisible},
                                     {QStringLiteral("name"), page.background.name}});
        qlonglong number = 0;
        for (const Layer& layer: page.layers) {
            annotated = annotated || !layer.elements.empty();
            layers.insert(++number, QVariantMap{{QStringLiteral("name"), layer.name},
                                                {QStringLiteral("isVisible"), layer.visible},
                                                {QStringLiteral("isAnnotated"), !layer.elements.empty()}});
        }
        const Background& background = page.background;
        const QString format = background.type == Background::Type::Pdf    ? QStringLiteral(":pdf") :
                               background.type == Background::Type::Pixmap ? QStringLiteral(":image") :
                                                                             background.style;
        pages.append(QVariantMap{{QStringLiteral("pageWidth"), page.width},
                                 {QStringLiteral("pageHeight"), page.height},
                                 {QStringLiteral("isAnnotated"), annotated},
                                 {QStringLiteral("pageTypeFormat"), format},
                                 {QStringLiteral("pageTypeConfig"), background.config},
                                 {QStringLiteral("backgroundColor"), rgbOf(background.color)},
                                 {QStringLiteral("pdfBackgroundPageNo"),
                                  background.type == Background::Type::Pdf ? background.pdfPage + 1 : 0},
                                 {QStringLiteral("layers"), QVariant::fromValue(layers)},
                                 {QStringLiteral("currentLayer"), page.layers.empty() ? 0 : page.activeLayer() + 1}});
    }
    return {QVariantMap{{QStringLiteral("pages"), pages},
                        {QStringLiteral("currentPage"), v.currentPage() + 1},
                        {QStringLiteral("pdfBackgroundFilename"), v.pdfPath()},
                        {QStringLiteral("xoppFilename"), v.filePath()}}};
}

QVariantList PluginApi::getPageLabel(const QVariantList& args) {
    // The label the PDF gives the page that is the background of a page of the document
    const PageCanvas& v = canvas();
    const int page = args.value(0).toInt();
    if (page < 1 || page > v.pageCount()) {
        return {QVariant(), tr("Page number %1 is out of range").arg(page)};
    }
    const Background& background = v.document().pages[static_cast<size_t>(page - 1)].background;
    return {background.type == Background::Type::Pdf ? v.pdfPageLabel(background.pdfPage) : QString()};
}

QVariantList PluginApi::changeCurrentPageBackground(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QString style = argumentText(args, 0);
    QStringList known;
    for (const QVariant& type: v.pageTypes()) {
        known.append(type.toMap().value(QStringLiteral("style")).toString());
    }
    if (!known.contains(style)) {
        throw Error{tr("Unknown page type \"%1\"").arg(style)};
    }
    v.setPageProperties(v.currentPage(),
                        {{QStringLiteral("type"), QStringLiteral("solid")}, {QStringLiteral("style"), style}});
    return {};
}

QVariantList PluginApi::changeBackgroundPdfPageNr(const QVariantList& args) {
    PageCanvas& v = canvas();
    int pdfPage = args.value(0).toInt();
    if (args.value(1).toBool()) {
        const QVariantMap properties = v.pageProperties(v.currentPage());
        if (properties.value(QStringLiteral("type")).toString() != u"pdf") {
            throw Error{tr("The current page has no page of a PDF as background")};
        }
        pdfPage += properties.value(QStringLiteral("pdfPage")).toInt();
    }
    if (pdfPage < 1 || pdfPage > v.pdfPageCount()) {
        throw Error{tr("The PDF has no page %1").arg(pdfPage)};
    }
    v.setPageProperties(v.currentPage(),
                        {{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("pdfPage"), pdfPage}});
    return {};
}

QVariantList PluginApi::scrollToPage(const QVariantList& args) {
    PageCanvas& v = canvas();
    const int page = args.value(1).toBool() ? v.currentPage() + args.value(0).toInt() : args.value(0).toInt() - 1;
    v.setCurrentPage(std::clamp(page, 0, v.pageCount() - 1));
    return {};
}

QVariantList PluginApi::scrollToPos(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QPointF position(args.value(0).toDouble(), args.value(1).toDouble());
    // Relative, unless the third argument says otherwise
    const bool relative = args.size() < 3 || !args[2].isValid() || args[2].toBool();
    v.scrollTo(relative ? v.scrollPosition() + position : position);
    return {};
}

QVariantList PluginApi::getScrollPos(const QVariantList&) {
    const PageCanvas& v = canvas();
    return {rectMap(QRectF(v.scrollPosition(), QSizeF(v.width(), v.height())))};
}

QVariantList PluginApi::setCurrentPage(const QVariantList& args) {
    canvas().selectPage(args.value(0).toInt() - 1);
    return {};
}

QVariantList PluginApi::setPageSize(const QVariantList& args) {
    PageCanvas& v = canvas();
    double width = args.value(0).toDouble();
    double height = args.value(1).toDouble();
    if (args.value(2).toBool()) {
        const QSizeF size = v.pageSize(v.currentPage());
        width += size.width();
        height += size.height();
    }
    if (width <= 0 || height <= 0) {
        throw Error{tr("The page cannot have the size %1 × %2").arg(width).arg(height)};
    }
    v.setPageProperties(v.currentPage(), {{QStringLiteral("width"), width}, {QStringLiteral("height"), height}});
    return {};
}

QVariantList PluginApi::setCurrentLayer(const QVariantList& args) {
    PageCanvas& v = canvas();
    const int layer = args.value(0).toInt();  // counted from 1; 0 is the background
    const int count = static_cast<int>(v.layers().size());
    if (layer < 1 || layer > count) {
        throw Error{tr("There is no layer %1").arg(layer)};
    }
    v.setCurrentLayer(layer - 1);
    if (args.value(1).toBool()) {
        // The layers up to the selected one are shown, the others hidden
        v.setBackgroundVisible(true);
        for (int i = 0; i < count; ++i) {
            v.setLayerVisible(i, i < layer);
        }
    }
    return {};
}

QVariantList PluginApi::setLayerVisibility(const QVariantList& args) {
    PageCanvas& v = canvas();
    v.setLayerVisible(v.currentLayer(), args.value(0).toBool());
    return {};
}

QVariantList PluginApi::setCurrentLayerName(const QVariantList& args) {
    PageCanvas& v = canvas();
    v.renameLayer(v.currentLayer(), argumentText(args, 0));
    return {};
}

QVariantList PluginApi::setBackgroundName(const QVariantList& args) {
    canvas().setBackgroundName(argumentText(args, 0));
    return {};
}

QVariantList PluginApi::getDisplayDpi(const QVariantList&) {
    const PageCanvas& v = canvas();
    const QScreen* screen = v.window() ? v.window()->screen() : nullptr;
    return {screen ? static_cast<int>(std::lround(screen->physicalDotsPerInch())) : 96};
}

QVariantList PluginApi::getZoom(const QVariantList&) { return {canvas().zoom()}; }

QVariantList PluginApi::setZoom(const QVariantList& args) {
    canvas().zoomTo(args.value(0).toDouble());
    return {};
}

QVariantList PluginApi::refreshPage(const QVariantList&) {
    canvas().refresh();
    return {};
}

// Files

QVariantList PluginApi::exportDocument(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    const QString file = options.value(QStringLiteral("outputFile")).toString();
    if (file.isEmpty()) {
        throw Error{tr("The option \"outputFile\" is missing")};
    }
    const QString background = options.value(QStringLiteral("background"), QStringLiteral("all")).toString();
    QVariantMap map{{QStringLiteral("pages"), options.value(QStringLiteral("range")).toString()},
                    {QStringLiteral("layers"), options.value(QStringLiteral("layerRange")).toString()},
                    {QStringLiteral("background"), background == u"unruled" ? QStringLiteral("noRuling") : background},
                    {QStringLiteral("progressive"), options.value(QStringLiteral("progressiveMode")).toBool()}};
    for (const auto& [from, to]:
         {std::pair{"pngDpi", "dpi"}, std::pair{"pngWidth", "width"}, std::pair{"pngHeight", "height"}}) {
        if (options.contains(QString::fromLatin1(from))) {
            map.insert(QString::fromLatin1(to), options.value(QString::fromLatin1(from)));
        }
    }
    return {v.exportDocument(QUrl::fromLocalFile(QFileInfo(file).absoluteFilePath()), map)};
}

QVariantList PluginApi::openFile(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QString path = argumentText(args, 0);
    const int page = args.value(1).isValid() ? args.value(1).toInt() : 1;
    if (!QFileInfo(path).isFile()) {
        return {false};
    }
    const QUrl url = QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath());
    if (v.modified() && !args.value(2).toBool()) {
        // The window asks what to do with the changes of the document that is open
        emit c.actionRequested(QStringLiteral("open-file"),
                               QVariantMap{{QStringLiteral("url"), url}, {QStringLiteral("page"), page}});
        return {true};
    }
    bool failed = false;
    const auto connection = QObject::connect(&v, &PageCanvas::loadFailed, &v, [&failed] { failed = true; });
    v.openFile(url);
    QObject::disconnect(connection);
    if (!failed) {
        v.setCurrentPage(page - 1);
    }
    return {!failed};
}

QVariantList PluginApi::getFolder(const QVariantList& args) {
    const QString type = argumentText(args, 0);
    QString folder;
    if (type == u"config") {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation) +
                 QStringLiteral("/plugin-settings/");
    } else if (type == u"state") {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                 QStringLiteral("/plugin-state/");
    } else if (type == u"data") {
        folder = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/plugin-data/");
    } else {
        throw Error{tr("Unknown kind of folder \"%1\": it is \"config\", \"data\" or \"state\"").arg(type)};
    }
    folder += plugin().name;
    if (!QDir().mkpath(folder)) {
        throw Error{tr("Could not create the folder \"%1\"").arg(folder)};
    }
    return {folder};
}

QVariantList PluginApi::rename(const QVariantList& args) {
    const QString from = argumentText(args, 0);
    const QString to = argumentText(args, 1);
    if (!QFileInfo::exists(from)) {
        return {QVariant(), tr("\"%1\" does not exist").arg(from)};
    }
    QFile::remove(to);
    // Renaming does not work from one file system to another: copy then
    if (QFile::rename(from, to) || (QFile::copy(from, to) && QFile::remove(from))) {
        return {1};
    }
    return {QVariant(), tr("Could not rename \"%1\" to \"%2\"").arg(from, to)};
}

// Elements

void PluginApi::forElements(
        const QString& scope,
        const std::function<void(const PageCanvas::ElementRef&, const Element&, QVariantMap&)>& function,
        QVariantList& result) {
    const PageCanvas& v = canvas();
    const Document& doc = v.document();
    auto visit = [&](int page, int layer, size_t index, bool withLayer, bool withPage) {
        const Element& element =
                doc.pages[static_cast<size_t>(page)].layers[static_cast<size_t>(layer)].elements[index];
        QVariantMap map;
        const PageCanvas::ElementRef place{page, layer, index};
        function(place, element, map);
        if (map.isEmpty()) {
            return;  // another kind of element
        }
        map.insert(QStringLiteral("ref"), ref(place));
        if (withLayer) {
            map.insert(QStringLiteral("layer"), layer + 1);
        }
        if (withPage) {
            map.insert(QStringLiteral("page"), page + 1);
        }
        result.append(map);
    };
    auto visitPage = [&](int page, bool withPage) {
        const auto& layers = doc.pages[static_cast<size_t>(page)].layers;
        for (size_t layer = 0; layer < layers.size(); ++layer) {
            for (size_t i = 0; i < layers[layer].elements.size(); ++i) {
                visit(page, static_cast<int>(layer), i, true, withPage);
            }
        }
    };

    const int page = v.currentPage();
    if (page < 0 || page >= v.pageCount()) {
        return;
    }
    if (scope == u"selection") {
        int selectionPage = 0;
        int layer = 0;
        std::vector<size_t> indices;
        if (!v.selectedElements(selectionPage, layer, indices)) {
            throw Error{tr("There is no selection")};
        }
        for (size_t index: indices) {
            visit(selectionPage, layer, index, false, false);
        }
    } else if (scope == u"layer") {
        const Page& current = doc.pages[static_cast<size_t>(page)];
        if (!current.layers.empty()) {
            const int layer = current.activeLayer();
            for (size_t i = 0; i < current.layers[static_cast<size_t>(layer)].elements.size(); ++i) {
                visit(page, layer, i, false, false);
            }
        }
    } else if (scope == u"page") {
        visitPage(page, false);
    } else if (scope == u"all") {
        for (int i = 0; i < v.pageCount(); ++i) {
            visitPage(i, true);
        }
    } else {
        throw Error{tr("Unknown scope \"%1\": it is \"selection\", \"layer\", \"page\" or \"all\"").arg(scope)};
    }
}

namespace {

/// Whether the changes of a call that adds elements are undone in one step
bool groupedUndo(const QVariantMap& options) {
    return options.value(QStringLiteral("allowUndoRedoAction"), QStringLiteral("grouped")).toString() != u"individual";
}

/// The style a plugin gives a stroke, on top of the settings of the tool
Stroke styledStroke(const PageCanvas& canvas, const QVariantMap& options) {
    const QString toolName = options.value(QStringLiteral("tool"), QStringLiteral("pen")).toString().toLower();
    if (toolName != u"pen" && toolName != u"highlighter") {
        throw PluginApi::Error{tr("Unknown stroke tool \"%1\": it is \"pen\" or \"highlighter\"").arg(toolName)};
    }
    const bool highlighter = toolName == u"highlighter";
    const QVariantMap info = canvas.toolInfo(highlighter ? PageCanvas::Highlighter : PageCanvas::Pen);

    Stroke stroke;
    stroke.tool = highlighter ? Stroke::Tool::Highlighter : Stroke::Tool::Pen;
    stroke.color = colorOf(options.value(QStringLiteral("color")), info.value(QStringLiteral("color")).value<QColor>());
    if (highlighter) {
        stroke.color.setAlpha(0x7f);
    }
    stroke.width = options.contains(QStringLiteral("width")) ? options.value(QStringLiteral("width")).toDouble() :
                                                               info.value(QStringLiteral("thickness")).toDouble();
    if (stroke.width <= 0) {
        throw PluginApi::Error{tr("The width of a stroke has to be positive")};
    }
    stroke.fill = options.contains(QStringLiteral("fill")) ?
                          std::clamp(options.value(QStringLiteral("fill")).toInt(), -1, 255) :
                  info.value(QStringLiteral("fill")).toBool() ? info.value(QStringLiteral("fillAlpha")).toInt() :
                                                                -1;
    const QString style = options.contains(QStringLiteral("lineStyle")) ?
                                  options.value(QStringLiteral("lineStyle")).toString() :
                                  info.value(QStringLiteral("lineStyle")).toString();
    if (lineStyleName(style) != u"plain") {
        stroke.setStyle(style);
    }
    return stroke;
}

QList<double> numbers(const QVariant& table) {
    QList<double> result;
    for (const QVariant& value: table.toList()) {
        result.append(value.toDouble());
    }
    return result;
}

/// Font and size of a text or link a plugin adds: its own, or those of the text tool
void fontOf(const PageCanvas& canvas, const QVariantMap& options, QString& font, double& size) {
    const QVariantMap given = options.value(QStringLiteral("font")).toMap();
    font = given.value(QStringLiteral("name"), canvas.textFontDescription()).toString();
    size = given.value(QStringLiteral("size"), canvas.textSize()).toDouble();
    if (size <= 0) {
        throw PluginApi::Error{tr("The size of a font has to be positive")};
    }
}

}  // namespace

QVariantList PluginApi::addStrokes(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("strokes"))) {
        throw Error{tr("The option \"strokes\" is missing")};
    }
    std::vector<Element> elements;
    for (const QVariant& entry: options.value(QStringLiteral("strokes")).toList()) {
        const QVariantMap map = entry.toMap();
        const QList<double> x = numbers(map.value(QStringLiteral("x")));
        const QList<double> y = numbers(map.value(QStringLiteral("y")));
        const QList<double> pressure = numbers(map.value(QStringLiteral("pressure")));
        if (x.isEmpty() || x.size() != y.size() || (!pressure.isEmpty() && pressure.size() != x.size())) {
            throw Error{tr("The tables x, y and pressure of a stroke have to be of the same length and not empty")};
        }
        Stroke stroke = styledStroke(v, map);
        for (qsizetype i = 0; i < x.size(); ++i) {
            stroke.points.append(QPointF(x[i], y[i]));
        }
        if (!pressure.isEmpty() && x.size() > 1) {
            // The pressure of a point is the width of the segment that starts there
            stroke.widths = pressure.mid(0, x.size() - 1);
        }
        stroke.updateBounds();
        elements.emplace_back(std::move(stroke));
    }
    return {QVariant(refs(v.addElements(elements, groupedUndo(options))))};
}

QVariantList PluginApi::addSplines(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("splines"))) {
        throw Error{tr("The option \"splines\" is missing")};
    }
    std::vector<Element> elements;
    for (const QVariant& entry: options.value(QStringLiteral("splines")).toList()) {
        const QVariantMap map = entry.toMap();
        const QList<double> coordinates = numbers(map.value(QStringLiteral("coordinates")));
        if (coordinates.isEmpty() || coordinates.size() % 8 != 0) {
            throw Error{tr("The coordinates of a spline come in groups of eight: two knots and two control points")};
        }
        Stroke stroke = styledStroke(v, map);
        for (qsizetype i = 0; i < coordinates.size(); i += 8) {
            SplineSegment segment;
            segment.firstKnot = PathPoint(coordinates[i], coordinates[i + 1]);
            segment.firstControlPoint = PathPoint(coordinates[i + 2], coordinates[i + 3]);
            segment.secondControlPoint = PathPoint(coordinates[i + 4], coordinates[i + 5]);
            segment.secondKnot = PathPoint(coordinates[i + 6], coordinates[i + 7]);
            for (const PathPoint& point: segment.toPointSequence()) {
                stroke.points.append(point.pos());
            }
        }
        stroke.points.append(QPointF(coordinates[coordinates.size() - 2], coordinates[coordinates.size() - 1]));
        stroke.updateBounds();
        elements.emplace_back(std::move(stroke));
    }
    return {QVariant(refs(v.addElements(elements, groupedUndo(options))))};
}

QVariantList PluginApi::addTexts(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("texts"))) {
        throw Error{tr("The option \"texts\" is missing")};
    }
    const QColor toolColor = v.toolInfo(PageCanvas::Text).value(QStringLiteral("color")).value<QColor>();
    std::vector<Element> elements;
    for (const QVariant& entry: options.value(QStringLiteral("texts")).toList()) {
        const QVariantMap map = entry.toMap();
        if (!map.contains(QStringLiteral("text")) || !map.contains(QStringLiteral("x")) ||
            !map.contains(QStringLiteral("y"))) {
            throw Error{tr("A text needs \"text\", \"x\" and \"y\"")};
        }
        TextElement text;
        text.text = map.value(QStringLiteral("text")).toString();
        fontOf(v, map, text.font, text.size);
        text.color = colorOf(map.value(QStringLiteral("color")), toolColor);
        text.pos = QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble());
        if (map.contains(QStringLiteral("wrap"))) {
            text.wrap = map.value(QStringLiteral("wrap")).toDouble();
        }
        elements.emplace_back(std::move(text));
    }
    return {QVariant(refs(v.addElements(elements, groupedUndo(options))))};
}

QVariantList PluginApi::addLinks(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("links"))) {
        throw Error{tr("The option \"links\" is missing")};
    }
    const QColor toolColor = v.toolInfo(PageCanvas::Link).value(QStringLiteral("color")).value<QColor>();
    std::vector<Element> elements;
    for (const QVariant& entry: options.value(QStringLiteral("links")).toList()) {
        const QVariantMap map = entry.toMap();
        if (!map.contains(QStringLiteral("text")) || !map.contains(QStringLiteral("url")) ||
            !map.contains(QStringLiteral("x")) || !map.contains(QStringLiteral("y"))) {
            throw Error{tr("A link needs \"text\", \"url\", \"x\" and \"y\"")};
        }
        LinkElement link;
        link.text = map.value(QStringLiteral("text")).toString();
        link.url = map.value(QStringLiteral("url")).toString();
        fontOf(v, map, link.font, link.size);
        link.color = colorOf(map.value(QStringLiteral("color")), toolColor);
        link.align =
                QString::fromLatin1(ALIGNMENT_NAMES[std::clamp(map.value(QStringLiteral("alignment")).toInt(), 0, 2)]);
        link.matrix = {
                1, 0, 0, 1, map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble()};
        elements.emplace_back(std::move(link));
    }
    return {QVariant(refs(v.addElements(elements, groupedUndo(options))))};
}

QVariantList PluginApi::addImages(const QVariantList& args) {
    PageCanvas& v = canvas();
    const QVariantMap options = args.value(0).toMap();
    if (!options.contains(QStringLiteral("images"))) {
        throw Error{tr("The option \"images\" is missing")};
    }
    const QSizeF page = v.pageSize(v.currentPage());
    // What goes wrong with one image is told in its place of the result
    QVariantList result;
    std::vector<Element> elements;
    std::vector<qsizetype> places;
    for (const QVariant& entry: options.value(QStringLiteral("images")).toList()) {
        const QVariantMap map = entry.toMap();
        const bool hasPath = map.contains(QStringLiteral("path"));
        if (hasPath == map.contains(QStringLiteral("data"))) {
            throw Error{tr("An image needs either \"path\" or \"data\"")};
        }
        ImageElement image;
        if (hasPath) {
            const QString path = map.value(QStringLiteral("path")).toString();
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                result.append(tr("Could not open \"%1\": %2").arg(path, file.errorString()));
                continue;
            }
            image.data = file.readAll();
        } else {
            image.data = map.value(QStringLiteral("data")).toByteArray();
        }
        image.image = QImage::fromData(image.data);
        if (image.image.isNull()) {
            result.append(tr("The image could not be read"));
            continue;
        }

        // The size: that of the image, changed by the limits and the scale that are given
        const QSizeF natural = image.image.size();
        const double maxWidth = map.value(QStringLiteral("maxWidth"), -1).toDouble();
        const double maxHeight = map.value(QStringLiteral("maxHeight"), -1).toDouble();
        const bool keepRatio =
                !map.contains(QStringLiteral("aspectRatio")) || map.value(QStringLiteral("aspectRatio")).toBool();
        QSizeF size = natural;
        if (maxWidth > 0 && maxHeight > 0) {
            size = keepRatio ? natural.scaled(maxWidth, maxHeight, Qt::KeepAspectRatio) : QSizeF(maxWidth, maxHeight);
        } else if (maxWidth > 0) {
            size = QSizeF(maxWidth, keepRatio ? natural.height() * maxWidth / natural.width() : natural.height());
        } else if (maxHeight > 0) {
            size = QSizeF(keepRatio ? natural.width() * maxHeight / natural.height() : natural.width(), maxHeight);
        }
        size *= map.contains(QStringLiteral("scale")) ? map.value(QStringLiteral("scale")).toDouble() : 1.0;
        if (size.width() > page.width() || size.height() > page.height()) {
            size.scale(page, Qt::KeepAspectRatio);
        }
        if (size.isEmpty()) {
            result.append(tr("The image would have no size"));
            continue;
        }
        image.naturalSize = natural;
        image.rect = QRectF(
                QPointF(map.value(QStringLiteral("x")).toDouble(), map.value(QStringLiteral("y")).toDouble()), size);
        places.push_back(result.size());
        result.append(QVariant());
        elements.emplace_back(std::move(image));
    }
    const QVariantList added = refs(v.addElements(elements, groupedUndo(options)));
    for (qsizetype i = 0; i < added.size(); ++i) {
        result[places[static_cast<size_t>(i)]] = added[i];
    }
    return {QVariant(result)};
}

QVariantList PluginApi::getStrokes(const QVariantList& args) {
    QVariantList result;
    forElements(
            argumentText(args, 0),
            [](const PageCanvas::ElementRef&, const Element& element, QVariantMap& map) {
                const auto* stroke = std::get_if<Stroke>(&element);
                if (!stroke) {
                    return;
                }
                QVariantList x;
                QVariantList y;
                for (const QPointF& point: stroke->points) {
                    x.append(point.x());
                    y.append(point.y());
                }
                map.insert(QStringLiteral("x"), x);
                map.insert(QStringLiteral("y"), y);
                if (stroke->hasPressure()) {
                    // One value per point: the last point has the width of the last segment
                    QVariantList pressure;
                    for (double width: stroke->widths) {
                        pressure.append(width);
                    }
                    pressure.append(stroke->widths.last());
                    map.insert(QStringLiteral("pressure"), pressure);
                }
                map.insert(QStringLiteral("tool"), stroke->tool == Stroke::Tool::Highlighter ?
                                                           QStringLiteral("highlighter") :
                                                   stroke->tool == Stroke::Tool::Eraser ? QStringLiteral("eraser") :
                                                                                          QStringLiteral("pen"));
                map.insert(QStringLiteral("width"), stroke->width);
                map.insert(QStringLiteral("color"), rgbOf(stroke->color));
                map.insert(QStringLiteral("fill"), stroke->fill);
                map.insert(QStringLiteral("lineStyle"), lineStyleName(stroke->style));
            },
            result);
    return {QVariant(result)};
}

QVariantList PluginApi::getTexts(const QVariantList& args) {
    QVariantList result;
    forElements(
            argumentText(args, 0),
            [](const PageCanvas::ElementRef&, const Element& element, QVariantMap& map) {
                const auto* text = std::get_if<TextElement>(&element);
                if (!text) {
                    return;
                }
                const QRectF bounds = Renderer::elementBounds(element);
                map = rectMap(bounds);
                map.insert(QStringLiteral("text"), text->text);
                map.insert(QStringLiteral("font"),
                           QVariantMap{{QStringLiteral("name"), text->font}, {QStringLiteral("size"), text->size}});
                map.insert(QStringLiteral("color"), rgbOf(text->color));
                if (text->wrap >= 0) {
                    map.insert(QStringLiteral("wrap"), text->wrap);
                }
            },
            result);
    return {QVariant(result)};
}

QVariantList PluginApi::getLinks(const QVariantList& args) {
    QVariantList result;
    forElements(
            argumentText(args, 0),
            [](const PageCanvas::ElementRef&, const Element& element, QVariantMap& map) {
                const auto* link = std::get_if<LinkElement>(&element);
                if (!link) {
                    return;
                }
                map = rectMap(Renderer::elementBounds(element));
                map.insert(QStringLiteral("text"), link->text);
                map.insert(QStringLiteral("url"), link->url);
                map.insert(QStringLiteral("alignment"), link->align == u"center" ? 1 : link->align == u"right" ? 2 : 0);
                map.insert(QStringLiteral("font"),
                           QVariantMap{{QStringLiteral("name"), link->font}, {QStringLiteral("size"), link->size}});
                map.insert(QStringLiteral("color"), rgbOf(link->color));
            },
            result);
    return {QVariant(result)};
}

QVariantList PluginApi::getImages(const QVariantList& args) {
    QVariantList result;
    forElements(
            argumentText(args, 0),
            [](const PageCanvas::ElementRef&, const Element& element, QVariantMap& map) {
                const auto* image = std::get_if<ImageElement>(&element);
                if (!image || image->tex) {
                    return;
                }
                map = rectMap(Renderer::elementBounds(element));
                map.insert(QStringLiteral("data"), image->data);
                QBuffer buffer;
                buffer.setData(image->data);
                buffer.open(QIODevice::ReadOnly);
                map.insert(QStringLiteral("format"), QString::fromLatin1(QImageReader::imageFormat(&buffer)));
                map.insert(QStringLiteral("imageWidth"), image->image.width());
                map.insert(QStringLiteral("imageHeight"), image->image.height());
            },
            result);
    return {QVariant(result)};
}

QVariantList PluginApi::addToSelection(const QVariantList& args) {
    PageCanvas& v = canvas();
    const int page = v.currentPage();
    const int layer = v.currentLayer();
    std::vector<size_t> indices;
    for (const QVariant& value: args.value(0).toList()) {
        const auto number = static_cast<size_t>(reinterpret_cast<quintptr>(value.value<void*>()));
        if (number == 0 || number > c.m_refs.size()) {
            throw Error{tr("A reference is not one of an element. References are valid until the plugin returns")};
        }
        const PageCanvas::ElementRef& element = c.m_refs[number - 1];
        if (element.page != page || element.layer != layer) {
            throw Error{tr("Only elements of the current layer can be selected")};
        }
        indices.push_back(element.index);
    }
    v.addToSelection(indices);
    return {};
}

QVariantList PluginApi::clearSelection(const QVariantList&) {
    canvas().clearSelection();
    return {};
}
