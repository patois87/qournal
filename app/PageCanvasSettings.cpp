/*
 * Qournal
 *
 * The part of PageCanvas that keeps its settings between sessions: the tools, the input, the buttons of the
 * devices, the layout of the pages and the page of new documents
 *
 * @license GNU GPLv2 or later
 */

#include <QMetaEnum>
#include <QMetaProperty>
#include <QSettings>
#include <algorithm>
#include <iterator>

#include "PageCanvas.h"
#include "Platform.h"

namespace {

// The properties of the canvas that are stored as they are
const char* const CANVAS_PROPERTIES[] = {
        "eraserType",         "textSize",          "textAlign",           "textJustify",
        "usePressure",        "fingerDraws",       "selectAllLayers",     "pairedPages",
        "layoutVertical",     "layoutRightToLeft", "layoutBottomToTop",   "autosaveEnabled",
        "autosaveInterval",   "pairedPagesOffset", "canvasColor",         "pdfMarkerAlpha",
        "spaceAbove",         "spaceBelow",        "spaceLeft",           "spaceRight",
        "unlimitedScrolling", "displayDpi",        "appendPage",          "selectionColor",
        "positionColor",      "positionRadius",    "positionBorderColor", "positionBorderWidth"};

const char* const TEMPLATE_KEYS[] = {"width", "height", "color", "style", "config"};

/// A value in a form that is readable in the settings file
QVariant storable(const QVariant& value, const QMetaProperty& property) {
    if (property.isEnumType()) {
        return value.toInt();
    }
    if (value.typeId() == QMetaType::QColor) {
        return value.value<QColor>().name(QColor::HexArgb);
    }
    return value;
}

void writeProperty(QSettings& settings, const QObject* object, const QMetaProperty& property) {
    settings.setValue(QString::fromLatin1(property.name()), storable(property.read(object), property));
}

void readProperty(const QSettings& settings, QObject* object, const QMetaProperty& property) {
    const QString key = QString::fromLatin1(property.name());
    if (!settings.contains(key)) {
        return;
    }
    QVariant value = settings.value(key);
    if (property.isEnumType()) {
        value = value.toInt();
    } else if (!value.convert(property.metaType())) {
        return;  // not what this version expects: the default stays
    }
    property.write(object, value);
}

QString buttonName(int button) {
    return QString::fromLatin1(QMetaEnum::fromType<PageCanvas::Button>().valueToKey(button)).mid(6);  // "Button"
}

QString toolName(int tool) { return QString::fromLatin1(QMetaEnum::fromType<PageCanvas::Tool>().valueToKey(tool)); }

}  // namespace

void PageCanvas::setCanvasColor(const QColor& color) {
    if (color.isValid() && m_canvasColor != color) {
        m_canvasColor = color;
        resetTiles();
        scheduleTiles();
        updateView();
        emit canvasColorChanged();
    }
}

void PageCanvas::setEinkMode(bool eink) {
    if (m_einkMode != eink) {
        m_einkMode = eink;
        m_drawingStyleDrawn = drawingStyle();
        applyEinkDefaults();
        if (m_einkMode && Platform::EinkPen::available()) {
            m_einkPenTick.start();
        } else {
            m_einkPenTick.stop();
            einkPenUpdate();
        }
        resetTiles();
        scheduleTiles();
        updateView();
        emit canvasColorChanged();
    }
}

void PageCanvas::applyEinkDefaults() {
    // The blue the pen starts with is faint on electronic paper: there it starts black. A colour that was chosen
    // stays
    static const QColor DEFAULT_PEN(0x00, 0x2e, 0x99);
    if (m_einkMode && m_toolStates[Pen].color == DEFAULT_PEN) {
        m_toolStates[Pen].color = Qt::black;
        emit toolChanged();
    }
}

void PageCanvas::setPageTemplate(const QVariantMap& pageTemplate) {
    QVariantMap kept;
    for (const char* key: TEMPLATE_KEYS) {
        const QString name = QString::fromLatin1(key);
        if (pageTemplate.contains(name)) {
            kept.insert(name, pageTemplate.value(name));
        }
    }
    if (m_pageTemplate != kept) {
        m_pageTemplate = kept;
        emit pageTemplateChanged();
    }
}

