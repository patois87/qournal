/*
 * Qournal
 *
 * The part of PageCanvas that handles the input of pen, mouse and fingers, and the tools they drive
 *
 * @license GNU GPLv2 or later
 */

#include <QCursor>
#include <QGuiApplication>
#include <QHoverEvent>
#include <QKeyEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPointingDevice>
#include <QStyleHints>
#include <QTabletEvent>
#include <QTouchEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>

#include "Eraser.h"
#include "PageCanvas.h"
#include "Platform.h"
#include "Renderer.h"
#include "ShapeRecognizer.h"

namespace {

constexpr int HIGHLIGHTER_ALPHA = 0x7f;
constexpr double KNOTS_ATTRACTION_RADIUS_PX = 10.0;  // around the first knot of a spline
constexpr double SPLINE_AID_WIDTH_PX = 2.0;
constexpr double DOUBLE_PRESS_DISTANCE_PX = 10.0;
constexpr double TAP_DISTANCE_PX = 5.0;
constexpr int LASER_FADE_STEP_MS = 50;
constexpr int LASER_FADE_ALPHA_STEP = 25;
constexpr double VERTICAL_SPACE_OPACITY = 0.3;
constexpr double MAX_VERTICAL_SPACE_IMAGE_PX = 8192.0;
constexpr int MAX_GEOMETRY_IMAGE_PX = 4096;
constexpr double NO_PRESSURE = StrokeBuilder::NO_PRESSURE;

// Keyboard control of splines and of the setsquare and compass, as in Xournal++
constexpr double SPLINE_SHIFT_AMOUNT = 1.0;
constexpr double SPLINE_ROTATE_AMOUNT = 5.0 * M_PI / 180.0;
constexpr double SPLINE_SCALE_AMOUNT = 1.05;
constexpr double SPLINE_MAX_TANGENT_LENGTH = 2000.0;
constexpr double SPLINE_MIN_TANGENT_LENGTH = 1.0;
constexpr double GEOMETRY_MOVE_AMOUNT = GeometryTool::HALF_CM / 2.0;
constexpr double GEOMETRY_MOVE_AMOUNT_SMALL = GeometryTool::HALF_CM / 20.0;
constexpr double GEOMETRY_ROTATE_AMOUNT = M_PI * 5.0 / 180.0;
constexpr double GEOMETRY_ROTATE_AMOUNT_SMALL = M_PI * 0.2 / 180.0;
constexpr double GEOMETRY_SCALE_AMOUNT = 1.1;
constexpr double GEOMETRY_SCALE_AMOUNT_SMALL = 1.01;

const QColor AID_COLOR(0xff, 0x00, 0x00);
const QColor SPLINE_KNOT_COLOR(0x80, 0x80, 0x80);
const QColor SPLINE_FIRST_KNOT_COLOR(0xff, 0x00, 0x00);
const QColor SPLINE_TANGENT_COLOR(0x7c, 0xfc, 0x00);

/// Widths of the strokes in points for the sizes very fine to very thick, as in Xournal++. Indexed by the tool
constexpr double THICKNESS[PageCanvas::TOOL_COUNT][5] = {
        {0.42, 0.85, 1.41, 2.26, 5.67},  // pen: 0.15, 0.3, 0.5, 0.8, 2 mm
        {1, 2.83, 8.50, 19.84, 30},      // highlighter
        {1, 2.83, 8.50, 12, 18},         // eraser
        {0, 0, 0, 0, 0},                 // hand
        {0, 0, 0, 0, 0},                 // vertical space
        {0.7, 1.41, 2.4, 4, 7},          // laser pointer (pen)
        {1, 2.83, 8.50, 19.84, 30},      // laser pointer (highlighter)
        {0, 0, 0, 0, 0},                 // select rectangle
        {0, 0, 0, 0, 0},                 // select region
        {0, 0, 0, 0, 0},                 // select object
        {0, 0, 0, 0, 0},                 // text
        {0, 0, 0, 0, 0},                 // link
        {0, 0, 0, 0, 0},                 // LaTeX
        {0, 0, 0, 0, 0},                 // select PDF text
        {0, 0, 0, 0, 0},                 // select PDF text in a rectangle
        {0, 0, 0, 0, 0},                 // play object
        {0, 0, 0, 0, 0},                 // image
};

constexpr int PEN_HOVER_MS = 500;

bool validMotion(const QPointF& p, const QPointF& q) {
    return QLineF(p, q).length() >= StrokeBuilder::MOTION_THRESHOLD;
}

}  // namespace

// Tools and their settings

void PageCanvas::setTool(Tool tool) {
    if (m_tool != tool) {
        finishInput();
        // The tools that work on what is under the pointer do not keep a selection, as in Xournal++
        if (tool == Pen || tool == Highlighter || tool == Eraser || tool == Text || tool == Link || tool == Latex ||
            tool == VerticalSpace || tool == Image) {
            clearSelection();
        }
        m_previousTool = m_tool;
        m_tool = tool;
        emit toolChanged();
    }
}

void PageCanvas::pencilTapped(int action) {
    switch (static_cast<Platform::PencilTap>(action)) {
        case Platform::PencilTap::SwitchEraser:
            setTool(m_tool == Eraser ? (m_previousTool == Eraser ? Pen : m_previousTool) : Eraser);
            break;
        case Platform::PencilTap::SwitchPrevious:
            setTool(m_previousTool);
            break;
        case Platform::PencilTap::ShowColorPalette:
            emit floatingToolboxRequested(m_pointerPos.x() >= 0 ? m_pointerPos : QPointF(width() / 2, height() / 3));
            break;
        case Platform::PencilTap::Ignore:
            break;
    }
}

void PageCanvas::setColor(const QColor& color) {
    // The selected elements take the colour as well; highlighter strokes stay transparent
    modifySelection([&](Element& element) {
        if (auto* stroke = std::get_if<Stroke>(&element)) {
            const int alpha = stroke->color.alpha();
            stroke->color = color;
            stroke->color.setAlpha(alpha);
        } else if (auto* text = std::get_if<TextElement>(&element)) {
            text->color = color;
        } else if (auto* link = std::get_if<LinkElement>(&element)) {
            link->color = color;
        }
    });
    setTextEditElement([&](TextElement& text) { text.color = color; });
    if (toolState().color != color) {
        toolState().color = color;
        emit toolChanged();
    }
}

void PageCanvas::setToolSize(ToolSize size) {
    // The selected strokes get the width their tool has at this size
    modifySelection([&](Element& element) {
        if (auto* stroke = std::get_if<Stroke>(&element)) {
            const Tool tool = stroke->tool == Stroke::Tool::Highlighter ? Highlighter :
                              stroke->tool == Stroke::Tool::Eraser      ? Eraser :
                                                                          Pen;
            const double width = thicknessOf(tool, size);
            for (double& w: stroke->widths) {
                w *= width / stroke->width;
            }
            stroke->width = width;
            stroke->updateBounds();
        }
    });
    if (toolState().size != size) {
        toolState().size = size;
        emit toolChanged();
    }
}

void PageCanvas::setDrawingType(DrawingType type) {
    if (toolState().drawingType != type) {
        finishInput();
        toolState().drawingType = type;
        emit toolChanged();
    }
}

void PageCanvas::setFill(bool fill) {
    modifySelection([&](Element& element) {
        auto* stroke = std::get_if<Stroke>(&element);
        if (stroke && stroke->tool != Stroke::Tool::Eraser) {
            stroke->fill = fill ? toolState().fillAlpha : -1;
        }
    });
    if (toolState().fill != fill) {
        toolState().fill = fill;
        emit toolChanged();
    }
}

void PageCanvas::setFillAlpha(int alpha) {
    alpha = std::clamp(alpha, 1, 255);
    if (toolState().fillAlpha != alpha) {
        toolState().fillAlpha = alpha;
        emit toolChanged();
    }
}

void PageCanvas::setLineStyle(const QString& style) {
    modifySelection([&](Element& element) {
        auto* stroke = std::get_if<Stroke>(&element);
        if (stroke && stroke->tool == Stroke::Tool::Pen) {
            stroke->setStyle(style);
        }
    });
    if (toolState().lineStyle != style) {
        toolState().lineStyle = style;
        emit toolChanged();
    }
}

void PageCanvas::setEraserType(EraserType type) {
    if (m_eraserType != type) {
        m_eraserType = type;
        emit toolChanged();
    }
}

double PageCanvas::thickness(Tool tool) const {
    return thicknessOf(tool, m_toolStates[static_cast<size_t>(tool)].size);
}

double PageCanvas::thicknessOf(Tool tool, ToolSize size) { return THICKNESS[tool][size]; }

/// Tool, colour, width, ... of the strokes a tool draws
Stroke PageCanvas::strokeStyle(Tool tool) const {
    const ToolState& state = m_toolStates[static_cast<size_t>(tool)];
    Stroke s;
    s.width = thickness(tool);
    s.color = state.color;
    switch (tool) {
        case Highlighter:
        case LaserHighlighter:
            s.tool = Stroke::Tool::Highlighter;
            s.color.setAlpha(HIGHLIGHTER_ALPHA);
            break;
        case Eraser:
            // The whiteout eraser paints
            s.tool = Stroke::Tool::Eraser;
            s.color = Qt::white;
            break;
        default:
            s.tool = Stroke::Tool::Pen;
            break;
    }
    if (tool == Pen || tool == Highlighter) {
        s.fill = state.fill ? state.fillAlpha : -1;
    }
    if (tool == Pen) {
        s.setStyle(state.lineStyle);
    }
    return s;
}

Snapper PageCanvas::snapper(int page) const {
    return Snapper(m_input.snapping(), m_doc.pages[static_cast<size_t>(page)]);
}

QPointF PageCanvas::viewToPage(int page, const QPointF& viewPos) const {
    return viewToWorld(viewPos) - m_pageRects[page].topLeft();
}

QRect PageCanvas::pageToViewRect(int page, const QRectF& rect) const {
    return QRectF(worldToView(rect.topLeft() + m_pageRects[page].topLeft()), rect.size() * m_scale)
            .adjusted(-2, -2, 2, 2)
            .toAlignedRect();
}

void PageCanvas::updatePageRect(int page, const QRectF& rect) {
    if (rect.isValid() && page >= 0 && page < m_pageRects.size()) {
        updateView(pageToViewRect(page, rect));
    }
}

// Events

