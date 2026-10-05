/*
 * Qournal
 *
 * Tests for the settings: the tools of the buttons, zoom steps, touch behaviour, the page of new documents, what
 * is kept between sessions, and the theme. They run without a display (QT_QPA_PLATFORM=offscreen,
 * QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "CanvasFixture.h"
#include "InputSettings.h"
#include "Localization.h"
#include "Theme.h"

namespace {

/// A pen stroke with a side button of the stylus held down
void strokeWithButton(Fixture& f, Qt::MouseButton button, const QPointF& from, const QPointF& to) {
    static const auto* pen = new QPointingDevice(
            QStringLiteral("button pen"), 44, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
            QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
    auto send = [&](QEvent::Type type, const QPointF& pos, Qt::MouseButton changed, Qt::MouseButtons buttons) {
        QTabletEvent event(type, pen, pos, f.window.mapToGlobal(pos), 0.5, 0, 0, 0, 0, 0, Qt::NoModifier, changed,
                           buttons);
        QCoreApplication::sendEvent(&f.window, &event);
    };
    send(QEvent::TabletPress, from, button, button | Qt::LeftButton);
    for (int i = 1; i <= 10; ++i) {
        send(QEvent::TabletMove, from + (to - from) * i / 10.0, Qt::NoButton, button | Qt::LeftButton);
    }
    send(QEvent::TabletRelease, to, button, Qt::NoButton);
}

void mouseDrag(Fixture& f, Qt::MouseButton button, const QPointF& from, const QPointF& to) {
    QTest::mousePress(&f.window, button, Qt::NoModifier, from.toPoint());
    for (int i = 1; i <= 10; ++i) {
        QTest::mouseMove(&f.window, (from + (to - from) * i / 10.0).toPoint());
    }
    QTest::mouseRelease(&f.window, button, Qt::NoModifier, to.toPoint());
}

}  // namespace

class TestSettings: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Nothing of the user is read or written
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("qournal-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_settings"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void init() { QSettings().clear(); }

    void stylusButtons() {
        Fixture f;
        PageCanvas* c = f.canvas;
        // As long as nothing else is set, the side buttons erase like the eraser tip
        QCOMPARE(c->buttonAction(PageCanvas::ButtonEraserTip), int(PageCanvas::Eraser));
        QCOMPARE(c->buttonAction(PageCanvas::ButtonStylus1), int(PageCanvas::Eraser));
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(f.strokes().size(), 1);
        strokeWithButton(f, Qt::RightButton, f.onPage(0, QPointF(200, 60)), f.onPage(0, QPointF(200, 140)));
        QCOMPARE(f.strokes().size(), 2);  // cut in two

        // A button with another tool: it is used for the stroke, the selected tool stays
        QSignalSpy changed(c, &PageCanvas::buttonActionsChanged);
        c->setButtonAction(PageCanvas::ButtonStylus1, PageCanvas::Highlighter);
        QCOMPARE(changed.count(), 1);
        strokeWithButton(f, Qt::RightButton, f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(300, 300)));
        QCOMPARE(f.strokes().size(), 3);
        QCOMPARE(f.strokes().last().tool, Stroke::Tool::Highlighter);
        QCOMPARE(c->tool(), PageCanvas::Pen);
        // The second button has its own setting
        strokeWithButton(f, Qt::MiddleButton, f.onPage(0, QPointF(200, 260)), f.onPage(0, QPointF(200, 340)));
        QCOMPARE(f.strokes().size(), 4);  // the highlighter stroke is cut in two

        // No tool of its own: the selected tool draws
        c->setButtonAction(PageCanvas::ButtonEraserTip, PageCanvas::ButtonNoAction);
        f.stroke(f.onPage(0, QPointF(100, 380)), f.onPage(0, QPointF(300, 380)), QPointingDevice::PointerType::Eraser);
        QCOMPARE(f.strokes().size(), 5);
        QCOMPARE(f.strokes().last().tool, Stroke::Tool::Pen);

        // The floating toolbox is asked for where the pen is, and nothing is drawn
        c->setButtonAction(PageCanvas::ButtonStylus2, PageCanvas::ButtonFloatingToolbox);
        QSignalSpy toolbox(c, &PageCanvas::floatingToolboxRequested);
        strokeWithButton(f, Qt::MiddleButton, QPointF(300, 560), QPointF(350, 560));
        QCOMPARE(toolbox.count(), 1);
        QCOMPARE(toolbox[0][0].toPointF(), QPointF(300, 560));
        QCOMPARE(f.strokes().size(), 5);
        // The pen works as before afterwards
        f.strokeOnPage(0, {QPointF(100, 420), QPointF(300, 420)});
        QCOMPARE(f.strokes().size(), 6);

        // Values that are no action are refused
        c->setButtonAction(PageCanvas::ButtonStylus1, 57);
        QCOMPARE(c->buttonAction(PageCanvas::ButtonStylus1), int(PageCanvas::Highlighter));
    }

    void mouseButtons() {
        Fixture f;
        PageCanvas* c = f.canvas;
        mouseDrag(f, Qt::LeftButton, f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));
        QCOMPARE(f.strokes().size(), 1);

        // The other buttons move the view
        const QPointF before = c->pageViewRect(0).topLeft();
        mouseDrag(f, Qt::RightButton, QPointF(400, 300), QPointF(400, 250));
        QCOMPARE(c->pageViewRect(0).topLeft(), before + QPointF(0, -50));
        mouseDrag(f, Qt::MiddleButton, QPointF(400, 250), QPointF(400, 300));
        QCOMPARE(c->pageViewRect(0).topLeft(), before);
        QCOMPARE(f.strokes().size(), 1);

        // ... or use a tool
        c->setButtonAction(PageCanvas::ButtonMouseRight, PageCanvas::Eraser);
        mouseDrag(f, Qt::RightButton, f.onPage(0, QPointF(200, 60)), f.onPage(0, QPointF(200, 140)));
        QCOMPARE(f.strokes().size(), 2);
        QCOMPARE(c->pageViewRect(0).topLeft(), before);

        c->setButtonAction(PageCanvas::ButtonMouseMiddle, PageCanvas::ButtonNoAction);
        mouseDrag(f, Qt::MiddleButton, f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(300, 300)));
        QCOMPARE(f.strokes().size(), 3);

        c->setButtonAction(PageCanvas::ButtonMouseRight, PageCanvas::ButtonFloatingToolbox);
        QSignalSpy toolbox(c, &PageCanvas::floatingToolboxRequested);
        mouseDrag(f, Qt::RightButton, QPointF(300, 400), QPointF(350, 400));
        QCOMPARE(toolbox.count(), 1);
        QCOMPARE(f.strokes().size(), 3);
    }

    void buttonsWithTheirOwnSettings() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("snapGrid", false);
        const QColor red(0xd0, 0x10, 0x10);
        // The right mouse button draws red rectangles with a fine pen
        c->setButtonAction(PageCanvas::ButtonMouseRight, PageCanvas::Pen);
        c->setButtonOptions(PageCanvas::ButtonMouseRight, {{QStringLiteral("drawingType"), PageCanvas::Rectangle},
                                                           {QStringLiteral("size"), PageCanvas::Fine},
                                                           {QStringLiteral("color"), red}});
        QCOMPARE(c->buttonOptions(PageCanvas::ButtonMouseRight).value(QStringLiteral("size")).toInt(),
                 int(PageCanvas::Fine));
        mouseDrag(f, Qt::RightButton, f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(200, 160)));
        Stroke stroke = f.strokes().last();
        QCOMPARE(stroke.points.size(), 5);
        QCOMPARE(stroke.color, red);
        QCOMPARE(stroke.width, 0.85);

        // The pen itself is as it was
        QCOMPARE(c->color(), PEN_COLOR);
        QCOMPARE(c->toolSize(), PageCanvas::VeryThick);
        QCOMPARE(c->drawingType(), PageCanvas::Freehand);
        mouseDrag(f, Qt::LeftButton, f.onPage(0, QPointF(100, 250)), f.onPage(0, QPointF(200, 250)));
        stroke = f.strokes().last();
        QVERIFY(stroke.points.size() > 5);
        QCOMPARE(stroke.color, PEN_COLOR);

        // Only what is set differs: the colour of the tool again
        c->setButtonOptions(PageCanvas::ButtonMouseRight, {{QStringLiteral("color"), QColor(0, 0, 0, 0)}});
        mouseDrag(f, Qt::RightButton, f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(200, 360)));
        QCOMPARE(f.strokes().last().color, PEN_COLOR);
        QCOMPARE(f.strokes().last().width, 0.85);

        // The fourth and fifth button
        QCOMPARE(c->buttonAction(PageCanvas::ButtonMouse4), int(PageCanvas::ButtonNoAction));
        c->setButtonAction(PageCanvas::ButtonMouse4, PageCanvas::Highlighter);
        mouseDrag(f, Qt::BackButton, f.onPage(0, QPointF(100, 400)), f.onPage(0, QPointF(200, 400)));
        QCOMPARE(f.strokes().last().tool, Stroke::Tool::Highlighter);
        const int count = f.strokes().size();
        mouseDrag(f, Qt::ForwardButton, f.onPage(0, QPointF(100, 420)), f.onPage(0, QPointF(200, 420)));
        QCOMPARE(f.strokes().size(), count + 1);  // no tool of its own: the selected one
        QCOMPARE(f.strokes().last().tool, Stroke::Tool::Pen);

        // Kept between sessions
        c->saveSettings();
        Fixture other;
        other.canvas->loadSettings();
        const QVariantMap options = other.canvas->buttonOptions(PageCanvas::ButtonMouseRight);
        QCOMPARE(options.value(QStringLiteral("drawingType")).toInt(), int(PageCanvas::Rectangle));
        QCOMPARE(options.value(QStringLiteral("size")).toInt(), int(PageCanvas::Fine));
        QVERIFY(!options.value(QStringLiteral("color")).value<QColor>().isValid());
        QCOMPARE(other.canvas->buttonAction(PageCanvas::ButtonMouse4), int(PageCanvas::Highlighter));
    }

    /// A device can be used as another kind of device, as in the settings of Xournal++
    void deviceClasses() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(f.strokes().size(), 1);

        // The pen of the test is known now, as a pen
        auto entry = [&](const QString& name) {
            for (const QVariant& device: c->inputDevices()) {
                if (device.toMap().value(QStringLiteral("name")).toString() == name) {
                    return device.toMap();
                }
            }
            return QVariantMap();
        };
        QVariantMap pen = entry(QStringLiteral("test pen"));
        QCOMPARE(pen.value(QStringLiteral("automaticClass")).toInt(), int(PageCanvas::DevicePen));
        QCOMPARE(pen.value(QStringLiteral("deviceClass")).toInt(), int(PageCanvas::DeviceAutomatic));

        // Used as an eraser, its tip erases: the stroke is cut in two
        c->setDeviceClass(QStringLiteral("test pen"), PageCanvas::DeviceEraser);
        QCOMPARE(entry(QStringLiteral("test pen")).value(QStringLiteral("deviceClass")).toInt(),
                 int(PageCanvas::DeviceEraser));
        f.strokeOnPage(0, {QPointF(200, 50), QPointF(200, 150)});
        QCOMPARE(f.strokes().size(), 2);
        for (const Stroke& stroke: f.strokes()) {
            QVERIFY(stroke.tool == Stroke::Tool::Pen);
            QVERIFY(stroke.bounds.right() < 200 || stroke.bounds.left() > 200);
        }

        // Disabled, it does nothing
        c->setDeviceClass(QStringLiteral("test pen"), PageCanvas::DeviceDisabled);
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        QCOMPARE(f.strokes().size(), 2);

        // As a touchscreen, it moves the view
        c->setDeviceClass(QStringLiteral("test pen"), PageCanvas::DeviceTouchscreen);
        const QPointF before = c->pageViewRect(0).topLeft();
        f.stroke(QPointF(400, 300), QPointF(400, 250));
        QCOMPARE(f.strokes().size(), 2);
        QCOMPARE(c->pageViewRect(0).topLeft(), before + QPointF(0, -50));

        c->setDeviceClass(QStringLiteral("test pen"), PageCanvas::DeviceAutomatic);
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        QCOMPARE(f.strokes().size(), 3);

        // A touchscreen used as a pen draws with one finger
        QPointingDevice* screen = QTest::createTouchDevice();
        QVERIFY(!screen->name().isEmpty());
        c->setDeviceClass(screen->name(), PageCanvas::DevicePen);
        const QPoint from = f.onPage(0, QPointF(100, 300)).toPoint();
        QTest::touchEvent(&f.window, screen).press(0, from);
        for (int i = 1; i <= 5; ++i) {
            QTest::touchEvent(&f.window, screen).move(0, from + QPoint(20 * i, 0));
        }
        QTest::touchEvent(&f.window, screen).release(0, from + QPoint(100, 0));
        QCOMPARE(f.strokes().size(), 4);

        // Kept between sessions
        c->saveSettings();
        Fixture other;
        other.canvas->loadSettings();
        QCOMPARE(other.canvas->input()->property("deviceClasses").toMap().value(screen->name()).toInt(),
                 int(PageCanvas::DevicePen));
        c->setDeviceClass(screen->name(), PageCanvas::DeviceAutomatic);
    }

    /// The first events of a stroke can be left out; the pointer of the stylus is as set
    void stylusSettings() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("ignoredStylusEvents", 3);
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(120, 100), QPointF(140, 100), QPointF(160, 100),
                           QPointF(180, 100), QPointF(200, 100)});
        QCOMPARE(f.strokes().size(), 1);
        // The press and two moves were left out: the stroke begins at the third move
        QCOMPARE(f.strokes()[0].points.first().x(), 160.0);
        c->input()->setProperty("ignoredStylusEvents", 0);

        c->input()->setProperty("stylusCursor", InputSettings::StylusCursorNone);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(300, 300)), 0);
        QCOMPARE(c->cursor().shape(), Qt::BlankCursor);
        c->input()->setProperty("stylusCursor", InputSettings::StylusCursorDot);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(310, 300)), 0);
        QCOMPARE(c->cursor().shape(), Qt::BitmapCursor);
        // The dot is as wide as the stroke will be on the screen, in the colour of the pen and without a border
        const auto dot = [&] {
            const QImage image = c->cursor().pixmap().toImage();
            int count = 0;
            int width = 0;
            for (int y = 0; y < image.height(); ++y) {
                int row = 0;
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.alpha() > 128) {
                        ++row;
                        // The edge is smoothed, which changes the colour a little
                        const QColor pen = c->color();
                        if (std::abs(pixel.red() - pen.red()) > 16 || std::abs(pixel.green() - pen.green()) > 16 ||
                            std::abs(pixel.blue() - pen.blue()) > 16) {
                            return QSize(-1, -1);
                        }
                    }
                }
                count += row;
                width = std::max(width, row);
            }
            return QSize(width, count);
        };
        c->setTool(PageCanvas::Pen);
        c->setToolSize(PageCanvas::Fine);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(320, 300)), 0);
        const QSize fine = dot();
        QVERIFY2(fine.width() >= 1 && fine.width() <= 3, qPrintable(QString::number(fine.width())));
        c->setToolSize(PageCanvas::VeryThick);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(330, 300)), 0);
        const QSize thick = dot();
        const double expected = c->thickness() * c->zoom() * c->property("displayDpi").toDouble() / 72.0;
        QVERIFY2(thick.width() > fine.width() + 2 && std::abs(thick.width() - expected) < 3,
                 qPrintable(QStringLiteral("%1, expected %2").arg(thick.width()).arg(expected)));
        c->setToolSize(PageCanvas::Medium);
    }

    /// Two fingers zoom only once they moved apart enough
    void touchZoomThreshold() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QPointingDevice* screen = QTest::createTouchDevice();
        c->input()->setProperty("touchZoomThreshold", 30.0);
        const double zoom = c->zoom();
        // 10 % apart: no zoom
        QTest::touchEvent(&f.window, screen).press(0, QPoint(350, 300)).press(1, QPoint(450, 300));
        QTest::touchEvent(&f.window, screen).move(0, QPoint(345, 300)).move(1, QPoint(455, 300));
        QTest::touchEvent(&f.window, screen).release(0, QPoint(345, 300)).release(1, QPoint(455, 300));
        QCOMPARE(c->zoom(), zoom);
        // 60 % apart: zoom
        QTest::touchEvent(&f.window, screen).press(0, QPoint(350, 300)).press(1, QPoint(450, 300));
        for (int i = 1; i <= 6; ++i) {
            QTest::touchEvent(&f.window, screen).move(0, QPoint(350 - 5 * i, 300)).move(1, QPoint(450 + 5 * i, 300));
        }
        QTest::touchEvent(&f.window, screen).release(0, QPoint(320, 300)).release(1, QPoint(480, 300));
        QVERIFY(c->zoom() > zoom * 1.1);
    }

    void zoomSteps() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->zoomTo(1.0);
        c->zoomIn();
        QCOMPARE(c->zoom(), 1.25);
        c->zoomOut();
        QCOMPARE(c->zoom(), 1.0);

        c->input()->setProperty("zoomStep", 10.0);
        c->zoomIn();
        QVERIFY(std::abs(c->zoom() - 1.1) < 1e-9);
        c->zoomTo(1.0);

        // A notch of the wheel with Control
        f.wheel(QPointF(400, 300), 120, Qt::ControlModifier);
        QVERIFY(std::abs(c->zoom() - 1.2) < 1e-9);
        c->zoomTo(1.0);
        c->input()->setProperty("wheelZoomStep", 50.0);
        f.wheel(QPointF(400, 300), -120, Qt::ControlModifier);
        QVERIFY(std::abs(c->zoom() - 1 / 1.5) < 1e-9);
    }

    void touch() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QPointingDevice* screen = QTest::createTouchDevice();
        auto pinch = [&] {
            QTest::touchEvent(&f.window, screen).press(0, QPoint(350, 300)).press(1, QPoint(450, 300));
            for (int i = 1; i <= 5; ++i) {
                QTest::touchEvent(&f.window, screen)
                        .move(0, QPoint(350 - 10 * i, 300))
                        .move(1, QPoint(450 + 10 * i, 300));
            }
            QTest::touchEvent(&f.window, screen).release(0, QPoint(300, 300)).release(1, QPoint(500, 300));
        };
        auto drag = [&](const QPoint& from, const QPoint& to) {
            QTest::touchEvent(&f.window, screen).press(0, from);
            for (int i = 1; i <= 5; ++i) {
                QTest::touchEvent(&f.window, screen).move(0, from + (to - from) * i / 5);
            }
            QTest::touchEvent(&f.window, screen).release(0, to);
        };

        // Two fingers zoom, unless that is switched off
        const double zoom = c->zoom();
        pinch();
        QVERIFY2(c->zoom() > zoom * 1.5, qPrintable(QString::number(c->zoom() / zoom)));
        c->zoomTo(zoom);
        c->input()->setProperty("zoomGestures", false);
        pinch();
        QCOMPARE(c->zoom(), zoom);

        // One finger moves the view, or draws
        const QPointF before = c->pageViewRect(0).topLeft();
        drag(QPoint(400, 300), QPoint(400, 250));
        // All of the way, also if events are merged
        QCOMPARE(c->pageViewRect(0).topLeft(), before + QPointF(0, -50));
        QCOMPARE(f.strokes().size(), 0);
        c->setFingerDraws(true);
        drag(QPoint(300, 300), QPoint(400, 300));
        QCOMPARE(f.strokes().size(), 1);

        // After the pen was used, touches are ignored for a while: the palm rests on the screen
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(f.strokes().size(), 2);
        drag(QPoint(300, 350), QPoint(400, 350));
        QCOMPARE(f.strokes().size(), 2);
        c->input()->setProperty("palmRejectionTime", 100);
        QTest::qWait(150);
        drag(QPoint(300, 350), QPoint(400, 350));
        QCOMPARE(f.strokes().size(), 3);
        // ... unless that is switched off
        c->input()->setProperty("palmRejectionTime", 5000);
        f.strokeOnPage(0, {QPointF(100, 150), QPointF(300, 150)});
        c->input()->setProperty("palmRejection", false);
        drag(QPoint(300, 400), QPoint(400, 400));
        QCOMPARE(f.strokes().size(), 5);
    }

    void directionOfAShape() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("snapGrid", false);
        c->setDrawingType(PageCanvas::Rectangle);
        auto drawn = [&](const QPointF& from, const QPointF& to) {
            f.strokeOnPage(0, {from, (from + to) / 2, to});
            return QPolygonF(f.strokes().last().points).boundingRect();
        };
        // Without the setting the direction does not matter
        QRectF rect = drawn(QPointF(300, 200), QPointF(200, 260));
        QCOMPARE(rect.size(), QSizeF(100, 60));

        c->input()->setProperty("drawDirModsEnabled", true);
        // To the right and down: as it is drawn
        rect = drawn(QPointF(100, 300), QPointF(200, 360));
        QCOMPARE(rect.size(), QSizeF(100, 60));
        // To the left: like Shift, a square
        rect = drawn(QPointF(300, 300), QPointF(200, 360));
        QVERIFY2(std::abs(rect.width() - rect.height()) < 1e-6,
                 qPrintable(QStringLiteral("%1 x %2").arg(rect.width()).arg(rect.height())));
        // Upwards: like Control, from the centre
        rect = drawn(QPointF(150, 250), QPointF(250, 190));
        QVERIFY2(std::abs(rect.center().x() - 150) < 1e-6 && std::abs(rect.center().y() - 250) < 1e-6,
                 qPrintable(QStringLiteral("%1, %2").arg(rect.center().x()).arg(rect.center().y())));
        QCOMPARE(rect.size(), QSizeF(200, 120));
        // The direction of the start counts once the pointer is far enough: back to the right, still a square
        f.strokeOnPage(0, {QPointF(300, 100), QPointF(200, 130), QPointF(380, 160)});
        rect = QPolygonF(f.strokes().last().points).boundingRect();
        QVERIFY(std::abs(rect.width() - rect.height()) < 1e-6);
    }

    void tapsAreNotStrokes() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.tap(f.onPage(0, QPointF(100, 100)));
        QCOMPARE(f.strokes().size(), 1);  // a dot

        c->input()->setProperty("strokeFilterEnabled", true);
        c->input()->setProperty("strokeFilterSuccessive", 300);
        QTest::qWait(350);
        f.tap(f.onPage(0, QPointF(200, 100)));
        QCOMPARE(f.strokes().size(), 1);
        QVERIFY(!c->canRedo());

        // A real stroke is drawn, and the dot right after it too (the dot of an i)
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        f.tap(f.onPage(0, QPointF(200, 180)));
        QCOMPARE(f.strokes().size(), 3);

        // A tap on something selects it
        QTest::qWait(350);
        QVERIFY(!c->hasSelection());
        f.tap(f.onPage(0, QPointF(200, 200)));
        QCOMPARE(f.strokes().size(), 3);
        QVERIFY(c->hasSelection());
        c->clearSelection();
        c->input()->setProperty("strokeFilterSelects", false);
        QTest::qWait(350);
        f.tap(f.onPage(0, QPointF(200, 200)));
        QVERIFY(!c->hasSelection());
        QCOMPARE(f.strokes().size(), 3);
    }

    void guessedPressure() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setUsePressure(true);
        // The mouse has no pressure
        mouseDrag(f, Qt::LeftButton, f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));
        QVERIFY(!f.strokes().last().hasPressure());

        c->input()->setProperty("pressureGuessing", true);
        mouseDrag(f, Qt::LeftButton, f.onPage(0, QPointF(100, 200)), f.onPage(0, QPointF(300, 200)));
        const Stroke stroke = f.strokes().last();
        QVERIFY(stroke.hasPressure());
        for (double width: stroke.widths) {
            QVERIFY2(width > 0 && width < 3 * c->thickness(), qPrintable(QString::number(width)));
        }
        // A pen with pressure keeps its own
        f.strokeOnPage(0, {QPointF(100, 300), QPointF(300, 300)}, 0.5);
        QVERIFY(std::abs(f.strokes().last().widths.first() - 0.5 * c->thickness()) < 1e-6);
    }

    void recognizedShapesOnTheGrid() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setDrawingType(PageCanvas::ShapeRecognizer);
        const double grid = c->input()->snapGridSize;
        auto onGrid = [&](double value) { return std::abs(value / grid - std::round(value / grid)) < 1e-6; };
        // A rectangle drawn by hand: many points, not quite straight
        const QList<QPointF> corners = {QPointF(103, 102), QPointF(301, 101), QPointF(302, 262), QPointF(101, 261),
                                        QPointF(103, 102)};
        QList<QPointF> rectangle;
        for (qsizetype i = 0; i + 1 < corners.size(); ++i) {
            for (int k = 0; k < 40; ++k) {
                const QPointF point = corners[i] + (corners[i + 1] - corners[i]) * k / 40.0;
                rectangle.append(point + QPointF(std::sin(k * 0.9), std::cos(k * 1.3)) * 0.7);
            }
        }
        rectangle.append(corners.last());
        f.strokeOnPage(0, rectangle);
        QRectF bounds = QPolygonF(f.strokes().last().points).boundingRect();
        QVERIFY(f.strokes().last().points.size() <= 5);  // recognised
        QVERIFY(!onGrid(bounds.left()) || !onGrid(bounds.bottom()));

        c->undo();
        c->undo();
        c->input()->setProperty("snapRecognizedShapes", true);
        f.strokeOnPage(0, rectangle);
        bounds = QPolygonF(f.strokes().last().points).boundingRect();
        QVERIFY2(onGrid(bounds.left()) && onGrid(bounds.top()) && onGrid(bounds.right()) && onGrid(bounds.bottom()),
                 qPrintable(QStringLiteral("%1 %2 %3 %4")
                                    .arg(bounds.left())
                                    .arg(bounds.top())
                                    .arg(bounds.right())
                                    .arg(bounds.bottom())));
        QVERIFY(std::abs(bounds.width() - 200) < grid && std::abs(bounds.height() - 160) < grid);
    }

    void squareOfTheEraser() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        const QPointF onStroke = f.onPage(0, QPointF(200, 200));
        const QPointF away = f.onPage(0, QPointF(200, 120));
        c->setTool(PageCanvas::Eraser);
        c->setToolSize(PageCanvas::VeryThick);
        auto lightened = [&] { return f.pixel(onStroke).blue() > PEN_COLOR.blue() + 30; };

        // Shown while it hovers: the stroke under it looks lighter
        f.tablet(QEvent::TabletMove, onStroke, 0);
        QVERIFY(lightened());
        f.tablet(QEvent::TabletMove, away, 0);
        QVERIFY(!lightened());

        c->input()->setProperty("eraserVisibility", InputSettings::EraserNever);
        f.tablet(QEvent::TabletMove, onStroke, 0);
        QVERIFY(!lightened());
        c->input()->setProperty("eraserVisibility", InputSettings::EraserTouch);
        f.tablet(QEvent::TabletMove, away, 0);
        f.tablet(QEvent::TabletMove, onStroke, 0);
        QVERIFY(!lightened());
        c->input()->setProperty("eraserVisibility", InputSettings::EraserHover);
        f.tablet(QEvent::TabletMove, away, 0);
        f.tablet(QEvent::TabletMove, onStroke, 0);
        QVERIFY(lightened());
        QCOMPARE(f.strokes().size(), 1);  // hovering erases nothing
    }

    void pageTemplateAndCanvasColor() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QCOMPARE(c->pageSize(0), QSizeF(595.27559, 841.88976));
        COMPARE_COLOR(f.pixel(QPointF(3, 3)), CANVAS_COLOR);

        QSignalSpy changed(c, &PageCanvas::pageTemplateChanged);
        c->setPageTemplate({{QStringLiteral("width"), 400.0},
                            {QStringLiteral("height"), 300.0},
                            {QStringLiteral("color"), QColor(0xff, 0xee, 0xcc)},
                            {QStringLiteral("style"), QStringLiteral("graph")},
                            {QStringLiteral("type"), QStringLiteral("solid")}});  // not part of a template
        QCOMPARE(changed.count(), 1);
        QVERIFY(!c->pageTemplate().contains(QStringLiteral("type")));
        // The document that is open stays as it is
        QCOMPARE(c->pageSize(0), QSizeF(595.27559, 841.88976));
        c->newDocument();
        QCOMPARE(c->pageSize(0), QSizeF(400, 300));
        const Background& background = c->document().pages[0].background;
        QCOMPARE(background.color, QColor(0xff, 0xee, 0xcc));
        QCOMPARE(background.style, QStringLiteral("graph"));
        QVERIFY(!c->modified());

        // The properties of a page serve as template
        c->setPageProperties(0, {{QStringLiteral("style"), QStringLiteral("lined")}});
        c->setPageTemplate(c->pageProperties(0));
        c->newDocument();
        QCOMPARE(c->document().pages[0].background.style, QStringLiteral("lined"));

        c->setPageTemplate({});
        c->newDocument();
        QCOMPARE(c->pageSize(0), QSizeF(595.27559, 841.88976));
        QCOMPARE(c->document().pages[0].background.style, QStringLiteral("plain"));

        const QColor red(0x80, 0x10, 0x10);
        c->setCanvasColor(red);
        COMPARE_COLOR(f.pixel(QPointF(3, 3)), red);
    }

    void keptBetweenSessions() {
        const QColor green(0x10, 0x90, 0x20);
        {
            Fixture f;
            PageCanvas* c = f.canvas;
            c->setTool(PageCanvas::Highlighter);
            c->setColor(green);
            c->setToolSize(PageCanvas::Fine);
            c->setDrawingType(PageCanvas::Ellipse);
            c->setFill(true);
            c->setFillAlpha(77);
            c->setTool(PageCanvas::Pen);
            c->setLineStyle(QStringLiteral("dash"));
            c->setTool(PageCanvas::Eraser);
            c->setEraserType(PageCanvas::EraseStrokes);
            c->setTextFamily(QStringLiteral("Serif"));
            c->setTextBold(true);
            c->setTextSize(17.5);
            c->setTextAlign(QStringLiteral("center"));
            c->setUsePressure(true);
            c->setFingerDraws(true);
            c->setSelectAllLayers(true);
            c->setPairedPages(true);
            c->setLayoutRows(3);
            c->setLayoutRightToLeft(true);
            c->setAutosaveEnabled(false);
            c->setAutosaveInterval(11);
            c->setCanvasColor(QColor(0x20, 0x30, 0x40));
            c->setButtonAction(PageCanvas::ButtonStylus1, PageCanvas::ButtonFloatingToolbox);
            c->setButtonAction(PageCanvas::ButtonMouseRight, PageCanvas::ButtonNoAction);
            c->setButtonAction(PageCanvas::ButtonEraserTip, PageCanvas::SelectRegion);
            c->setPageTemplate({{QStringLiteral("width"), 400.0},
                                {QStringLiteral("height"), 300.0},
                                {QStringLiteral("color"), QColor(0xff, 0xee, 0xcc)},
                                {QStringLiteral("style"), QStringLiteral("dotted")},
                                {QStringLiteral("config"), QStringLiteral("r1=2")}});
            InputSettings* input = c->input();
            input->setProperty("minimumPressure", 0.2);
            input->setProperty("stabilizerAveraging", InputSettings::VelocityGaussian);
            input->setProperty("stabilizerPreprocessor", InputSettings::Inertia);
            input->setProperty("stabilizerBufferSize", 33);
            input->setProperty("snapGrid", false);
            input->setProperty("zoomStep", 12.5);
            input->setProperty("palmRejectionTime", 900);
            c->saveSettings();
        }

        Fixture f;
        PageCanvas* c = f.canvas;
        f.canvas->setUsePressure(true);  // the fixture switches it off
        QCOMPARE(c->tool(), PageCanvas::Pen);
        QSignalSpy toolChanged(c, &PageCanvas::toolChanged);
        c->loadSettings();
        QVERIFY(toolChanged.count() > 0);

        QCOMPARE(c->tool(), PageCanvas::Eraser);
        QCOMPARE(c->eraserType(), PageCanvas::EraseStrokes);
        c->setTool(PageCanvas::Highlighter);
        QCOMPARE(c->color(), green);
        QCOMPARE(c->toolSize(), PageCanvas::Fine);
        QCOMPARE(c->drawingType(), PageCanvas::Ellipse);
        QVERIFY(c->fill());
        QCOMPARE(c->fillAlpha(), 77);
        c->setTool(PageCanvas::Pen);
        QCOMPARE(c->lineStyle(), QStringLiteral("dash"));
        QCOMPARE(c->drawingType(), PageCanvas::Freehand);
        QCOMPARE(c->color(), PEN_COLOR);
        QCOMPARE(c->textFamily(), QStringLiteral("Serif"));
        QVERIFY(c->textBold());
        QVERIFY(!c->textItalic());
        QCOMPARE(c->textSize(), 17.5);
        QCOMPARE(c->textAlign(), QStringLiteral("center"));
        QVERIFY(c->usePressure());
        QVERIFY(c->fingerDraws());
        QVERIFY(c->selectAllLayers());
        QVERIFY(c->pairedPages());
        QCOMPARE(c->layoutRows(), 3);
        QCOMPARE(c->layoutColumns(), 0);
        QVERIFY(c->layoutRightToLeft());
        QVERIFY(!c->layoutBottomToTop());
        QVERIFY(!c->autosaveEnabled());
        QCOMPARE(c->autosaveInterval(), 11);
        QCOMPARE(c->canvasColor(), QColor(0x20, 0x30, 0x40));
        QCOMPARE(c->buttonAction(PageCanvas::ButtonStylus1), int(PageCanvas::ButtonFloatingToolbox));
        QCOMPARE(c->buttonAction(PageCanvas::ButtonMouseRight), int(PageCanvas::ButtonNoAction));
        QCOMPARE(c->buttonAction(PageCanvas::ButtonEraserTip), int(PageCanvas::SelectRegion));
        QCOMPARE(c->buttonAction(PageCanvas::ButtonMouseMiddle), int(PageCanvas::Hand));
        const QVariantMap pageTemplate = c->pageTemplate();
        QCOMPARE(pageTemplate.value(QStringLiteral("width")).toDouble(), 400.0);
        QCOMPARE(pageTemplate.value(QStringLiteral("color")).value<QColor>(), QColor(0xff, 0xee, 0xcc));
        QCOMPARE(pageTemplate.value(QStringLiteral("style")).toString(), QStringLiteral("dotted"));
        QCOMPARE(pageTemplate.value(QStringLiteral("config")).toString(), QStringLiteral("r1=2"));
        const InputSettings* input = c->input();
        QCOMPARE(input->minimumPressure, 0.2);
        QCOMPARE(input->stabilizerAveraging, InputSettings::VelocityGaussian);
        QCOMPARE(input->stabilizerPreprocessor, InputSettings::Inertia);
        QCOMPARE(input->stabilizerBufferSize, 33);
        QVERIFY(!input->snapGrid);
        QVERIFY(input->snapRotation);
        QCOMPARE(input->zoomStep, 12.5);
        QCOMPARE(input->palmRejectionTime, 900);

        // Back to what the application starts with
        c->resetSettings();
        QCOMPARE(c->tool(), PageCanvas::Pen);
        QCOMPARE(c->lineStyle(), QStringLiteral("plain"));
        QCOMPARE(c->eraserType(), PageCanvas::EraseStandard);
        QCOMPARE(c->textFamily(), QStringLiteral("Sans"));
        QVERIFY(!c->fingerDraws());
        QVERIFY(!c->pairedPages());
        QCOMPARE(c->layoutColumns(), 1);
        QVERIFY(c->autosaveEnabled());
        QCOMPARE(c->canvasColor(), CANVAS_COLOR);
        QCOMPARE(c->buttonAction(PageCanvas::ButtonStylus1), int(PageCanvas::Eraser));
        QVERIFY(c->pageTemplate().isEmpty());
        QCOMPARE(input->stabilizerBufferSize, 20);
        QVERIFY(input->snapGrid);
        QCOMPARE(input->zoomStep, 25.0);
    }

    void unusableSettingsAreIgnored() {
        {
            QSettings settings;
            settings.setValue(QStringLiteral("canvas/tool"), QStringLiteral("Chisel"));
            settings.setValue(QStringLiteral("canvas/tools/Pen/color"), QStringLiteral("no colour"));
            settings.setValue(QStringLiteral("canvas/tools/Pen/size"), 99);
            settings.setValue(QStringLiteral("canvas/tools/Pen/drawingType"), -3);
            settings.setValue(QStringLiteral("canvas/tools/Pen/fillAlpha"), 9999);
            settings.setValue(QStringLiteral("canvas/buttons/Stylus1"), QStringLiteral("Hammer"));
            settings.setValue(QStringLiteral("canvas/canvasColor"), QStringLiteral("nothing"));
            settings.setValue(QStringLiteral("canvas/autosaveInterval"), QStringLiteral("often"));
            settings.setValue(QStringLiteral("canvas/layoutColumns"), -4);
            settings.setValue(QStringLiteral("input/stabilizerBufferSize"), QStringLiteral("many"));
            settings.setValue(QStringLiteral("input/zoomStep"), 40);
        }
        Fixture f;
        PageCanvas* c = f.canvas;
        c->loadSettings();
        QCOMPARE(c->tool(), PageCanvas::Pen);
        QCOMPARE(c->color(), PEN_COLOR);
        QCOMPARE(c->toolSize(), PageCanvas::VeryThick);  // as the fixture sets it
        QCOMPARE(c->drawingType(), PageCanvas::Freehand);
        QCOMPARE(c->fillAlpha(), 255);
        QCOMPARE(c->buttonAction(PageCanvas::ButtonStylus1), int(PageCanvas::Eraser));
        QCOMPARE(c->canvasColor(), CANVAS_COLOR);
        QCOMPARE(c->autosaveInterval(), 3);
        QCOMPARE(c->layoutColumns(), 1);
        QCOMPARE(c->input()->stabilizerBufferSize, 20);
        QCOMPARE(c->input()->zoomStep, 40.0);  // what can be used is used
        // The canvas still works
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(f.strokes().size(), 1);
    }

    void theme() {
        Theme theme;
        QSignalSpy changed(&theme, &Theme::changed);
        QCOMPARE(theme.mode(), QStringLiteral("system"));

        theme.setMode(QStringLiteral("dark"));
        QCOMPARE(changed.count(), 1);
        QVERIFY(theme.dark());
        const QVariantMap dark = theme.colors();
        QVERIFY(dark.value(QStringLiteral("window")).value<QColor>().lightness() < 80);
        QVERIFY(dark.value(QStringLiteral("text")).value<QColor>().lightness() > 200);

        theme.setMode(QStringLiteral("light"));
        QVERIFY(!theme.dark());
        const QVariantMap light = theme.colors();
        QVERIFY(light.value(QStringLiteral("window")).value<QColor>().lightness() > 200);
        QVERIFY(light.value(QStringLiteral("text")).value<QColor>().lightness() < 50);
        QCOMPARE(light.keys(), dark.keys());

        theme.setMode(QStringLiteral("purple"));
        QCOMPARE(theme.mode(), QStringLiteral("light"));

        // Electronic paper: black on white, nothing in between but the text of disabled controls
        QVERIFY(!theme.eink());
        theme.setMode(QStringLiteral("eink"));
        QVERIFY(theme.eink());
        QVERIFY(!theme.dark());
        const QVariantMap paper = theme.colors();
        QCOMPARE(paper.keys(), dark.keys());
        QCOMPARE(paper.value(QStringLiteral("window")).value<QColor>(), QColor(Qt::white));
        QCOMPARE(paper.value(QStringLiteral("button")).value<QColor>(), QColor(Qt::white));
        QCOMPARE(paper.value(QStringLiteral("text")).value<QColor>(), QColor(Qt::black));
        QCOMPARE(paper.value(QStringLiteral("mid")).value<QColor>(), QColor(Qt::black));
        // A device that is electronic paper has them as "system"
        theme.setMode(QStringLiteral("system"));
        qputenv("QOURNAL_EINK", "1");
        QVERIFY(theme.eink());
        QVERIFY(Theme::einkFor(QStringLiteral("system")));
        QVERIFY(!Theme::einkFor(QStringLiteral("dark")));
        qunsetenv("QOURNAL_EINK");
        QVERIFY(!theme.eink());
        theme.setMode(QStringLiteral("light"));

        // Icons
        theme.setIconTheme(QStringLiteral("none"));
        QVERIFY(theme.icons().isEmpty());
        theme.setIconTheme(QStringLiteral("unknown"));
        QCOMPARE(theme.iconTheme(), QStringLiteral("none"));
        theme.setIconTheme(QStringLiteral("lucide"));
        if (!theme.iconsSupported()) {
            QVERIFY(theme.icons().isEmpty());
            QSKIP("This Qt has no SVG plugin: the buttons show text");
        }
        const QVariantMap lucide = theme.icons();
        QCOMPARE(lucide.value(QStringLiteral("tool-pencil")).toString(),
                 QStringLiteral("qrc:/icons/lucide-light/xopp-tool-pencil.svg"));
        QVERIFY(lucide.size() > 80);
        theme.setMode(QStringLiteral("dark"));
        QCOMPARE(theme.icons().value(QStringLiteral("tool-pencil")).toString(),
                 QStringLiteral("qrc:/icons/lucide-dark/xopp-tool-pencil.svg"));

        // The colourful theme, with the Lucide icons for what it lacks
        theme.setMode(QStringLiteral("light"));
        theme.setIconTheme(QStringLiteral("color"));
        const QVariantMap color = theme.icons();
        QCOMPARE(color.value(QStringLiteral("draw-line")).toString(),
                 QStringLiteral("qrc:/icons/color-light/xopp-draw-line.svg"));
        QCOMPARE(color.value(QStringLiteral("tool-pencil")).toString(),
                 QStringLiteral("qrc:/icons/lucide-light/xopp-tool-pencil.svg"));
        for (const QString& name: lucide.keys()) {
            QVERIFY2(color.contains(name), qPrintable(name));
        }
        // Every icon can be read
        for (const QVariant& url: color) {
            const QString path = url.toString().mid(3);  // "qrc:/..." -> ":/..."
            QVERIFY2(!QImage(path).isNull(), qPrintable(path));
        }
    }

    void language() {
        Localization localization;
        QCOMPARE(localization.language(), QString());
        QSignalSpy changed(&localization, &Localization::languageChanged);
        localization.setLanguage(QStringLiteral("de"));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(Localization().language(), QStringLiteral("de"));
        // English is always there; the others depend on the translations that were built
        const QVariantList languages = localization.languages();
        QVERIFY(!languages.isEmpty());
        QCOMPARE(languages.first().toMap().value(QStringLiteral("code")).toString(), QStringLiteral("en"));
        QCOMPARE(languages.size(), Localization::available().size() + 1);

        localization.setLanguage(QStringLiteral("en"));
        QCOMPARE(Localization::install(QCoreApplication::instance()), QStringLiteral("en"));
    }

private:
    QTemporaryDir m_settingsDir;
};

QTEST_MAIN(TestSettings)
#include "tst_settings.moc"
