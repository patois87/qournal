/*
 * Qournal
 *
 * A window with a canvas for the tests, and simulated input of a pen
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QPointingDevice>
#include <QQuickWindow>
#include <QTabletEvent>
#include <QTest>
#include <QWheelEvent>

#include "Document.h"
#include "PageCanvas.h"

inline const QColor CANVAS_COLOR(0x5a, 0x5a, 0x60);
inline const QColor PEN_COLOR(0x00, 0x2e, 0x99);

inline QUrl dataFile(const QString& name) { return QUrl::fromLocalFile(QStringLiteral(TEST_DATA_DIR "/") + name); }

inline bool similar(const QColor& a, const QColor& b, int tolerance = 24) {
    return std::abs(a.red() - b.red()) < tolerance && std::abs(a.green() - b.green()) < tolerance &&
           std::abs(a.blue() - b.blue()) < tolerance;
}

/// A window with a canvas that fills it
struct Fixture {
    explicit Fixture(const QSize& size = QSize(800, 600)) {
        window.resize(size);
        canvas = new PageCanvas(window.contentItem());
        canvas->setSize(size);
        // Strokes wide enough to find their colour in the middle of them
        canvas->setToolSize(PageCanvas::VeryThick);
        canvas->setUsePressure(false);
        window.show();
    }

    /// The window content, once everything is rendered
    QImage grab() {
        if (!QTest::qWaitFor([this] { return !canvas->rendering(); })) {
            qWarning("The canvas did not finish rendering");
        }
        return window.grabWindow();
    }

    QColor pixel(const QPointF& pos) { return grab().pixelColor(pos.toPoint()); }

    /// View position of a point given in page coordinates
    QPointF onPage(int page, const QPointF& pos) const {
        return canvas->pageViewRect(page).topLeft() + pos * canvas->zoom();
    }

    void tablet(QEvent::Type type, const QPointF& pos, qreal pressure,
                QPointingDevice::PointerType pointer = QPointingDevice::PointerType::Pen,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        // Never destroyed: a device must not outlive the application, which static objects would
        static const auto* pen = new QPointingDevice(
                QStringLiteral("test pen"), 42, QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        static const auto* eraser = new QPointingDevice(
                QStringLiteral("test pen"), 43, QInputDevice::DeviceType::Stylus,
                QPointingDevice::PointerType::Eraser,
                QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1, 3);
        const bool down = type != QEvent::TabletRelease;
        QTabletEvent event(type, pointer == QPointingDevice::PointerType::Eraser ? eraser : pen, pos,
                           window.mapToGlobal(pos), pressure, 0, 0, 0, 0, 0, modifiers,
                           type == QEvent::TabletMove ? Qt::NoButton : Qt::LeftButton,
                           down ? Qt::LeftButton : Qt::NoButton);
        QCoreApplication::sendEvent(&window, &event);
    }

    /// A straight pen stroke between two view positions
    void stroke(const QPointF& from, const QPointF& to,
                QPointingDevice::PointerType pointer = QPointingDevice::PointerType::Pen,
                Qt::KeyboardModifiers modifiers = Qt::NoModifier, qreal pressure = 0.5) {
        tablet(QEvent::TabletPress, from, pressure, pointer, modifiers);
        for (int i = 1; i <= 10; ++i) {
            tablet(QEvent::TabletMove, from + (to - from) * i / 10.0, pressure, pointer, modifiers);
        }
        tablet(QEvent::TabletRelease, to, 0, pointer, modifiers);
    }

    /// A pen stroke through points given in page coordinates
    void strokeOnPage(int page, const QList<QPointF>& points, qreal pressure = 0.5) {
        tablet(QEvent::TabletPress, onPage(page, points.first()), pressure);
        for (qsizetype i = 1; i < points.size(); ++i) {
            tablet(QEvent::TabletMove, onPage(page, points[i]), pressure);
        }
        tablet(QEvent::TabletRelease, onPage(page, points.last()), 0);
    }

    /// Press and release at one position
    void tap(const QPointF& pos) {
        tablet(QEvent::TabletPress, pos, 0.5);
        tablet(QEvent::TabletRelease, pos, 0);
    }

    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QTest::keyClick(&window, key, modifiers);
    }

    /// The strokes of a page, of all its layers
    QList<Stroke> strokes(int page = 0) const {
        QList<Stroke> result;
        for (const Layer& layer: canvas->document().pages[static_cast<size_t>(page)].layers) {
            for (const Element& element: layer.elements) {
                if (const auto* stroke = std::get_if<Stroke>(&element)) {
                    result.append(*stroke);
                }
            }
        }
        return result;
    }

    void wheel(const QPointF& pos, int angleDelta, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
        QWheelEvent event(pos, window.mapToGlobal(pos), QPoint(), QPoint(0, angleDelta), Qt::NoButton, modifiers,
                          Qt::NoScrollPhase, false);
        QCoreApplication::sendEvent(&window, &event);
    }

    QQuickWindow window;
    PageCanvas* canvas = nullptr;
};

#define COMPARE_COLOR(actual, expected) \
    do { \
        const QColor actualColor = (actual); \
        const QColor expectedColor = (expected); \
        QVERIFY2(similar(actualColor, expectedColor), \
                 qPrintable(QStringLiteral("%1 instead of %2").arg(actualColor.name(), expectedColor.name()))); \
    } while (false)