bool PageCanvas::eventFilter(QObject* watched, QEvent* event) {
    switch (event->type()) {
        case QEvent::TabletPress:
        case QEvent::TabletMove:
        case QEvent::TabletRelease: {
            auto* tablet = static_cast<QTabletEvent*>(event);
            const QPointF pos = mapFromScene(tablet->position());
            // A menu or a dialog may lie over the pages: the pen presses its entries then, and does not draw
            // under it
            const bool overCanvas = isVisible() && isEnabled() && contains(pos) && (m_penDown || !popupOpen());
            // The pen places the cursor in the text that is being edited, like the mouse
            const bool overEditor = m_textEdit.active && textEditViewRect().contains(pos);
            if (m_penMouseDown) {
                // Pressed on a toolbar or the like: it stays there until it is lifted, also over the pages
                return sendPenAsMouse(tablet);
            }
            if (!m_penDown && (!overCanvas || overEditor)) {
                // Not for us, but for the toolbar etc., as a mouse event: Qt makes one of it on the desktop. On
                // Android it does not, so that the pen could not press a button: there it is done here
                return m_penClicksAsMouse ? sendPenAsMouse(tablet) : false;
            }
            const int deviceClass = deviceClassOf(tablet->pointingDevice());
            if (deviceClass == DeviceDisabled) {
                tablet->accept();
                return true;
            }
            if (deviceClass == DeviceMouse && !m_penDown) {
                return false;  // Qt turns it into mouse events, which are handled as those of a mouse
            }
            handleTablet(tablet, pos);
            tablet->accept();
            return true;
        }
        default:
            return QQuickItem::eventFilter(watched, event);
    }
}

// The pen of electronic paper

bool PageCanvas::einkPenWanted() const {
    if (!m_einkMode || !Platform::EinkPen::available() || !isVisible() || !window() || m_presentationMode ||
        m_textEdit.active || m_tool != Pen || toolState().drawingType != Freehand ||
        qGuiApp->applicationState() != Qt::ApplicationActive) {
        return false;
    }
    // A menu or a dialog is drawn by the application: it would not be seen
    return !popupOpen();
}

bool PageCanvas::popupOpen() const {
    if (!window()) {
        return false;
    }
    // Menus and dialogs are items of the overlay of the window, which is above everything else
    const QList<QQuickItem*> top = window()->contentItem()->childItems();
    for (const QQuickItem* item: top) {
        if (qstrcmp(item->metaObject()->className(), "QQuickOverlay") == 0) {
            const QList<QQuickItem*> popups = item->childItems();
            if (std::any_of(popups.begin(), popups.end(), [](const QQuickItem* popup) { return popup->isVisible(); })) {
                return true;
            }
        }
    }
    return false;
}

void PageCanvas::einkPenHold(int ms) {
    if (!m_einkPenTick.isActive()) {
        return;
    }
    m_einkPenHoldUntil = std::max(m_einkPenHoldUntil, m_einkPenClock.elapsed() + ms);
    einkPenUpdate();
}

void PageCanvas::einkPenUpdate() {
    const qint64 now = m_einkPenClock.elapsed();
    if (m_einkPenOn && m_einkPenSyncAt > 0 && now >= m_einkPenSyncAt && !m_penDown) {
        // Written, and the pen rests: off for a moment, so that the screen takes what the application drew
        m_einkPenSyncAt = 0;
        m_einkPenHoldUntil = std::max(m_einkPenHoldUntil, now + 400);
    }
    const bool on = einkPenWanted() && now >= m_einkPenHoldUntil;
    if (on == m_einkPenOn) {
        return;
    }
    m_einkPenOn = on;
    if (on) {
        const double ratio = window()->effectiveDevicePixelRatio();
        const QRectF area(mapToGlobal(QPointF(0, 0)) * ratio, size() * ratio);
        Platform::EinkPen::start(area.toRect(), std::max(1.0, thickness(Pen) * m_scale * ratio), toolState().color);
    } else {
        Platform::EinkPen::pause();
        update();
    }
}

bool PageCanvas::sendPenAsMouse(QTabletEvent* tablet) {
    QEvent::Type type = QEvent::MouseMove;
    Qt::MouseButton button = Qt::NoButton;
    if (tablet->type() == QEvent::TabletPress) {
        type = QEvent::MouseButtonPress;
        button = Qt::LeftButton;
        m_penMouseDown = true;
    } else if (tablet->type() == QEvent::TabletRelease) {
        type = QEvent::MouseButtonRelease;
        button = Qt::LeftButton;
        m_penMouseDown = false;
    }
    QMouseEvent mouse(type, tablet->position(), tablet->globalPosition(), button,
                      m_penMouseDown ? Qt::LeftButton : Qt::NoButton, tablet->modifiers());
    if (QQuickWindow* target = window()) {
        QCoreApplication::sendEvent(target, &mouse);
    }
    tablet->accept();
    return true;
}

void PageCanvas::handleTablet(QTabletEvent* event, const QPointF& pos) {
    m_penSeen.restart();

    const QPointingDevice* device = event->pointingDevice();
    const int deviceClass = deviceClassOf(device);
    const bool hasPressure = device->hasCapability(QInputDevice::Capability::Pressure);
    const bool eraserTip = event->pointerType() == QPointingDevice::PointerType::Eraser || deviceClass == DeviceEraser;
    // A stylus used as a touchscreen moves the view, unless fingers draw
    if (deviceClass == DeviceTouchscreen && !m_fingerDraws) {
        if (event->type() == QEvent::TabletPress) {
            m_devicePanning = true;
            m_lastPan = pos;
        } else if (event->type() == QEvent::TabletMove && m_devicePanning) {
            panBy(pos - m_lastPan);
            m_lastPan = pos;
        } else if (event->type() == QEvent::TabletRelease) {
            m_devicePanning = false;
        }
        return;
    }
    const PointerInput input{pos, hasPressure ? event->pressure() : -1.0, static_cast<quint64>(m_inputClock.elapsed()),
                             event->modifiers()};
    showStylusCursor();

    // The first events of a stroke can be left out (Xournal++: numIgnoredStylusEvents): the stroke begins later
    QEvent::Type type = event->type();
    if (type == QEvent::TabletPress && m_input.ignoredStylusEvents > 0) {
        m_stylusEventsToIgnore = m_input.ignoredStylusEvents;
        return;
    }
    if (type == QEvent::TabletMove && m_stylusEventsToIgnore > 0 && event->buttons() != Qt::NoButton) {
        if (--m_stylusEventsToIgnore > 0) {
            return;
        }
        type = QEvent::TabletPress;
    }
    if (type == QEvent::TabletRelease) {
        m_stylusEventsToIgnore = 0;
    }

    switch (type) {
        case QEvent::TabletPress: {
            // The eraser tip and the side buttons of the stylus have tools of their own
            std::optional<Tool> buttonTool;
            bool used = true;
            if (event->buttons() & Qt::RightButton) {
                used = resolveButton(ButtonStylus1, pos, buttonTool);
            } else if (event->buttons() & Qt::MiddleButton) {
                used = resolveButton(ButtonStylus2, pos, buttonTool);
            } else if (eraserTip) {
                used = resolveButton(ButtonEraserTip, pos, buttonTool);
            }
            if (used) {
                pointerPress(input, buttonTool);
                m_penDown = true;
            }
            // The device draws strokes of the pen only: not what a button or the eraser end does, and no shapes
            if (buttonTool || eraserTip || m_action != Action::Draw) {
                einkPenHold(800);
            }
            m_einkPenSyncAt = 0;
            break;
        }
        case QEvent::TabletMove:
            if (m_penDown) {
                pointerMove(input);
            } else {
                pointerHover(input);
            }
            break;
        case QEvent::TabletRelease:
            if (m_penDown && event->buttons() == Qt::NoButton) {
                pointerRelease(input);
                m_penDown = false;
                // A while after the last stroke, what was written is shown as the application draws it
                m_einkPenSyncAt = m_einkPenClock.elapsed() + 900;
            }
            break;
        default:
            break;
    }

    QString name = device->name().isEmpty() ? tr("Stylus") : device->name();
    if (eraserTip) {
        name += tr(" (eraser tip)");
    }
    noteInput(name, event->pressure(), hasPressure);
}

void PageCanvas::showStylusCursor() {
    if (m_einkMode) {
        setCursor(QCursor(Qt::BlankCursor));  // a pointer that follows the pen leaves traces on electronic paper
        return;
    }
    // As in Xournal++: a dot as large as the stroke will be wide, in the colour of the tool, so that it shows
    // where and how the pen will draw. It was a dot of a fixed size with a border, much larger than a fine
    // stroke. "Big" adds a pencil to it, for those who lose sight of the dot
    const bool draws = m_tool == Pen || m_tool == Highlighter;
    const double diameter = draws ? std::clamp(thickness(m_tool) * m_scale, 2.0, 90.0) : 4.0;
    QColor color = m_toolStates[static_cast<size_t>(m_tool)].color;
    if (m_tool == Highlighter && color.alpha() == 255) {
        color.setAlpha(120);
    }
    const double ratio = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    // Made anew only when it looks different: this is called with every event of the pen
    const QString key = QStringLiteral("%1 %2 %3 %4 %5")
                                .arg(int(m_input.stylusCursor))
                                .arg(color.rgba())
                                .arg(qRound(diameter * 8))
                                .arg(ratio)
                                .arg(int(m_tool));
    if (m_stylusCursorShown && key == m_stylusCursorKey) {
        return;
    }
    m_stylusCursorKey = key;
    QCursor cursor(Qt::ArrowCursor);
    switch (m_input.stylusCursor) {
        case InputSettings::StylusCursorNone:
            cursor = QCursor(Qt::BlankCursor);
            break;
        case InputSettings::StylusCursorDot:
        case InputSettings::StylusCursorBig: {
            const bool big = m_input.stylusCursor == InputSettings::StylusCursorBig;
            // The dot is in the middle; the pencil of "Big" needs room up and to the right of it
            const int side = 2 * qCeil(std::max(diameter / 2 + 1, big ? 21.0 : 0.0)) + 1;
            QPixmap pixmap(QSize(side, side) * ratio);
            pixmap.setDevicePixelRatio(ratio);
            pixmap.fill(Qt::transparent);
            QPainter p(&pixmap);
            p.setRenderHint(QPainter::Antialiasing);
            const QPointF center(side / 2.0, side / 2.0);
            if (big) {
                QPolygonF pencil;
                pencil << center + QPointF(2, 0) << center + QPointF(2, -4) << center + QPointF(15, -17.5)
                       << center + QPointF(19, -14) << center + QPointF(6, 0);
                p.setPen(QPen(Qt::black, 1.2));
                p.setBrush(Qt::white);
                p.drawPolygon(pencil);
            }
            p.setPen(Qt::NoPen);
            p.setBrush(color);
            p.drawEllipse(center, diameter / 2, diameter / 2);
            p.end();
            cursor = QCursor(pixmap, side / 2, side / 2);
            break;
        }
        case InputSettings::StylusCursorArrow:
            break;
    }
    setCursor(cursor);
    m_stylusCursorShown = true;
}

int PageCanvas::deviceClassOf(const QInputDevice* device) {
    if (!device) {
        return DeviceMouse;
    }
    if (!device->name().isEmpty()) {
        m_seenDevices.insert(device->name(), device->type());
    }
    const QVariant chosen = m_input.deviceClasses.value(device->name());
    if (chosen.isValid() && chosen.toInt() >= DeviceDisabled && chosen.toInt() <= DeviceTouchscreen) {
        return chosen.toInt();
    }
    switch (device->type()) {
        case QInputDevice::DeviceType::TouchScreen:
            return DeviceTouchscreen;
        case QInputDevice::DeviceType::Stylus:
        case QInputDevice::DeviceType::Airbrush:
        case QInputDevice::DeviceType::Puck:
            return DevicePen;
        default:
            return DeviceMouse;
    }
}