void PageCanvas::applyPageTemplate(Page& page) const {
    const double width = m_pageTemplate.value(QStringLiteral("width")).toDouble();
    const double height = m_pageTemplate.value(QStringLiteral("height")).toDouble();
    if (width >= 1 && height >= 1) {
        page.width = width;
        page.height = height;
    }
    const QColor color = m_pageTemplate.value(QStringLiteral("color")).value<QColor>();
    if (color.isValid()) {
        page.background.color = color;
    }
    const QString style = m_pageTemplate.value(QStringLiteral("style")).toString();
    if (!style.isEmpty()) {
        page.background.style = style;
        page.background.config = m_pageTemplate.value(QStringLiteral("config")).toString();
    }
}

void PageCanvas::saveSettings() const {
    QSettings settings;
    settings.beginGroup(QStringLiteral("canvas"));
    const QMetaObject* meta = metaObject();
    for (const char* name: CANVAS_PROPERTIES) {
        writeProperty(settings, this, meta->property(meta->indexOfProperty(name)));
    }
    settings.setValue(QStringLiteral("tool"), toolName(m_tool));
    settings.setValue(QStringLiteral("textFont"), m_textFont);
    // Either the number of columns or the number of rows is fixed
    settings.setValue(QStringLiteral("layoutColumns"), layoutColumns());
    settings.setValue(QStringLiteral("layoutRows"), layoutRows());

    settings.beginGroup(QStringLiteral("tools"));
    for (size_t i = 0; i < m_toolStates.size(); ++i) {
        const ToolState& state = m_toolStates[i];
        settings.beginGroup(toolName(static_cast<int>(i)));
        settings.setValue(QStringLiteral("color"), state.color.name(QColor::HexArgb));
        settings.setValue(QStringLiteral("size"), static_cast<int>(state.size));
        settings.setValue(QStringLiteral("drawingType"), static_cast<int>(state.drawingType));
        settings.setValue(QStringLiteral("fill"), state.fill);
        settings.setValue(QStringLiteral("fillAlpha"), state.fillAlpha);
        settings.setValue(QStringLiteral("lineStyle"), state.lineStyle);
        settings.endGroup();
    }
    settings.endGroup();

    settings.beginGroup(QStringLiteral("buttons"));
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        const int action = m_buttonActions[static_cast<size_t>(i)];
        settings.setValue(buttonName(i), action == ButtonNoAction        ? QStringLiteral("None") :
                                         action == ButtonFloatingToolbox ? QStringLiteral("FloatingToolbox") :
                                                                           toolName(action));
        const ButtonOptions& options = m_buttonOptions[static_cast<size_t>(i)];
        settings.setValue(buttonName(i) + QStringLiteral("_drawingType"), options.drawingType);
        settings.setValue(buttonName(i) + QStringLiteral("_size"), options.size);
        settings.setValue(buttonName(i) + QStringLiteral("_color"),
                          options.color.isValid() ? options.color.name(QColor::HexArgb) : QString());
    }
    settings.endGroup();

    settings.remove(QStringLiteral("pageTemplate"));
    settings.beginGroup(QStringLiteral("pageTemplate"));
    for (auto it = m_pageTemplate.begin(); it != m_pageTemplate.end(); ++it) {
        settings.setValue(it.key(), it.value().typeId() == QMetaType::QColor ?
                                            QVariant(it.value().value<QColor>().name(QColor::HexArgb)) :
                                            it.value());
    }
    settings.endGroup();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("input"));
    const QMetaObject* inputMeta = m_input.metaObject();
    for (int i = inputMeta->propertyOffset(); i < inputMeta->propertyCount(); ++i) {
        writeProperty(settings, &m_input, inputMeta->property(i));
    }
    settings.endGroup();
}