QVariantList PageCanvas::inputDevices() const {
    QHash<QString, QInputDevice::DeviceType> devices = m_seenDevices;
    for (const QInputDevice* device: QInputDevice::devices()) {
        if (!device->name().isEmpty() && device->type() != QInputDevice::DeviceType::Keyboard) {
            devices.insert(device->name(), device->type());
        }
    }
    QStringList names = devices.keys();
    names.sort(Qt::CaseInsensitive);
    QVariantList result;
    for (const QString& name: std::as_const(names)) {
        const QInputDevice::DeviceType type = devices.value(name);
        int automatic = DeviceMouse;
        QString typeName = tr("Mouse");
        switch (type) {
            case QInputDevice::DeviceType::TouchScreen:
                automatic = DeviceTouchscreen;
                typeName = tr("Touchscreen");
                break;
            case QInputDevice::DeviceType::TouchPad:
                typeName = tr("Touchpad");
                break;
            case QInputDevice::DeviceType::Stylus:
            case QInputDevice::DeviceType::Airbrush:
            case QInputDevice::DeviceType::Puck:
                automatic = DevicePen;
                typeName = tr("Pen");
                break;
            default:
                break;
        }
        const QVariant chosen = m_input.deviceClasses.value(name);
        result.append(QVariantMap{{QStringLiteral("name"), name},
                                  {QStringLiteral("type"), typeName},
                                  {QStringLiteral("deviceClass"), chosen.isValid() ? chosen.toInt() : -1},
                                  {QStringLiteral("automaticClass"), automatic}});
    }
    return result;
}

void PageCanvas::setDeviceClass(const QString& name, int deviceClass) {
    if (deviceClass < DeviceDisabled || deviceClass > DeviceTouchscreen) {
        m_input.deviceClasses.remove(name);
    } else {
        m_input.deviceClasses.insert(name, deviceClass);
    }
    emit m_input.changed();
}

bool PageCanvas::resolveButton(Button button, const QPointF& pos, std::optional<Tool>& tool) {
    restoreButtonState();
    const int action = buttonAction(button);
    if (action == ButtonFloatingToolbox) {
        emit floatingToolboxRequested(pos);
        return false;
    }
    if (action >= 0 && action < static_cast<int>(m_toolStates.size())) {
        tool = static_cast<Tool>(action);
        // The tool as the button wants it, until the button is released
        const ButtonOptions& options = m_buttonOptions[static_cast<size_t>(button)];
        ToolState& state = m_toolStates[static_cast<size_t>(action)];
        if (options.drawingType >= 0 || options.size >= 0 || options.color.isValid()) {
            m_savedToolState = {*tool, state};
            if (options.drawingType >= 0) {
                state.drawingType = static_cast<DrawingType>(options.drawingType);
            }
            if (options.size >= 0) {
                state.size = static_cast<ToolSize>(options.size);
            }
            if (options.color.isValid()) {
                state.color = options.color;
            }
        }
    }
    return true;
}

void PageCanvas::restoreButtonState() {
    if (m_savedToolState) {
        m_toolStates[static_cast<size_t>(m_savedToolState->first)] = m_savedToolState->second;
        m_savedToolState.reset();
    }
}

QVariantMap PageCanvas::buttonOptions(Button button) const {
    const ButtonOptions& options = m_buttonOptions[static_cast<size_t>(button)];
    return {{QStringLiteral("drawingType"), options.drawingType},
            {QStringLiteral("size"), options.size},
            {QStringLiteral("color"), options.color}};
}

void PageCanvas::setButtonOptions(Button button, const QVariantMap& map) {
    const auto index = static_cast<size_t>(button);
    if (index >= m_buttonOptions.size()) {
        return;
    }
    restoreButtonState();
    ButtonOptions& options = m_buttonOptions[index];
    if (map.contains(QStringLiteral("drawingType"))) {
        options.drawingType =
                std::clamp(map.value(QStringLiteral("drawingType")).toInt(), -1, static_cast<int>(ShapeRecognizer));
    }
    if (map.contains(QStringLiteral("size"))) {
        options.size = std::clamp(map.value(QStringLiteral("size")).toInt(), -1, static_cast<int>(VeryThick));
    }
    if (map.contains(QStringLiteral("color"))) {
        // Fully transparent stands for none: QML has no invalid colour
        const QColor color = map.value(QStringLiteral("color")).value<QColor>();
        options.color = color.isValid() && color.alpha() > 0 ? color : QColor();
    }
    emit buttonActionsChanged();
}

void PageCanvas::setButtonAction(Button button, int action) {
    const bool valid = action == ButtonNoAction || action == ButtonFloatingToolbox ||
                       (action >= 0 && action < static_cast<int>(m_toolStates.size()));
    const auto index = static_cast<size_t>(button);
    if (valid && index < m_buttonActions.size() && m_buttonActions[index] != action) {
        m_buttonActions[index] = action;
        emit buttonActionsChanged();
    }
}

void PageCanvas::mousePressEvent(QMouseEvent* event) {
    einkPenHold();
    noteInput(tr("Mouse"), 0, false);
    if (m_stylusCursorShown) {
        unsetCursor();
        m_stylusCursorShown = false;
    }
    event->accept();
    if (m_mouseDrawing || m_panning) {
        return;  // a second button while the first one is still down
    }
    const int deviceClass = deviceClassOf(event->device());
    if (deviceClass == DeviceDisabled) {
        return;
    }
    if (deviceClass == DeviceTouchscreen && !m_fingerDraws) {
        // A mouse used as a touchscreen moves the view
        m_panning = true;
        m_lastPan = event->position();
        return;
    }
    const bool pen = deviceClass == DevicePen;
    if (pen) {
        m_penSeen.restart();  // palm rejection
    }
    std::optional<Tool> buttonTool;
    if (deviceClass == DeviceEraser && event->button() == Qt::LeftButton &&
        !resolveButton(ButtonEraserTip, event->position(), buttonTool)) {
        return;
    }
    if (event->button() != Qt::LeftButton) {
        // A mouse used as a pen has the buttons of the stylus
        Button button = pen ? ButtonStylus1 : ButtonMouseRight;
        switch (event->button()) {
            case Qt::MiddleButton:
                button = pen ? ButtonStylus2 : ButtonMouseMiddle;
                break;
            case Qt::RightButton:
                break;
            case Qt::BackButton:
                button = ButtonMouse4;
                break;
            case Qt::ForwardButton:
                button = ButtonMouse5;
                break;
            default:
                return;
        }
        if (!resolveButton(button, event->position(), buttonTool)) {
            return;
        }
        if (buttonTool == Hand) {
            // Moves the view, also where the hand tool would do something else (links, setsquare)
            m_panning = true;
            m_lastPan = event->position();
            return;
        }
    }
    pointerPress({event->position(), -1.0, static_cast<quint64>(m_inputClock.elapsed()), event->modifiers()},
                 buttonTool);
    m_mouseDrawing = true;
    m_mouseButton = event->button();
}

void PageCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_mouseDrawing) {
        pointerMove({event->position(), -1.0, static_cast<quint64>(m_inputClock.elapsed()), event->modifiers()});
    } else if (m_panning) {
        panBy(event->position() - m_lastPan);
        m_lastPan = event->position();
    }
    noteInput(tr("Mouse"), 0, false);
}

void PageCanvas::mouseReleaseEvent(QMouseEvent* event) {
    if (m_mouseDrawing && event->button() == m_mouseButton) {
        pointerRelease({event->position(), -1.0, static_cast<quint64>(m_inputClock.elapsed()), event->modifiers()});
        m_mouseDrawing = false;
    } else if (event->buttons() == Qt::NoButton) {
        m_panning = false;
    }
}

void PageCanvas::hoverMoveEvent(QHoverEvent* event) {
    // While the pen is in use it tells where the pointer is. Qt Quick sends hover events of the mouse on its
    // own, with the place the mouse was seen at last
    if (m_penSeen.isValid() && m_penSeen.elapsed() < PEN_HOVER_MS) {
        return;
    }
    if (m_stylusCursorShown) {
        unsetCursor();
        m_stylusCursorShown = false;
    }
    pointerHover({event->position(), -1.0, static_cast<quint64>(m_inputClock.elapsed()), event->modifiers()});
}

void PageCanvas::wheelEvent(QWheelEvent* event) {
    einkPenHold();
    if (event->modifiers() & Qt::ControlModifier) {
        const double step = 1.0 + std::max(m_input.wheelZoomStep, 1.0) / 100.0;
        zoomAt(event->position(), std::pow(step, event->angleDelta().y() / 120.0));
    } else if (m_presentationMode && scrollBounds().width() <= width() / m_scale + 1e-6 &&
               scrollBounds().height() <= height() / m_scale + 1e-6) {
        // The whole page is visible: the wheel turns the pages
        m_wheelPages -= event->angleDelta().y() / 120.0;
        const int pages = static_cast<int>(m_wheelPages);
        if (pages != 0) {
            setCurrentPage(m_currentPage + pages);
            m_wheelPages = 0;
        }
    } else {
        QPointF delta = event->pixelDelta();
        if (delta.isNull()) {
            delta = QPointF(event->angleDelta()) / 120.0 * 60.0;
        }
        panBy(delta);
    }
    event->accept();
}

void PageCanvas::touchEvent(QTouchEvent* event) {
    einkPenHold();
    event->accept();
    const auto& points = event->points();
    const int deviceClass = deviceClassOf(event->device());
    if (deviceClass == DeviceDisabled) {
        m_touchPositions.clear();
        return;
    }
    // A touchscreen used as a pen, a mouse or an eraser draws with one finger
    const bool draws =
            m_fingerDraws || deviceClass == DevicePen || deviceClass == DeviceMouse || deviceClass == DeviceEraser;

    const bool finished = event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel;
    if (m_touchDrawing && (finished || points.size() != 1)) {
        pointerRelease({m_lastTouch, -1.0, static_cast<quint64>(m_inputClock.elapsed()), event->modifiers()});
        m_touchDrawing = false;
    }

    // Palm rejection: ignore touches while the pen is in use. Not for a touchscreen that is used as the pen
    if (deviceClass == DeviceTouchscreen && (m_penDown || (m_input.palmRejection && m_penSeen.isValid() &&
                                                           m_penSeen.elapsed() < m_input.palmRejectionTime))) {
        m_geometryTouch = false;
        m_touchPositions.clear();
        return;
    }
    // Fingers on the setsquare or compass move it
    if (geometryTouch(event)) {
        m_touchPositions.clear();
        return;
    }
    if (finished || points.isEmpty()) {
        m_touchPositions.clear();
        return;
    }
    noteInput(tr("Touch (%n finger(s))", nullptr, static_cast<int>(points.size())), 0, false);

    // Where the fingers were when the last event was handled. Qt Quick may merge several events of a frame into
    // one, so the previous position of an event point is not the one that was seen here
    QList<QPointF> positions;
    bool continued = points.size() == m_touchPositions.size();
    for (const QEventPoint& point: points) {
        positions.append(point.position());
        continued = continued && point.state() != QEventPoint::Pressed;
    }
    const QList<QPointF> last = continued ? m_touchPositions : positions;
    m_touchPositions = positions;

    if (points.size() == 1) {
        const QEventPoint& point = points.first();
        const PointerInput input{point.position(), -1.0, static_cast<quint64>(m_inputClock.elapsed()),
                                 event->modifiers()};
        if (draws) {
            m_lastTouch = point.position();
            if (point.state() == QEventPoint::Pressed) {
                std::optional<Tool> eraser;
                if (deviceClass == DeviceEraser) {
                    eraser = Eraser;
                }
                pointerPress(input, eraser);
                m_touchDrawing = true;
            } else if (m_touchDrawing && point.state() == QEventPoint::Updated) {
                pointerMove(input);
            }
        } else {
            panBy(point.position() - last[0]);
        }
        return;
    }

    // Two fingers: pinch to zoom, drag to pan
    const QEventPoint& a = points[0];
    const QEventPoint& b = points[1];
    if (a.state() == QEventPoint::Pressed || b.state() == QEventPoint::Pressed) {
        // Zooming begins once the fingers moved apart or together enough (Xournal++: touchZoomStartThreshold)
        m_pinchStart = QLineF(a.position(), b.position()).length();
        m_pinchZooming = m_input.touchZoomThreshold <= 0;
        return;
    }
    const QPointF center = (a.position() + b.position()) / 2;
    const QPointF lastCenter = (last[0] + last[1]) / 2;
    const double distance = QLineF(a.position(), b.position()).length();
    const double lastDistance = QLineF(last[0], last[1]).length();
    if (!m_pinchZooming && m_pinchStart > 1.0 &&
        std::abs(distance / m_pinchStart - 1) * 100 >= m_input.touchZoomThreshold) {
        m_pinchZooming = true;
    }
    if (lastDistance > 1.0 && m_input.zoomGestures && m_pinchZooming) {
        zoomAt(center, distance / lastDistance);
    }
    panBy(center - lastCenter);
}

bool PageCanvas::event(QEvent* event) {
    if (event->type() == QEvent::ShortcutOverride) {
        // Keys that control a spline or the setsquare and compass are not shortcuts of the application then
        const int key = static_cast<QKeyEvent*>(event)->key();
        const bool arrow = key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down;
        const bool spline = m_spline.active && (arrow || key == Qt::Key_Escape || key == Qt::Key_Backspace ||
                                                key == Qt::Key_R || key == Qt::Key_S);
        const bool geometry = m_geometryToolType != NoGeometryTool &&
                              (arrow || key == Qt::Key_R || key == Qt::Key_S || key == Qt::Key_M);
        const bool selection = m_selection.active &&
                               (arrow || key == Qt::Key_Escape || key == Qt::Key_Delete || key == Qt::Key_Backspace);
        if (spline || geometry || selection) {
            event->accept();
            return true;
        }
    }
    return QQuickItem::event(event);
}

void PageCanvas::keyPressEvent(QKeyEvent* event) {
    if (m_spline.active && splineKey(event)) {
        return;
    }
    if (m_selection.active && selectionKey(event)) {
        return;
    }
    if (m_geometryToolType != NoGeometryTool && geometryKey(event)) {
        return;
    }
    if (m_action == Action::Shape) {
        // The modifier keys change the shape while it is drawn
        if (event->key() == Qt::Key_Shift) {
            m_shapeModifiers.shift = true;
        } else if (event->key() == Qt::Key_Control) {
            m_shapeModifiers.control = true;
        } else if (event->key() == Qt::Key_Alt) {
            m_shapeModifiers.alt = true;
        }
        updateShape();
    }
    event->ignore();
}

void PageCanvas::keyReleaseEvent(QKeyEvent* event) {
    if (m_action == Action::Shape) {
        if (event->key() == Qt::Key_Shift) {
            m_shapeModifiers.shift = false;
        } else if (event->key() == Qt::Key_Control) {
            m_shapeModifiers.control = false;
        } else if (event->key() == Qt::Key_Alt) {
            m_shapeModifiers.alt = false;
        }
        updateShape();
    }
    event->ignore();
}

// The pointer

PageCanvas::PointerInput PageCanvas::withGuessedPressure(const PointerInput& raw, bool press) {
    if (raw.pressure >= 0 || !m_input.pressureGuessing || !m_usePressure) {
        return raw;
    }
    // As in Xournal++ (PenInputHandler::inferPressureValue): the slower the pointer, the higher the pressure
    PointerInput input = raw;
    if (press) {
        m_guessedPressure = 0;
        m_guessPos = raw.pos;
        m_guessTime = raw.timestamp;
    }
    const double dt = static_cast<double>(raw.timestamp - m_guessTime) / 10.0;
    const double distance = QLineF(raw.pos, m_guessPos).length();
    const double inverseSpeed = dt / (distance + 0.001);
    double pressure = 3.142 / 2.0 + std::atan(inverseSpeed * 3.14 - 1.3);
    pressure = std::min(pressure, 2.0) / 5.0 + m_guessedPressure * 4.0 / 5.0;
    if (distance == 0) {
        pressure = std::sqrt(dt / 10.0) - 0.1;
    }
    m_guessedPressure = pressure;
    m_guessPos = raw.pos;
    m_guessTime = raw.timestamp;
    input.pressure = std::max((pressure * 1.1 + 0.8) / 2.0, 0.0);
    return input;
}

bool PageCanvas::isFilteredTap(const PointerInput& input) const {
    if (!m_input.strokeFilterEnabled) {
        return false;
    }
    // As in Xournal++ (PenInputHandler::isCurrentTapSelection)
    const double pixelsPerMm = m_scale * 72.0 / 25.4;
    const bool noMovement = QLineF(m_pressPos, input.pos).length() < m_input.strokeFilterLength * pixelsPerMm;
    const bool fastEnough = input.timestamp - m_pressTime < static_cast<quint64>(m_input.strokeFilterTime);
    const bool notAnAftershock =
            !m_hasLastStrokeEnd || m_pressTime - m_lastStrokeEnd > static_cast<quint64>(m_input.strokeFilterSuccessive);
    return noMovement && fastEnough && notAnAftershock;
}

Shapes::Modifiers PageCanvas::shapeModifiers() {
    Shapes::Modifiers modifiers = m_shapeModifiers;
    const bool applies = m_drawingType == Rectangle || m_drawingType == Ellipse || m_drawingType == CoordinateSystem;
    if (!m_input.drawDirModsEnabled || !applies) {
        return modifiers;
    }
    // As in Xournal++ (BaseShapeHandler::modifyModifiersByDrawDir): to the left is Shift, upwards is Control,
    // each turned around by the key itself. Once the pointer is far enough from the start, the direction is kept
    const QPointF delta = m_shapeCurrent - m_shapeStart;
    if (!m_dirModsFixed) {
        m_dirShift = delta.x() < 0;
        m_dirControl = delta.y() < 0;
        const double radius = std::max(m_input.drawDirModsRadius, 1) / m_scale;
        m_dirModsFixed = std::abs(delta.x()) > radius || std::abs(delta.y()) > radius;
    }
    modifiers.shift = modifiers.shift != m_dirShift;
    modifiers.control = modifiers.control != m_dirControl;
    return modifiers;
}

void PageCanvas::pointerPress(const PointerInput& rawInput, std::optional<Tool> buttonTool) {
    if (inputActive()) {
        finishInput();
    }
    const PointerInput input = withGuessedPressure(rawInput, true);
    m_pressPos = input.pos;
    m_pressTime = input.timestamp;
    m_dirModsFixed = false;
    forceActiveFocus();  // for the keys of the tools

    const Tool tool = buttonTool.value_or(m_tool);
    const bool erase = tool == Eraser && buttonTool;
    m_pressTool = tool;
    m_pressAudio = currentAudio();
    const ToolState& state = m_toolStates[static_cast<size_t>(tool)];
    const int page = pageAt(viewToWorld(input.pos));

    // A spline goes on with every press, as long as nothing else is done
    const bool continuesSpline = m_spline.active && page == m_spline.page && (tool == Pen || tool == Highlighter) &&
                                 state.drawingType == Spline;
    if (m_spline.active && !continuesSpline) {
        finishSpline();
    }

    // A selection is grabbed with any tool; a press next to it drops it
    // A press ends the editing of a text; with the text tool it goes on where the press is
    if (m_textEdit.active) {
        finishTextEdit();
    }
    clearPdfSelection();
    if (m_selection.active) {
        const Handle handle = erase ? Handle::None : selectionHandleAt(input.pos);
        if (handle != Handle::None) {
            m_action = Action::SelectionGesture;
            m_curPage = m_selection.page;
            m_selection.handle = handle;
            m_selection.start = viewToPage(m_selection.page, input.pos);
            m_selection.pending = QTransform();
            m_selection.pendingWidthFactor = 1.0;
            m_selection.gestureRect = m_selection.rect;
            m_selection.gestureRotation = m_selection.rotation;
            return;
        }
        clearSelection();
    }

    if (geometryPress(input, tool)) {
        return;
    }
    if (tool == Hand) {
        m_action = Action::Hand;
        m_lastPan = input.pos;
        m_handPressPos = input.pos;
        return;
    }
    if (page < 0) {
        return;
    }
    m_curPage = page;
    setCurrentPageInternal(page);
    const QPointF pagePos = viewToPage(page, input.pos);

    switch (tool) {
        case Eraser:
            if (m_eraserType == EraseWhiteout) {
                m_drawingType = Freehand;
                startDrawing(input, strokeStyle(Eraser));
            } else {
                m_action = Action::Erase;
                m_group.clear();
                eraseAt(pagePos);
            }
            break;
        case LaserPen:
        case LaserHighlighter:
            m_laserFadeDelay.stop();
            m_laserFade.stop();
            m_laserAlpha = 255;
            startDrawing(input, strokeStyle(tool));
            m_action = Action::Laser;
            break;
        case VerticalSpace:
            startVerticalSpace(input);
            break;
        case SelectRect:
        case SelectRegion:
        case SelectObject:
            selectPress(input);
            break;
        case SelectPdfTextLinear:
        case SelectPdfTextRect:
            pdfSelectPress(input);
            break;
        case Text:
            textPress(input);
            break;
        case PlayObject:
            playObjectPress(input);
            break;
        case Image:
            emit imageRequested(input.pos);
            break;
        case Link:
            linkPress(input);
            break;
        case Latex:
            latexPress(input);
            break;
        case Pen:
        case Highlighter:
            m_drawingType = state.drawingType;
            if (m_drawingType == Freehand || m_drawingType == ShapeRecognizer) {
                startDrawing(input, strokeStyle(tool));
            } else if (m_drawingType == Spline) {
                splinePress(input);
            } else {
                m_action = Action::Shape;
                m_shape = strokeStyle(tool);
                m_shapeModifiers = {bool(input.modifiers & Qt::AltModifier), bool(input.modifiers & Qt::ShiftModifier),
                                    bool(input.modifiers & Qt::ControlModifier)};
                m_shapeStart = snapper(page).snapToGrid(pagePos, m_shapeModifiers.alt);
                m_shapeCurrent = m_shapeStart;
            }
            break;
        default:
            break;
    }
}