void PageCanvas::loadSettings() {
    finishInput();
    QSettings settings;
    settings.beginGroup(QStringLiteral("canvas"));
    const QMetaObject* meta = metaObject();
    for (const char* name: CANVAS_PROPERTIES) {
        readProperty(settings, this, meta->property(meta->indexOfProperty(name)));
    }
    m_textFont = settings.value(QStringLiteral("textFont"), m_textFont).toString();
    const int rows = settings.value(QStringLiteral("layoutRows"), layoutRows()).toInt();
    if (rows > 0) {
        setLayoutRows(rows);
    } else {
        setLayoutColumns(std::max(settings.value(QStringLiteral("layoutColumns"), layoutColumns()).toInt(), 1));
    }

    const auto sizes = QMetaEnum::fromType<ToolSize>();
    const auto drawingTypes = QMetaEnum::fromType<DrawingType>();
    settings.beginGroup(QStringLiteral("tools"));
    for (size_t i = 0; i < m_toolStates.size(); ++i) {
        ToolState& state = m_toolStates[i];
        settings.beginGroup(toolName(static_cast<int>(i)));
        const QColor color(settings.value(QStringLiteral("color")).toString());
        if (color.isValid()) {
            state.color = color;
        }
        const int size = settings.value(QStringLiteral("size"), static_cast<int>(state.size)).toInt();
        if (sizes.valueToKey(size)) {
            state.size = static_cast<ToolSize>(size);
        }
        const int type = settings.value(QStringLiteral("drawingType"), static_cast<int>(state.drawingType)).toInt();
        if (drawingTypes.valueToKey(type)) {
            state.drawingType = static_cast<DrawingType>(type);
        }
        state.fill = settings.value(QStringLiteral("fill"), state.fill).toBool();
        state.fillAlpha = std::clamp(settings.value(QStringLiteral("fillAlpha"), state.fillAlpha).toInt(), 1, 255);
        state.lineStyle = settings.value(QStringLiteral("lineStyle"), state.lineStyle).toString();
        settings.endGroup();
    }
    settings.endGroup();

    const auto tools = QMetaEnum::fromType<Tool>();
    bool known = false;
    const int tool = tools.keyToValue(settings.value(QStringLiteral("tool")).toString().toLatin1().constData(), &known);
    if (known) {
        m_tool = static_cast<Tool>(tool);
    }

    settings.beginGroup(QStringLiteral("buttons"));
    for (int i = 0; i < BUTTON_COUNT; ++i) {
        setButtonOptions(
                static_cast<Button>(i),
                {{QStringLiteral("drawingType"), settings.value(buttonName(i) + QStringLiteral("_drawingType"), -1)},
                 {QStringLiteral("size"), settings.value(buttonName(i) + QStringLiteral("_size"), -1)},
                 {QStringLiteral("color"),
                  QColor(settings.value(buttonName(i) + QStringLiteral("_color")).toString())}});
        if (!settings.contains(buttonName(i))) {
            continue;
        }
        const QString action = settings.value(buttonName(i)).toString();
        const int buttonTool = tools.keyToValue(action.toLatin1().constData(), &known);
        if (action == u"None") {
            m_buttonActions[static_cast<size_t>(i)] = ButtonNoAction;
        } else if (action == u"FloatingToolbox") {
            m_buttonActions[static_cast<size_t>(i)] = ButtonFloatingToolbox;
        } else if (known) {
            m_buttonActions[static_cast<size_t>(i)] = buttonTool;
        }
    }
    settings.endGroup();

    QVariantMap pageTemplate;
    settings.beginGroup(QStringLiteral("pageTemplate"));
    for (const char* key: TEMPLATE_KEYS) {
        const QString name = QString::fromLatin1(key);
        if (settings.contains(name)) {
            pageTemplate.insert(
                    name, name == u"color" ? QVariant(QColor(settings.value(name).toString())) : settings.value(name));
        }
    }
    settings.endGroup();
    setPageTemplate(pageTemplate);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("input"));
    const QMetaObject* inputMeta = m_input.metaObject();
    for (int i = inputMeta->propertyOffset(); i < inputMeta->propertyCount(); ++i) {
        readProperty(settings, &m_input, inputMeta->property(i));
    }
    settings.endGroup();

    applyEinkDefaults();
    emit toolChanged();
    emit textStyleChanged();
    emit buttonActionsChanged();
}

void PageCanvas::resetSettings() {
    finishInput();
    // What a canvas starts with
    const PageCanvas fresh;
    const QMetaObject* meta = metaObject();
    for (const char* name: CANVAS_PROPERTIES) {
        const QMetaProperty property = meta->property(meta->indexOfProperty(name));
        property.write(this, property.read(&fresh));
    }
    m_textFont = fresh.m_textFont;
    setLayoutColumns(1);
    m_toolStates = fresh.m_toolStates;
    m_tool = fresh.m_tool;
    m_buttonActions = fresh.m_buttonActions;
    m_buttonOptions = fresh.m_buttonOptions;
    setPageTemplate({});
    const QMetaObject* inputMeta = m_input.metaObject();
    for (int i = inputMeta->propertyOffset(); i < inputMeta->propertyCount(); ++i) {
        inputMeta->property(i).write(&m_input, inputMeta->property(i).read(&fresh.m_input));
    }
    applyEinkDefaults();
    emit toolChanged();
    emit textStyleChanged();
    emit buttonActionsChanged();
}