void PageCanvas::pointerMove(const PointerInput& rawInput) {
    const PointerInput input = withGuessedPressure(rawInput, false);
    notePointer(input.pos);
    switch (m_action) {
        case Action::Draw:
        case Action::Laser:
            m_stabilizer->processEvent(stabilizerEvent(input), input.timestamp);
            updatePageRect(m_curPage, m_builder.takeDirtyRect());
            break;
        case Action::Shape: {
            const QPointF pagePos = viewToPage(m_curPage, input.pos);
            if (validMotion(pagePos, m_shapeCurrent)) {
                m_shapeCurrent = pagePos;
                m_shapeModifiers = {bool(input.modifiers & Qt::AltModifier), bool(input.modifiers & Qt::ShiftModifier),
                                    bool(input.modifiers & Qt::ControlModifier)};
                updateShape();
            }
            break;
        }
        case Action::Spline:
            splineMove(input);
            break;
        case Action::Erase: {
            // A fast pen leaves gaps between its events: erase along the way, in steps the eraser covers
            const QPointF target = viewToPage(m_curPage, input.pos);
            const QPointF from = m_eraserPos;
            const double distance = QLineF(from, target).length();
            const int steps = std::max(1, static_cast<int>(std::ceil(distance / thickness(Eraser))));
            for (int i = 1; i <= steps; ++i) {
                eraseAt(from + (target - from) * (static_cast<double>(i) / steps));
            }
            break;
        }
        case Action::Hand:
            panBy(input.pos - m_lastPan);
            m_lastPan = input.pos;
            break;
        case Action::VerticalSpace: {
            const double y = snapper(m_curPage).snapVertically(viewToPage(m_curPage, input.pos).y());
            if (y != m_vertical.endY) {
                m_vertical.endY = y;
                updateView();
            }
            break;
        }
        case Action::GeometryStroke:
        case Action::GeometryMove:
            geometryMove(input);
            break;
        case Action::Select: {
            const QPointF pagePos = viewToPage(m_curPage, input.pos);
            if (m_pressTool == SelectRegion) {
                m_selectPoints.append(pagePos);
            } else {
                m_selectPoints = {m_selectPoints.first(), pagePos};
            }
            updateView();
            break;
        }
        case Action::SelectionGesture:
            selectionGestureMove(input);
            // At the edge of the view, the view moves along (Xournal++: edgePanSpeed)
            m_edgePanInput = input;
            if (m_input.edgePanSpeed > 0 && !m_edgePan.isActive()) {
                m_edgePanClock.restart();
                m_edgePan.start(16);
            }
            break;
        case Action::PdfSelect:
            pdfSelectMove(input);
            break;
        case Action::None:
            break;
    }
}

void PageCanvas::edgePanStep() {
    if (m_action != Action::SelectionGesture || m_input.edgePanSpeed <= 0) {
        m_edgePan.stop();
        return;
    }
    const double seconds = std::min(m_edgePanClock.restart(), qint64(100)) / 1000.0;
    // How deep the pointer is in the edge, from 0 to 1 and beyond, on each axis
    const QPointF pos = m_edgePanInput.pos;
    const QSizeF edge(std::max(width() * 0.05, 20.0), std::max(height() * 0.05, 20.0));
    auto depth = [](double p, double length, double margin) {
        if (p < margin) {
            return -(margin - p) / margin;
        }
        if (p > length - margin) {
            return (p - (length - margin)) / margin;
        }
        return 0.0;
    };
    const double dx = depth(pos.x(), width(), edge.width());
    const double dy = depth(pos.y(), height(), edge.height());
    if (dx == 0 && dy == 0) {
        return;
    }
    auto speed = [&](double d, double length) {
        const double factor = 1 + (std::max(m_input.edgePanMaxMult, 1.0) - 1) * std::min(std::abs(d), 1.0);
        return (d < 0 ? -1 : 1) * m_input.edgePanSpeed / 100.0 * length * factor * seconds;
    };
    panBy(QPointF(dx ? -speed(dx, width()) : 0, dy ? -speed(dy, height()) : 0));
    // The selection stays under the pointer
    selectionGestureMove(m_edgePanInput);
}

void PageCanvas::pointerRelease(const PointerInput& rawInput) {
    const PointerInput input = withGuessedPressure(rawInput, false);
    // A tap is not a stroke: nothing is drawn, and what is under it is selected
    const bool penStroke = m_action == Action::Draw && (m_pressTool == Pen || m_pressTool == Highlighter);
    if (penStroke && isFilteredTap(input)) {
        const int page = m_curPage;
        cancelInput();
        updateView();
        if (m_input.strokeFilterSelects && page >= 0) {
            selectObjectAt(page, viewToPage(page, input.pos));
        }
        return;
    }
    if (penStroke) {
        m_lastStrokeEnd = input.timestamp;
        m_hasLastStrokeEnd = true;
    }
    switch (m_action) {
        case Action::Spline:
            splineRelease(input);
            break;
        case Action::SelectionGesture:
            m_action = Action::None;
            selectionGestureEnd(&input);
            break;
        case Action::Draw:
        case Action::Laser: {
            // The pressure of the release only matters for a dot
            endAction(m_builder.hasPressure() ? filteredPressure(input.pressure) : NO_PRESSURE);
            break;
        }
        default:
            endAction(NO_PRESSURE);
            break;
    }
    restoreButtonState();
}

void PageCanvas::pointerHover(const PointerInput& input) {
    notePointer(input.pos);
    if (m_spline.active && !m_spline.pressed) {
        splineHover(input);
    }
}

void PageCanvas::finishInput() {
    if (m_textEdit.active) {
        finishTextEdit();
    }
    endAction(NO_PRESSURE);
    if (m_spline.active) {
        finishSpline();
    }
}

void PageCanvas::cancelInput() {
    const Action action = m_action;
    m_action = Action::None;
    switch (action) {
        case Action::Draw:
        case Action::Laser:
            m_builder.cancel();
            m_stabilizer.reset();
            break;
        case Action::VerticalSpace:
            finishVerticalSpace(true);
            break;
        case Action::GeometryStroke:
            m_geometry.endStroke();
            break;
        case Action::Erase:
            // What is erased stays erased, and can be undone
            if (!m_group.empty()) {
                pushUndo(std::move(m_group));
            }
            m_group.clear();
            break;
        case Action::SelectionGesture:
            // The selection stays where it was
            m_selection.pending = QTransform();
            m_selection.gestureRect = m_selection.rect;
            m_selection.gestureRotation = m_selection.rotation;
            break;
        default:
            break;
    }
    m_selectPoints.clear();
    clearSpline();
    restoreButtonState();
    updateView();
}

/// Ends what the pointer is doing between press and release, and keeps the result
void PageCanvas::endAction(double pressure) {
    // The action is over before the document changes: changing it ends any input in progress
    const Action action = m_action;
    m_action = Action::None;

    switch (action) {
        case Action::Draw: {
            m_stabilizer->finalizeStroke();
            m_stabilizer.reset();
            const Stroke stroke = m_builder.finish(pressure);
            addStroke(m_curPage, stroke);
            if (m_drawingType == ShapeRecognizer) {
                if (auto shape = ShapeRecognizer::recognize(stroke, m_input.recognizerMinSize)) {
                    if (m_input.snapRecognizedShapes && !shape->points.isEmpty()) {
                        // As in Xournal++ (StrokeHandler::strokeRecognizerDetected): the top left corner of the
                        // shape goes to the grid, then the shape is scaled so that the opposite corner does too
                        const Snapper snap = snapper(m_curPage);
                        Element element = *shape;
                        QRectF bounds = QPolygonF(shape->points).boundingRect();
                        const QPointF topLeft = snap.snapToGrid(bounds.topLeft(), false);
                        Selection::transform(element, QTransform::fromTranslate(topLeft.x() - bounds.left(),
                                                                                topLeft.y() - bounds.top()));
                        bounds.moveTopLeft(topLeft);
                        const QPointF bottomRight = snap.snapToGrid(bounds.bottomRight(), false);
                        const double fx = bounds.width() > 1e-9 ? (bottomRight.x() - topLeft.x()) / bounds.width() : 1;
                        const double fy =
                                bounds.height() > 1e-9 ? (bottomRight.y() - topLeft.y()) / bounds.height() : 1;
                        if (fx > 0 && fy > 0) {
                            Selection::transform(element, QTransform()
                                                                  .translate(topLeft.x(), topLeft.y())
                                                                  .scale(fx, fy)
                                                                  .translate(-topLeft.x(), -topLeft.y()));
                        }
                        shape = std::get<Stroke>(element);
                    }
                    // A step of its own: undoing it brings back the stroke as it was drawn
                    const int layer = activeLayer(m_curPage);
                    const auto& layers = m_doc.pages[static_cast<size_t>(m_curPage)].layers;
                    const size_t index = layers[static_cast<size_t>(layer)].elements.size() - 1;
                    perform({ElementEdit{false, m_curPage, layer, index, stroke},
                             ElementEdit{true, m_curPage, layer, index, *shape}});
                }
            }
            break;
        }
        case Action::Laser:
            m_stabilizer->finalizeStroke();
            m_stabilizer.reset();
            m_laserStrokes.push_back({m_curPage, m_builder.finish(pressure)});
            m_laserFadeDelay.start(m_input.laserFadeOutTime);
            updateView();
            break;
        case Action::Shape:
            // A shape needs at least two points; they can be the same
            if (m_shape.points.size() > 1) {
                m_shape.updateBounds();
                addStroke(m_curPage, m_shape);
            }
            m_shape.points.clear();
            updateView();
            break;
        case Action::Erase:
            if (!m_group.empty()) {
                pushUndo(std::move(m_group));
            }
            m_group.clear();
            updateView();
            break;
        case Action::VerticalSpace:
            finishVerticalSpace(false);
            break;
        case Action::GeometryStroke:
        case Action::GeometryMove:
            m_action = action;
            geometryRelease();
            break;
        case Action::Spline:
            m_spline.pressed = false;
            break;
        case Action::Select:
            selectRelease();
            break;
        case Action::PdfSelect:
            emit pdfSelectionChanged();
            emit pdfSelectionRectChanged();
            updateView();
            break;
        case Action::SelectionGesture:
            selectionGestureEnd(nullptr);
            break;
        case Action::Hand: {
            // A tap on a link opens it
            const int page = pageAt(viewToWorld(m_handPressPos));
            if (page >= 0 && QLineF(m_lastPan, m_handPressPos).length() < TAP_DISTANCE_PX) {
                if (const auto link = elementOfKindAt(page, viewToPage(page, m_handPressPos), 3)) {
                    const auto& layers = m_doc.pages[static_cast<size_t>(page)].layers;
                    const auto& element = layers[static_cast<size_t>(link->first)].elements[link->second];
                    emit openLinkRequested(std::get<LinkElement>(element).url);
                } else {
                    followPdfLink(page, viewToPage(page, m_handPressPos));
                }
            }
            break;
        }
        case Action::None:
            break;
    }
}

// Pen, highlighter, whiteout eraser, laser pointer

/// The pressure of the pen as it is used for the width: with the lower limit and the multiplier of the settings
double PageCanvas::filteredPressure(double pressure) const {
    return std::max(m_input.minimumPressure, pressure * m_input.pressureMultiplier);
}

void PageCanvas::startDrawing(const PointerInput& input, const Stroke& style) {
    // Only the pen changes its width with the pressure
    const bool pressureSensitive = m_usePressure && input.pressure >= 0 && style.tool == Stroke::Tool::Pen;
    m_builder.begin(style, viewToPage(m_curPage, input.pos),
                    pressureSensitive ? filteredPressure(input.pressure) : NO_PRESSURE);
    m_stabilizer = std::make_unique<StrokeStabilizer>(m_input.stabilizer(), m_builder, m_scale, stabilizerEvent(input),
                                                      input.timestamp);
    m_action = Action::Draw;
    updatePageRect(m_curPage, m_builder.takeDirtyRect());
}

/// The stabilizer works in pixels, so that its settings do not depend on the zoom
StrokeStabilizer::Event PageCanvas::stabilizerEvent(const PointerInput& input) const {
    const QPointF pos = viewToPage(m_curPage, input.pos) * m_scale;
    return {pos.x(), pos.y(), m_builder.hasPressure() ? filteredPressure(input.pressure) : NO_PRESSURE};
}

// Audio

void PageCanvas::setAudioRecording(const QString& filename) {
    if (m_audioFile != filename) {
        m_audioFile = filename;
        m_audioClock.restart();
        emit audioRecordingChanged();
    }
}

AudioRef PageCanvas::currentAudio() const {
    if (m_audioFile.isEmpty()) {
        return {};
    }
    return {m_audioFile, m_audioClock.elapsed()};
}

void PageCanvas::playObjectPress(const PointerInput& input) {
    // The front-most stroke or text with a recording at the position, on the visible layers
    const Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    for (auto layer = page.layers.rbegin(); layer != page.layers.rend(); ++layer) {
        if (!layer->visible) {
            continue;
        }
        for (auto element = layer->elements.rbegin(); element != layer->elements.rend(); ++element) {
            const AudioRef* audio = nullptr;
            if (const auto* stroke = std::get_if<Stroke>(&*element)) {
                audio = &stroke->audio;
            } else if (const auto* text = std::get_if<TextElement>(&*element)) {
                audio = &text->audio;
            }
            if (audio && !audio->filename.isEmpty() &&
                Selection::distanceTo(*element, pagePos) <= Selection::ACTION_RADIUS) {
                emit audioPlayRequested(audio->filename, audio->timestamp);
                return;
            }
        }
    }
}

void PageCanvas::addStroke(int pageIndex, const Stroke& newStroke) {
    Stroke stroke = newStroke;
    if (stroke.tool == Stroke::Tool::Pen && stroke.audio.filename.isEmpty() && !m_audioFile.isEmpty()) {
        // The time the stroke was begun at, so that the playback starts with what was said while it was written
        stroke.audio = m_pressAudio.filename == m_audioFile ? m_pressAudio : currentAudio();
    }
    const int layer = activeLayer(pageIndex);
    Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    auto& elements = page.layers[static_cast<size_t>(layer)].elements;
    elements.emplace_back(stroke);
    pushUndo({ElementEdit{true, pageIndex, layer, elements.size() - 1, stroke}});
    m_snapshots[static_cast<size_t>(pageIndex)].reset();

    // Add the stroke to the tiles instead of rendering them again
    const QRectF& pageRect = m_pageRects[pageIndex];
    const QRectF bounds = stroke.bounds.translated(pageRect.topLeft());
    const QRect range = tileRange(bounds);
    const double scale = tileScale();
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (!range.contains(it.key())) {
            continue;
        }
        it->version = ++m_tileVersion;
        QPainter p(&it->image);
        p.setRenderHint(QPainter::Antialiasing, smoothEdges());
        const Renderer::PatternFills fills(patternFills());
        p.translate(-QPointF(it.key()) * TILE_SIZE);
        p.scale(scale, scale);
        p.translate(pageRect.topLeft());
        p.setClipRect(QRectF(QPointF(0, 0), pageRect.size()));
        Renderer::renderStroke(p, stroke);
    }
    // Tiles that are being rendered do not have the stroke yet
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (range.contains(it.key())) {
            it->outdated = true;
        }
    }
    updatePageRect(pageIndex, stroke.bounds);
    emit pageChanged(pageIndex);
}

// Shapes

void PageCanvas::updateShape() {
    static const Shapes::Type TYPES[] = {Shapes::Type::Line,  // not used: freehand
                                         Shapes::Type::Line,        Shapes::Type::Rectangle,
                                         Shapes::Type::Ellipse,     Shapes::Type::Arrow,
                                         Shapes::Type::DoubleArrow, Shapes::Type::CoordinateSystem};
    if (m_drawingType < Line || m_drawingType > CoordinateSystem) {
        return;
    }
    m_shape.points = Shapes::create(TYPES[m_drawingType], m_shapeStart, m_shapeCurrent, snapper(m_curPage),
                                    shapeModifiers(), m_shape.width);
    updateView();
}

// Eraser

void PageCanvas::eraseAt(const QPointF& pagePos) {
    // The thickness of the eraser is half the side of its square
    const double halfSize = thickness(Eraser);
    m_eraserPos = pagePos;
    updateView();

    // Only the layer that is drawn on, as in Xournal++
    Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    for (const int l: {activeLayer(m_curPage)}) {
        auto& elements = page.layers[static_cast<size_t>(l)].elements;
        for (size_t i = elements.size(); i-- > 0;) {
            const auto* stroke = std::get_if<Stroke>(&elements[i]);
            if (!stroke) {
                continue;
            }
            // Most strokes are far from the eraser
            const double reach = halfSize + stroke->width;
            if (!stroke->bounds.adjusted(-reach, -reach, reach, reach).contains(pagePos)) {
                continue;
            }

            std::vector<Stroke> pieces;
            if (m_eraserType == EraseStrokes) {
                if (!::Eraser::intersects(*stroke, pagePos, halfSize)) {
                    continue;
                }
            } else {
                auto remaining = ::Eraser::erase(*stroke, pagePos, halfSize);
                if (!remaining) {
                    continue;
                }
                pieces = std::move(*remaining);
            }

            // The pieces take the place of the stroke
            const QRectF bounds = stroke->bounds;
            m_group.push_back(ElementEdit{false, m_curPage, l, i, std::move(elements[i])});
            elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(i));
            for (size_t k = 0; k < pieces.size(); ++k) {
                elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(i + k), pieces[k]);
                m_group.push_back(ElementEdit{true, m_curPage, l, i + k, pieces[k]});
            }
            markDirty(m_curPage, bounds);
        }
    }
}

// Spline

void PageCanvas::splinePress(const PointerInput& input) {
    const double radius = KNOTS_ATTRACTION_RADIUS_PX / m_scale;
    m_action = Action::Spline;
    m_spline.pressed = true;

    // A double click or double tap ends the spline
    const int interval = QGuiApplication::styleHints()->mouseDoubleClickInterval();
    const bool doublePress = m_spline.active && m_spline.sincePress.isValid() &&
                             m_spline.sincePress.elapsed() < interval &&
                             QLineF(input.pos, m_spline.lastPressPos).length() < DOUBLE_PRESS_DISTANCE_PX;
    m_spline.sincePress.restart();
    m_spline.lastPressPos = input.pos;
    if (doublePress) {
        m_action = Action::None;
        finishSpline();
        return;
    }

    const bool alt = input.modifiers & Qt::AltModifier;
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    if (!m_spline.active) {
        m_spline.active = true;
        m_spline.page = m_curPage;
        m_spline.style = strokeStyle(m_pressTool);
        m_spline.knots.clear();
        m_spline.tangents.clear();
        m_spline.inFirstKnotZone = false;
        m_spline.current = snapper(m_curPage).snapToGrid(pagePos, alt);
        m_spline.knots.append(m_spline.current);
        m_spline.tangents.append(QPointF());
    } else {
        m_spline.current = snapper(m_curPage).snap(pagePos, m_spline.knots.last(), alt);
        if (QLineF(pagePos, m_spline.knots.first()).length() < radius) {
            // On the first knot: the spline is closed when the pointer is released there
            m_spline.knots.append(m_spline.knots.first());
            m_spline.tangents.append(m_spline.tangents.first());
            m_spline.inFirstKnotZone = true;
        } else if (validMotion(m_spline.current, m_spline.knots.last())) {
            m_spline.knots.append(m_spline.current);
            m_spline.tangents.append(QPointF());
        }
    }
    updateView();
}

void PageCanvas::splineMove(const PointerInput& input) {
    if (m_spline.inFirstKnotZone) {
        return;
    }
    // Dragging sets the tangent of the knot
    const QPointF tangent = viewToPage(m_spline.page, input.pos) - m_spline.current;
    if (validMotion(tangent, m_spline.tangents.last())) {
        m_spline.tangents.last() = tangent;
        updateView();
    }
}

void PageCanvas::splineRelease(const PointerInput& input) {
    m_action = Action::None;
    m_spline.pressed = false;
    if (!m_spline.inFirstKnotZone) {
        return;
    }
    const double radius = KNOTS_ATTRACTION_RADIUS_PX / m_scale;
    if (QLineF(viewToPage(m_spline.page, input.pos), m_spline.knots.first()).length() < radius) {
        finishSpline();
    } else {
        // Moved away from the first knot: the spline is not closed
        m_spline.inFirstKnotZone = false;
        if (m_spline.knots.size() > 1) {
            m_spline.knots.removeLast();
            m_spline.tangents.removeLast();
        }
        updateView();
    }
}

void PageCanvas::splineHover(const PointerInput& input) {
    if (pageAt(viewToWorld(input.pos)) != m_spline.page) {
        return;
    }
    const double radius = KNOTS_ATTRACTION_RADIUS_PX / m_scale;
    const QPointF pagePos = viewToPage(m_spline.page, input.pos);
    const bool nowInZone = QLineF(pagePos, m_spline.knots.first()).length() < radius;
    if (nowInZone && m_spline.inFirstKnotZone) {
        return;
    }
    if (!nowInZone) {
        const bool alt = input.modifiers & Qt::AltModifier;
        m_spline.current = snapper(m_spline.page).snap(pagePos, m_spline.knots.last(), alt);
    }
    m_spline.inFirstKnotZone = nowInZone;
    updateView();
}

bool PageCanvas::splineKey(QKeyEvent* event) {
    QPointF& tangent = m_spline.tangents.last();
    switch (event->key()) {
        case Qt::Key_Escape:
            finishSpline();
            break;
        case Qt::Key_Backspace:
            if (m_spline.knots.size() > 1) {
                m_spline.knots.removeLast();
                m_spline.tangents.removeLast();
            }
            break;
        case Qt::Key_Right:
            m_spline.knots.last() += QPointF(SPLINE_SHIFT_AMOUNT, 0);
            break;
        case Qt::Key_Left:
            m_spline.knots.last() += QPointF(-SPLINE_SHIFT_AMOUNT, 0);
            break;
        case Qt::Key_Up:
            m_spline.knots.last() += QPointF(0, -SPLINE_SHIFT_AMOUNT);
            break;
        case Qt::Key_Down:
            m_spline.knots.last() += QPointF(0, SPLINE_SHIFT_AMOUNT);
            break;
        case Qt::Key_R: {
            // "r" like rotate; with Shift the other way round
            const bool shift = event->modifiers() & Qt::ShiftModifier;
            const double angle = shift ? -SPLINE_ROTATE_AMOUNT : SPLINE_ROTATE_AMOUNT;
            tangent = QPointF(std::cos(angle) * tangent.x() + std::sin(angle) * tangent.y(),
                              -std::sin(angle) * tangent.x() + std::cos(angle) * tangent.y());
            break;
        }
        case Qt::Key_S: {
            // "s" like scale; with Shift smaller
            const double length = 2 * std::hypot(tangent.x(), tangent.y());
            if (event->modifiers() & Qt::ShiftModifier) {
                if (length >= SPLINE_MIN_TANGENT_LENGTH) {
                    tangent /= SPLINE_SCALE_AMOUNT;
                }
            } else if (length <= SPLINE_MAX_TANGENT_LENGTH) {
                tangent *= SPLINE_SCALE_AMOUNT;
            }
            break;
        }
        default:
            return false;
    }
    event->accept();
    updateView();
    return true;
}

void PageCanvas::finishSpline() {
    if (!m_spline.active) {
        return;
    }
    const SplineInput spline = m_spline;
    clearSpline();
    if (m_action == Action::Spline) {
        m_action = Action::None;
    }
    // A spline needs two knots
    if (spline.knots.size() >= 2) {
        Stroke stroke = spline.style;
        stroke.points = Shapes::spline(spline.knots, spline.tangents);
        stroke.updateBounds();
        addStroke(spline.page, stroke);
    }
    updateView();
}

void PageCanvas::clearSpline() {
    m_spline.active = false;
    m_spline.pressed = false;
    m_spline.inFirstKnotZone = false;
    m_spline.knots.clear();
    m_spline.tangents.clear();
}

// Vertical space

void PageCanvas::startVerticalSpace(const PointerInput& input) {
    m_action = Action::VerticalSpace;
    m_vertical = VerticalSpaceInput();
    m_vertical.page = m_curPage;
    m_vertical.startY = snapper(m_curPage).snapVertically(viewToPage(m_curPage, input.pos).y());
    m_vertical.endY = m_vertical.startY;
    // With Control what is above moves, else what is below
    const bool above = input.modifiers & Qt::ControlModifier;

    // The elements of the layer that is drawn on leave the page while they are moved
    Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    QRectF bounds;
    for (const int l: {activeLayer(m_curPage)}) {
        auto& elements = page.layers[static_cast<size_t>(l)].elements;
        std::vector<Element> kept;
        kept.reserve(elements.size());
        for (size_t i = 0; i < elements.size(); ++i) {
            const QRectF box = Renderer::elementBounds(elements[i]);
            if (above ? box.bottom() <= m_vertical.startY : box.top() >= m_vertical.startY) {
                bounds |= box;
                m_vertical.items.push_back({l, i, std::move(elements[i])});
            } else {
                kept.push_back(std::move(elements[i]));
            }
        }
        elements = std::move(kept);
    }
    if (m_vertical.items.empty()) {
        updateView();
        return;
    }
    markDirty(m_curPage, {});

    // Render them once, as far as they can come into view
    const QRectF pageRect(0, 0, page.width, page.height);
    const QRectF visible(viewToPage(m_curPage, QPointF(0, 0)), size() / m_scale);
    m_vertical.imageRect =
            bounds.adjusted(-2, -2, 2, 2) & pageRect & visible.adjusted(0, -visible.height(), 0, visible.height());
    if (m_vertical.imageRect.isEmpty()) {
        return;
    }
    const double scale = std::min(tileScale(), MAX_VERTICAL_SPACE_IMAGE_PX / std::max(m_vertical.imageRect.width(),
                                                                                      m_vertical.imageRect.height()));
    m_vertical.image = QImage((m_vertical.imageRect.size() * scale).toSize().expandedTo(QSize(1, 1)),
                              QImage::Format_ARGB32_Premultiplied);
    m_vertical.image.fill(Qt::transparent);
    Page moved;
    moved.layers.emplace_back();
    for (const auto& item: m_vertical.items) {
        moved.layers.back().elements.push_back(item.element);
    }
    QPainter p(&m_vertical.image);
    p.setRenderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing, smoothEdges());
    const Renderer::PatternFills fills(patternFills());
    p.scale(scale, scale);
    p.translate(-m_vertical.imageRect.topLeft());
    Renderer::renderLayers(p, moved, m_vertical.imageRect);
}

void PageCanvas::finishVerticalSpace(bool cancel) {
    VerticalSpaceInput vertical = std::move(m_vertical);
    m_vertical = VerticalSpaceInput();
    if (vertical.page < 0 || vertical.page >= pageCount()) {
        return;
    }

    // Back to where they were in their layers
    Page& page = m_doc.pages[static_cast<size_t>(vertical.page)];
    std::vector<std::pair<int, size_t>> moved;
    for (auto& item: vertical.items) {
        auto& elements = page.layers[static_cast<size_t>(item.layer)].elements;
        elements.insert(elements.begin() + static_cast<std::ptrdiff_t>(std::min(item.index, elements.size())),
                        std::move(item.element));
        moved.emplace_back(item.layer, item.index);
    }

    const double dy = vertical.endY - vertical.startY;
    if (!cancel && dy != 0 && !moved.empty()) {
        perform({ElementMoveEdit{vertical.page, std::move(moved), QPointF(0, dy)}});
    } else if (!moved.empty()) {
        markDirty(vertical.page, {});
    }
    updateView();
}

// Laser pointer

void PageCanvas::fadeLaser() {
    if (m_laserAlpha <= LASER_FADE_ALPHA_STEP) {
        m_laserFade.stop();
        m_laserStrokes.clear();
        m_laserAlpha = 255;
    } else {
        m_laserAlpha -= LASER_FADE_ALPHA_STEP;
    }
    updateView();
}

// Setsquare and compass

void PageCanvas::setGeometryTool(GeometryToolType type) {
    if (m_geometryToolType == type) {
        return;
    }
    finishInput();
    m_geometryToolType = type;
    m_geometryTouch = false;
    m_geometryPage = -1;
    if (type != NoGeometryTool && !m_doc.pages.empty()) {
        // In the middle of what is visible of the current page
        m_geometryPage = std::clamp(m_currentPage, 0, pageCount() - 1);
        const Page& page = m_doc.pages[static_cast<size_t>(m_geometryPage)];
        const QRectF visible = QRectF(viewToPage(m_geometryPage, QPointF(0, 0)), size() / m_scale) &
                               QRectF(0, 0, page.width, page.height);
        const QPointF origin = visible.isEmpty() ? QPointF(page.width / 2, page.height / 2) : visible.center();
        m_geometry =
                GeometryTool(type == Setsquare ? GeometryTool::Type::Setsquare : GeometryTool::Type::Compass, origin);
        forceActiveFocus();
    }
    updateView();
    emit geometryToolChanged();
}

/// The straight lines of the page: the setsquare and compass align with them
std::vector<QLineF> PageCanvas::geometryLines() const {
    std::vector<QLineF> lines;
    if (m_geometryPage < 0 || m_geometryPage >= pageCount()) {
        return lines;
    }
    for (const Layer& layer: m_doc.pages[static_cast<size_t>(m_geometryPage)].layers) {
        for (const Element& element: layer.elements) {
            const auto* stroke = std::get_if<Stroke>(&element);
            if (stroke && stroke->points.size() == 2) {
                lines.emplace_back(stroke->points[0], stroke->points[1]);
            }
        }
    }
    return lines;
}

bool PageCanvas::geometryPress(const PointerInput& input, Tool tool) {
    if (m_geometryToolType == NoGeometryTool || pageAt(viewToWorld(input.pos)) != m_geometryPage) {
        return false;
    }
    const QPointF pagePos = viewToPage(m_geometryPage, input.pos);
    if (tool == Pen || tool == Highlighter) {
        // The pen follows the edges of the tool
        const Stroke style = strokeStyle(tool);
        if (m_geometry.beginStroke(pagePos, style.fill >= 0)) {
            m_action = Action::GeometryStroke;
            m_curPage = m_geometryPage;
            m_shape = style;
            m_shape.points = m_geometry.strokePoints();
            updateView();
            return true;
        }
    } else if (tool == Hand && m_geometry.contains(pagePos)) {
        m_action = Action::GeometryMove;
        m_geometryLast = pagePos;
        m_geometryLines = geometryLines();
        return true;
    }
    return false;
}

void PageCanvas::geometryMove(const PointerInput& input) {
    const QPointF pagePos = viewToPage(m_geometryPage, input.pos);
    if (m_action == Action::GeometryStroke) {
        m_geometry.updateStroke(pagePos);
        m_shape.points = m_geometry.strokePoints();
    } else {
        m_geometry.translateWithSnapping(pagePos - m_geometryLast, m_geometryLines);
        m_geometryLast = pagePos;
    }
    updateView();
}

void PageCanvas::geometryRelease() {
    const Action action = m_action;
    m_action = Action::None;
    if (action == Action::GeometryStroke) {
        m_shape.points = m_geometry.strokePoints();
        m_geometry.endStroke();
        if (m_shape.points.size() >= 2) {
            m_shape.updateBounds();
            addStroke(m_geometryPage, m_shape);
        }
        m_shape.points.clear();
    }
    updateView();
}

bool PageCanvas::geometryKey(QKeyEvent* event) {
    const bool small = event->modifiers() & Qt::AltModifier;
    const bool shift = event->modifiers() & Qt::ShiftModifier;
    QPointF direction;
    switch (event->key()) {
        case Qt::Key_Left:
            direction = QPointF(-1, 0);
            break;
        case Qt::Key_Right:
            direction = QPointF(1, 0);
            break;
        case Qt::Key_Up:
            direction = QPointF(0, -1);
            break;
        case Qt::Key_Down:
            direction = QPointF(0, 1);
            break;
        case Qt::Key_R:
            // "r" like rotate; with Shift the other way round
            m_geometry.rotate((shift ? 1 : -1) * (small ? GEOMETRY_ROTATE_AMOUNT_SMALL : GEOMETRY_ROTATE_AMOUNT));
            break;
        case Qt::Key_S: {
            // "s" like scale; with Shift smaller
            const double factor = small ? GEOMETRY_SCALE_AMOUNT_SMALL : GEOMETRY_SCALE_AMOUNT;
            m_geometry.scale(shift ? 1. / factor : factor);
            break;
        }
        case Qt::Key_M: {
            // Marks the origin of the tool with a small cross
            Stroke cross;
            cross.color = m_toolStates[Pen].color;
            cross.width = THICKNESS[Pen][Fine];
            cross.points = m_geometry.originMark();
            cross.updateBounds();
            addStroke(m_geometryPage, cross);
            break;
        }
        default:
            return false;
    }
    if (!direction.isNull()) {
        // With Shift along the tool instead of along the page
        const double amount = small ? GEOMETRY_MOVE_AMOUNT_SMALL : GEOMETRY_MOVE_AMOUNT;
        if (shift) {
            const double c = std::cos(m_geometry.rotation());
            const double s = std::sin(m_geometry.rotation());
            direction = QPointF(c * direction.x() - s * direction.y(), s * direction.x() + c * direction.y());
        }
        m_geometry.translate(amount * direction);
    }
    event->accept();
    updateView();
    return true;
}

/// One finger on the tool moves it, two fingers turn and resize it
bool PageCanvas::geometryTouch(QTouchEvent* event) {
    if (m_geometryToolType == NoGeometryTool) {
        return false;
    }
    const auto& points = event->points();
    if (event->type() == QEvent::TouchEnd || event->type() == QEvent::TouchCancel || points.isEmpty()) {
        const bool wasActive = m_geometryTouch;
        m_geometryTouch = false;
        return wasActive;
    }

    const QPointF first = viewToPage(m_geometryPage, points[0].position());
    if (points.size() == 1) {
        if (points[0].state() == QEventPoint::Pressed) {
            m_geometryTouch = pageAt(viewToWorld(points[0].position())) == m_geometryPage && m_geometry.contains(first);
            m_geometryLines = geometryLines();
        } else if (m_geometryTouch && points[0].state() == QEventPoint::Updated) {
            m_geometry.translateWithSnapping(first - m_geometryLast, m_geometryLines);
            updateView();
        }
        m_geometryLast = first;
        return m_geometryTouch;
    }
    if (!m_geometryTouch) {
        return false;
    }

    const QPointF second = viewToPage(m_geometryPage, points[1].position());
    const QPointF center = (first + second) / 2;
    const double angle = std::atan2(second.y() - first.y(), second.x() - first.x());
    const double distance = std::max(QLineF(first, second).length(), 0.01);
    const bool started = points[0].state() == QEventPoint::Pressed || points[1].state() == QEventPoint::Pressed ||
                         m_geometryLastDistance <= 0;
    if (!started) {
        m_geometry.translate(center - m_geometryLast);
        // Moving with one finger outside of the tool does not turn it by accident
        if (m_geometry.contains(second)) {
            m_geometry.rotate(angle - m_geometryLastAngle, center);
        }
        m_geometry.scale(distance / m_geometryLastDistance, center);
        updateView();
    }
    m_geometryLast = center;
    m_geometryLastAngle = angle;
    m_geometryLastDistance = distance;
    return true;
}

// Painting of what is in progress, on top of the tiles

void PageCanvas::paintOverlays(QPainter* painter) {
    m_fullUpdate = false;
    m_overlayShown = hasOverlays();
    painter->setRenderHint(QPainter::Antialiasing, smoothEdges());
    const Renderer::PatternFills fills(patternFills());
    if (m_highlightPosition && m_pointerPos.x() >= 0) {
        // As Xournal++ shows it: a translucent disc, with a border if one is set
        painter->setPen(m_positionBorderWidth > 0 ? QPen(m_positionBorderColor, m_positionBorderWidth) :
                                                    QPen(Qt::NoPen));
        painter->setBrush(m_positionColor);
        painter->drawEllipse(m_pointerPos, m_positionRadius, m_positionRadius);
    }
    auto onPage = [&](int page, const std::function<void()>& paint) {
        if (page < 0 || page >= m_pageRects.size()) {
            return;
        }
        painter->save();
        applyPageTransform(*painter, page);
        paint();
        painter->restore();
    };

    // Vertical space: the elements at their new place, and the space that is added or removed
    if (m_action == Action::VerticalSpace) {
        onPage(m_vertical.page, [&] {
            const double dy = m_vertical.endY - m_vertical.startY;
            if (!m_vertical.image.isNull()) {
                painter->setRenderHint(QPainter::SmoothPixmapTransform);
                painter->drawImage(m_vertical.imageRect.translated(0, dy), m_vertical.image);
            }
            const QRectF band(0, std::min(m_vertical.startY, m_vertical.endY), m_pageRects[m_vertical.page].width(),
                              std::abs(dy));
            QColor fill = AID_COLOR;
            fill.setAlphaF(VERTICAL_SPACE_OPACITY);
            painter->setPen(QPen(AID_COLOR, 0));
            painter->setBrush(fill);
            painter->drawRect(band);
        });
    }

    // Setsquare or compass, with the stroke that is drawn along it
    if (m_geometryToolType != NoGeometryTool) {
        onPage(m_geometryPage, [&] {
            // The many marks of the tool are rendered once; they only change when it is moved
            const double scale = tileScale();
            const QRectF rect = m_geometry.bounds(false);
            const GeometryCacheKey key{static_cast<int>(m_geometry.type()), m_geometry.height(), m_geometry.rotation(),
                                       m_geometry.origin(), scale};
            const QSize pixelSize = (rect.size() * scale).toSize();
            if (pixelSize.width() > MAX_GEOMETRY_IMAGE_PX || pixelSize.height() > MAX_GEOMETRY_IMAGE_PX) {
                m_geometry.paint(*painter);
            } else {
                if (!(key == m_geometryCacheKey) || m_geometryCache.size() != pixelSize) {
                    m_geometryCacheKey = key;
                    m_geometryCache = QImage(pixelSize, QImage::Format_ARGB32_Premultiplied);
                    m_geometryCache.fill(Qt::transparent);
                    QPainter p(&m_geometryCache);
                    p.scale(scale, scale);
                    p.translate(-rect.topLeft());
                    m_geometry.paint(p);
                }
                painter->drawImage(rect, m_geometryCache);
            }
            if (m_action == Action::GeometryStroke) {
                Renderer::renderStroke(*painter, m_shape);
            }
        });
    }

    // Laser pointer
    if (!m_laserStrokes.empty() || m_action == Action::Laser) {
        painter->save();
        painter->setOpacity(m_laserAlpha / 255.0);
        for (const LaserStroke& laser: m_laserStrokes) {
            onPage(laser.page, [&] { Renderer::renderStroke(*painter, laser.stroke); });
        }
        if (m_action == Action::Laser) {
            onPage(m_curPage, [&] { Renderer::renderStroke(*painter, m_builder.stroke()); });
        }
        painter->restore();
    }

    if (m_action == Action::Draw) {
        onPage(m_curPage, [&] { Renderer::renderStroke(*painter, m_builder.stroke()); });
    } else if (m_action == Action::Shape) {
        onPage(m_curPage, [&] { Renderer::renderStroke(*painter, m_shape); });
    }
    // The square of the eraser: while it erases, while it hovers, both or never
    const auto visibility = m_input.eraserVisibility;
    const bool erasing = m_action == Action::Erase;
    const bool hovering = m_action == Action::None && m_tool == Eraser && m_pointerPos.x() >= 0;
    if ((erasing && (visibility == InputSettings::EraserAlways || visibility == InputSettings::EraserTouch)) ||
        (hovering && (visibility == InputSettings::EraserAlways || visibility == InputSettings::EraserHover))) {
        const int page = erasing ? m_curPage : pageAt(viewToWorld(m_pointerPos));
        const QPointF center = erasing ? m_eraserPos : (page >= 0 ? viewToPage(page, m_pointerPos) : QPointF());
        onPage(page, [&] {
            const double halfSize = thickness(Eraser);
            painter->setPen(QPen(Qt::black, 0));
            painter->setBrush(QColor(255, 255, 255, 128));
            painter->drawRect(QRectF(center - QPointF(halfSize, halfSize), QSizeF(2 * halfSize, 2 * halfSize)));
        });
    }

    paintPdfOverlays(painter);
    paintSelection(painter);

    if (m_spline.active && !m_spline.knots.isEmpty()) {
        onPage(m_spline.page, [&] {
            const auto& knots = m_spline.knots;
            const auto& tangents = m_spline.tangents;
            const double radius = KNOTS_ATTRACTION_RADIUS_PX / m_scale;
            const double lineWidth = SPLINE_AID_WIDTH_PX / m_scale;
            painter->setBrush(Qt::NoBrush);

            // Circles around the knots; the first one is filled when releasing closes the spline
            painter->setPen(QPen(SPLINE_KNOT_COLOR, lineWidth));
            for (qsizetype i = 1; i < knots.size(); ++i) {
                painter->drawEllipse(knots[i], radius, radius);
            }
            painter->setPen(QPen(SPLINE_FIRST_KNOT_COLOR, lineWidth));
            if (m_spline.inFirstKnotZone) {
                painter->setBrush(SPLINE_FIRST_KNOT_COLOR);
            }
            painter->drawEllipse(knots.first(), radius, radius);
            painter->setBrush(Qt::NoBrush);

            // The segment that the next knot would add
            const QPointF target = m_spline.inFirstKnotZone ? knots.first() : m_spline.current;
            const QPointF targetControl = m_spline.inFirstKnotZone ? knots.first() - tangents.first() : target;
            QPainterPath next;
            next.moveTo(knots.last());
            next.cubicTo(knots.last() + tangents.last(), targetControl, target);
            painter->setPen(QPen(SPLINE_KNOT_COLOR, lineWidth));
            painter->drawPath(next);

            // Tangents
            painter->setPen(QPen(SPLINE_TANGENT_COLOR, lineWidth));
            for (qsizetype i = 0; i < knots.size(); ++i) {
                painter->drawLine(knots[i] - tangents[i], knots[i] + tangents[i]);
            }

            // The spline so far, as it will be
            if (knots.size() > 1) {
                Stroke stroke = m_spline.style;
                stroke.points = Shapes::spline(knots, tangents);
                Renderer::renderStroke(*painter, stroke);
            }
        });
    }
}
