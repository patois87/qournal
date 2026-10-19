/*
 * Qournal
 *
 * Tests for the drawing tools of the canvas, driven by simulated pen input.
 * They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>

#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "CanvasFixture.h"
#include "ColorPalette.h"
#include "GeometryTool.h"
#include "MindMap.h"
#include "Renderer.h"
#include "XoppLoader.h"
#include "XoppWriter.h"

namespace {

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-6) {
    return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}

QString describe(const QList<QPointF>& points) {
    QStringList parts;
    for (const QPointF& p: points) {
        parts.append(QStringLiteral("(%1, %2)").arg(p.x()).arg(p.y()));
    }
    return parts.join(u' ');
}

/// Points along a polygon, every step units, with a deterministic wobble like a hand drawn stroke
QList<QPointF> handDrawn(const QList<QPointF>& corners, double step, double wobble) {
    QList<QPointF> points;
    int n = 0;
    for (qsizetype i = 0; i + 1 < corners.size(); ++i) {
        const QPointF a = corners[i];
        const QPointF b = corners[i + 1];
        const double length = std::hypot(b.x() - a.x(), b.y() - a.y());
        for (double d = 0; d < length; d += step, ++n) {
            points.append(a + (b - a) * (d / length) + QPointF(wobble * std::sin(n * 1.7), wobble * std::cos(n * 2.3)));
        }
    }
    points.append(corners.last());
    return points;
}

/// A fixture without snapping, so that shapes are where the pen is
struct FreeFixture: Fixture {
    FreeFixture() {
        canvas->input()->setProperty("snapGrid", false);
        canvas->input()->setProperty("snapRotation", false);
    }
};

}  // namespace

#define COMPARE_POINT(actual, expected) \
    do { \
        const QPointF actualPoint = (actual); \
        const QPointF expectedPoint = (expected); \
        QVERIFY2(near(actualPoint, expectedPoint, 1e-6), \
                 qPrintable(QStringLiteral("(%1, %2) instead of (%3, %4)") \
                                    .arg(actualPoint.x()) \
                                    .arg(actualPoint.y()) \
                                    .arg(expectedPoint.x()) \
                                    .arg(expectedPoint.y()))); \
    } while (false)

class TestDrawing: public QObject {
    Q_OBJECT

private slots:
    void toolsRememberTheirSettings() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy toolChanged(c, &PageCanvas::toolChanged);

        // The sizes of Xournal++
        QCOMPARE(c->thickness(), 5.67);
        c->setToolSize(PageCanvas::Medium);
        QCOMPARE(c->thickness(), 1.41);
        c->setToolSize(PageCanvas::VeryFine);
        QCOMPARE(c->thickness(), 0.42);
        QCOMPARE(toolChanged.count(), 2);

        c->setTool(PageCanvas::Highlighter);
        QCOMPARE(c->toolSize(), PageCanvas::Medium);
        QCOMPARE(c->thickness(), 8.5);
        QCOMPARE(c->color(), QColor(0xff, 0xe1, 0x6b));
        c->setColor(Qt::green);
        c->setDrawingType(PageCanvas::Rectangle);
        c->setFill(true);

        c->setTool(PageCanvas::Eraser);
        QCOMPARE(c->thickness(), 8.5);
        c->setToolSize(PageCanvas::Thick);
        QCOMPARE(c->thickness(), 12.0);

        c->setTool(PageCanvas::LaserPen);
        QCOMPARE(c->thickness(), 2.4);
        QCOMPARE(c->color(), QColor(Qt::red));

        // Back at the pen: nothing of the other tools
        c->setTool(PageCanvas::Pen);
        QCOMPARE(c->color(), PEN_COLOR);
        QCOMPARE(c->toolSize(), PageCanvas::VeryFine);
        QCOMPARE(c->drawingType(), PageCanvas::Freehand);
        QVERIFY(!c->fill());
        c->setTool(PageCanvas::Highlighter);
        QCOMPARE(c->color(), QColor(Qt::green));
        QCOMPARE(c->drawingType(), PageCanvas::Rectangle);
        QVERIFY(c->fill());

        c->setFillAlpha(999);
        QCOMPARE(c->fillAlpha(), 255);
    }

    void penAndHighlighterStrokes() {
        Fixture f;
        f.strokeOnPage(0, {{100, 100}, {150, 100}, {200, 120}});
        f.canvas->setTool(PageCanvas::Highlighter);
        f.strokeOnPage(0, {{100, 200}, {200, 200}});

        const QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 2);
        QCOMPARE(strokes[0].tool, Stroke::Tool::Pen);
        QCOMPARE(strokes[0].color, PEN_COLOR);
        QCOMPARE(strokes[0].width, 5.67);
        QCOMPARE(strokes[0].points.size(), 3);
        COMPARE_POINT(strokes[0].points[2], QPointF(200, 120));
        QCOMPARE(strokes[0].fill, -1);
        QVERIFY(strokes[0].style.isEmpty());

        QCOMPARE(strokes[1].tool, Stroke::Tool::Highlighter);
        QCOMPARE(strokes[1].color, QColor(0xff, 0xe1, 0x6b, 0x7f));
        QCOMPARE(strokes[1].width, 8.5);

        // A tap is a dot
        f.canvas->setTool(PageCanvas::Pen);
        f.tap(f.onPage(0, QPointF(300, 300)));
        QCOMPARE(f.strokes().size(), 3);
        QCOMPARE(f.strokes()[2].points.size(), 2);
        COMPARE_POINT(f.strokes()[2].points[0], f.strokes()[2].points[1]);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(300, 300))), PEN_COLOR);

        // Outside of the pages nothing is drawn
        f.tap(QPointF(3, 300));
        QCOMPARE(f.strokes().size(), 3);
    }

    void pressureChangesTheWidth() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setUsePressure(true);
        c->setToolSize(PageCanvas::Thick);  // 2.26 pt

        f.strokeOnPage(0, {{100, 100}, {150, 100}, {200, 100}}, 0.5);
        Stroke stroke = f.strokes().last();
        QVERIFY(stroke.hasPressure());
        QCOMPARE(stroke.widths.size(), 2);
        QVERIFY(near(stroke.widths[0], 1.13));
        QVERIFY(near(stroke.widths[1], 1.13));

        // The settings of the pressure: a lower limit and a multiplier
        c->input()->setProperty("minimumPressure", 0.8);
        f.strokeOnPage(0, {{100, 120}, {150, 120}, {200, 120}}, 0.5);
        QVERIFY(near(f.strokes().last().widths[0], 0.8 * 2.26));
        c->input()->setProperty("minimumPressure", 0.05);
        c->input()->setProperty("pressureMultiplier", 1.5);
        f.strokeOnPage(0, {{100, 140}, {150, 140}, {200, 140}}, 0.5);
        QVERIFY(near(f.strokes().last().widths[0], 0.75 * 2.26));

        // The highlighter has a constant width, and so has the pen without pressure sensitivity
        c->setTool(PageCanvas::Highlighter);
        f.strokeOnPage(0, {{100, 160}, {200, 160}}, 0.5);
        QVERIFY(!f.strokes().last().hasPressure());
        c->setTool(PageCanvas::Pen);
        c->setUsePressure(false);
        f.strokeOnPage(0, {{100, 180}, {200, 180}}, 0.5);
        QVERIFY(!f.strokes().last().hasPressure());
    }

    void stabilizer() {
        Fixture f;
        PageCanvas* c = f.canvas;
        const double zoom = c->zoom();
        c->input()->setProperty("stabilizerPreprocessor", InputSettings::Deadzone);
        c->input()->setProperty("stabilizerDeadzoneRadius", 20.0);
        c->input()->setProperty("stabilizerFinalizeStroke", false);

        // The stroke follows the pen at the distance of the deadzone (in pixels)
        f.stroke(f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));
        COMPARE_POINT(f.strokes().last().points.last(), QPointF(300 - 20 / zoom, 100));

        // ... and catches up at the end if it is finalized
        c->input()->setProperty("stabilizerFinalizeStroke", true);
        f.stroke(f.onPage(0, QPointF(100, 150)), f.onPage(0, QPointF(300, 150)));
        COMPARE_POINT(f.strokes().last().points.last(), QPointF(300, 150));

        c->input()->setProperty("stabilizerPreprocessor", InputSettings::Inertia);
        c->input()->setProperty("stabilizerAveraging", InputSettings::Arithmetic);
        f.stroke(f.onPage(0, QPointF(100, 200)), f.onPage(0, QPointF(300, 200)));
        COMPARE_POINT(f.strokes().last().points.last(), QPointF(300, 200));
        QCOMPARE(f.strokes().size(), 3);
    }

    void shapes() {
        FreeFixture f;
        PageCanvas* c = f.canvas;
        const QPointF from = f.onPage(0, QPointF(100, 100));
        const QPointF to = f.onPage(0, QPointF(300, 180));

        c->setDrawingType(PageCanvas::Line);
        f.stroke(from, to);
        QCOMPARE(f.strokes().last().points.size(), 2);
        COMPARE_POINT(f.strokes().last().points[0], QPointF(100, 100));
        COMPARE_POINT(f.strokes().last().points[1], QPointF(300, 180));
        QVERIFY(!f.strokes().last().hasPressure());

        c->setDrawingType(PageCanvas::Rectangle);
        f.stroke(from, to);
        QCOMPARE(f.strokes().last().points.size(), 5);
        COMPARE_POINT(f.strokes().last().points[1], QPointF(100, 180));
        COMPARE_POINT(f.strokes().last().points[2], QPointF(300, 180));
        COMPARE_POINT(f.strokes().last().points[4], QPointF(100, 100));

        c->setDrawingType(PageCanvas::Ellipse);
        f.stroke(from, to);
        QVERIFY(f.strokes().last().points.size() > 100);
        COMPARE_POINT(f.strokes().last().points.first(), QPointF(300, 140));
        COMPARE_POINT(f.strokes().last().points.last(), QPointF(300, 140));

        c->setDrawingType(PageCanvas::Arrow);
        f.stroke(from, to);
        QCOMPARE(f.strokes().last().points.size(), 6);
        COMPARE_POINT(f.strokes().last().points[1], QPointF(300, 180));

        c->setDrawingType(PageCanvas::DoubleArrow);
        f.stroke(from, to);
        QCOMPARE(f.strokes().last().points.size(), 10);

        c->setDrawingType(PageCanvas::CoordinateSystem);
        f.stroke(from, to);
        QCOMPARE(f.strokes().last().points.size(), 3);
        COMPARE_POINT(f.strokes().last().points[1], QPointF(100, 180));

        QCOMPARE(f.strokes().size(), 6);
        // The rectangle is on the screen, and every shape is one step to undo
        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(100, 140)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 100)).toPoint()), PEN_COLOR);
        for (int i = 0; i < 6; ++i) {
            QVERIFY(c->canUndo());
            c->undo();
        }
        QVERIFY(!c->canUndo());
        QVERIFY(f.strokes().isEmpty());

        // A tap is not a shape
        c->setDrawingType(PageCanvas::Rectangle);
        f.tap(from);
        QVERIFY(f.strokes().isEmpty());
        QVERIFY(!c->canUndo());
    }

    void shapeModifiersAndSnapping() {
        Fixture f;
        PageCanvas* c = f.canvas;
        const QPointF from = f.onPage(0, QPointF(100, 100));

        // By default the corners snap to the grid of 5 mm
        c->setDrawingType(PageCanvas::Rectangle);
        f.stroke(from, f.onPage(0, QPointF(300, 180)));
        COMPARE_POINT(f.strokes().last().points[0], QPointF(7 * 14.17, 7 * 14.17));
        COMPARE_POINT(f.strokes().last().points[2], QPointF(21 * 14.17, 13 * 14.17));

        // ... and lines to steps of 15°
        c->input()->setProperty("snapGrid", false);
        c->setDrawingType(PageCanvas::Line);
        f.stroke(from, f.onPage(0, QPointF(300, 103)));
        COMPARE_POINT(f.strokes().last().points[1], QPointF(100 + std::hypot(200, 3), 100));
        // Alt toggles both: no steps of 15° now, but the grid
        f.stroke(from, f.onPage(0, QPointF(300, 103)), QPointingDevice::PointerType::Pen, Qt::AltModifier);
        COMPARE_POINT(f.strokes().last().points[0], QPointF(7 * 14.17, 7 * 14.17));
        COMPARE_POINT(f.strokes().last().points[1], QPointF(21 * 14.17, 7 * 14.17));
        c->input()->setProperty("snapRotation", false);
        f.stroke(from, f.onPage(0, QPointF(300, 103)));
        COMPARE_POINT(f.strokes().last().points[1], QPointF(300, 103));

        // Shift: a square; Control: from the center
        c->setDrawingType(PageCanvas::Rectangle);
        f.stroke(from, f.onPage(0, QPointF(300, 180)), QPointingDevice::PointerType::Pen, Qt::ShiftModifier);
        COMPARE_POINT(f.strokes().last().points[2], QPointF(300, 300));
        f.stroke(from, f.onPage(0, QPointF(150, 180)), QPointingDevice::PointerType::Pen, Qt::ControlModifier);
        COMPARE_POINT(f.strokes().last().points[0], QPointF(50, 20));
        COMPARE_POINT(f.strokes().last().points[2], QPointF(150, 180));
    }

    void fillAndLineStyle() {
        FreeFixture f;
        PageCanvas* c = f.canvas;
        c->setDrawingType(PageCanvas::Rectangle);
        c->setFill(true);
        c->setFillAlpha(200);
        c->setLineStyle(QStringLiteral("dash"));
        c->setColor(Qt::red);
        c->setToolSize(PageCanvas::Thick);
        f.stroke(f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 200)));

        Stroke stroke = f.strokes().last();
        QCOMPARE(stroke.fill, 200);
        QCOMPARE(stroke.style, QStringLiteral("dash"));
        QVERIFY(!stroke.dashes.isEmpty());
        QCOMPARE(stroke.color, QColor(Qt::red));

        // The inside is filled with the transparent colour
        const QColor inside = f.pixel(f.onPage(0, QPointF(200, 150)));
        QVERIFY2(inside.red() == 255 && inside.green() > 30 && inside.green() < 80, qPrintable(inside.name()));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 250))), Qt::white);

        // The highlighter can fill, but has no line style
        c->setTool(PageCanvas::Highlighter);
        c->setDrawingType(PageCanvas::Ellipse);
        c->setFill(true);
        c->setLineStyle(QStringLiteral("dot"));
        f.stroke(f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(300, 400)));
        stroke = f.strokes().last();
        QCOMPARE(stroke.tool, Stroke::Tool::Highlighter);
        QCOMPARE(stroke.fill, 128);
        QVERIFY(stroke.style.isEmpty());

        // Fill and line style are written to the file
        Document reloaded;
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shapes.xopp"));
        QVERIFY(saveXopp(path, c->document(), nullptr));
        QVERIFY(loadXopp(path, reloaded, nullptr));
        const auto& first = std::get<Stroke>(reloaded.pages[0].layers[0].elements[0]);
        QCOMPARE(first.fill, 200);
        QCOMPARE(first.style, QStringLiteral("dash"));
        QCOMPARE(first.points.size(), 5);
        QCOMPARE(std::get<Stroke>(reloaded.pages[0].layers[0].elements[1]).fill, 128);
    }

    void spline() {
        FreeFixture f;
        PageCanvas* c = f.canvas;
        c->setDrawingType(PageCanvas::Spline);

        // Taps set the knots; without tangents they are joined by straight lines
        f.tap(f.onPage(0, QPointF(100, 100)));
        f.tap(f.onPage(0, QPointF(200, 100)));
        f.tap(f.onPage(0, QPointF(200, 200)));
        QVERIFY(f.strokes().isEmpty());
        // The knots are marked while the spline is built
        const QColor knotMark = f.pixel(f.onPage(0, QPointF(100, 100)) + QPointF(0, -10));
        QVERIFY2(similar(knotMark, Qt::red, 90), qPrintable(knotMark.name()));
        f.key(Qt::Key_Escape);
        QCOMPARE(f.strokes().size(), 1);
        QVERIFY2(f.strokes()[0].points.size() == 3, qPrintable(describe(f.strokes()[0].points)));
        COMPARE_POINT(f.strokes()[0].points[2], QPointF(200, 200));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(100, 100)) + QPointF(0, -10)), Qt::white);
        QCOMPARE(f.strokes()[0].width, 5.67);

        // Dragging sets the tangent of a knot: the curve leaves it in that direction
        f.tablet(QEvent::TabletPress, f.onPage(0, QPointF(100, 300)), 0.5);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(100, 350)), 0.5);
        f.tablet(QEvent::TabletRelease, f.onPage(0, QPointF(100, 350)), 0);
        f.tap(f.onPage(0, QPointF(200, 300)));
        // A second tap at the same place ends the spline
        f.tap(f.onPage(0, QPointF(200, 300)));
        QCOMPARE(f.strokes().size(), 2);
        const QList<QPointF> curve = f.strokes()[1].points;
        QVERIFY(curve.size() > 8);
        COMPARE_POINT(curve.first(), QPointF(100, 300));
        COMPARE_POINT(curve.last(), QPointF(200, 300));
        QVERIFY(curve[1].y() > 300 && near(curve[1].x(), 100, 1.0));

        // Backspace removes the last knot
        f.tap(f.onPage(0, QPointF(300, 100)));
        f.tap(f.onPage(0, QPointF(350, 100)));
        f.tap(f.onPage(0, QPointF(350, 150)));
        f.tap(f.onPage(0, QPointF(300, 150)));
        f.key(Qt::Key_Backspace);
        // The arrow keys move the last knot
        f.key(Qt::Key_Down);
        f.key(Qt::Key_Down);
        // A tap on the first knot closes the spline
        f.tap(f.onPage(0, QPointF(301, 101)));
        QCOMPARE(f.strokes().size(), 3);
        const QList<QPointF> closed = f.strokes()[2].points;
        QVERIFY2(closed.size() == 4, qPrintable(describe(closed)));
        COMPARE_POINT(closed[2], QPointF(350, 152));
        COMPARE_POINT(closed.last(), closed.first());

        // Changing the tool ends the spline; a single knot is not a spline
        f.tap(f.onPage(0, QPointF(400, 100)));
        f.tap(f.onPage(0, QPointF(450, 100)));
        c->setTool(PageCanvas::Eraser);
        QCOMPARE(f.strokes().size(), 4);
        c->setTool(PageCanvas::Pen);
        f.tap(f.onPage(0, QPointF(400, 300)));
        c->setDrawingType(PageCanvas::Freehand);
        QCOMPARE(f.strokes().size(), 4);
        for (int i = 0; i < 4; ++i) {
            c->undo();
        }
        QVERIFY(f.strokes().isEmpty() && !c->canUndo());
    }

    void shapeRecognizer() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setDrawingType(PageCanvas::ShapeRecognizer);

        const QList<QPointF> drawn = handDrawn({{100, 100}, {302, 103}, {300, 221}, {98, 218}, {101, 102}}, 4, 0.7);
        f.strokeOnPage(0, drawn);
        QCOMPARE(f.strokes().size(), 1);
        const Stroke shape = f.strokes()[0];
        QVERIFY2(shape.points.size() == 5, qPrintable(describe(shape.points)));
        QCOMPARE(shape.color, PEN_COLOR);
        COMPARE_COLOR(f.pixel(f.onPage(0, shape.points[0] + (shape.points[1] - shape.points[0]) / 2)), PEN_COLOR);

        // Undo brings back the stroke as it was drawn, then removes it
        c->undo();
        QCOMPARE(f.strokes().size(), 1);
        QVERIFY(f.strokes()[0].points.size() > 50);
        c->undo();
        QVERIFY(f.strokes().isEmpty());
        c->redo();
        c->redo();
        QCOMPARE(f.strokes()[0].points.size(), 5);

        // What is not a shape stays as it is
        QList<QPointF> wave;
        for (int i = 0; i <= 80; ++i) {
            wave.append(QPointF(100 + 3 * i, 400 + 40 * std::sin(i / 8.0)));
        }
        f.strokeOnPage(0, wave);
        QCOMPARE(f.strokes().size(), 2);
        QCOMPARE(f.strokes()[1].points.size(), 81);
    }

    void mindMapTool() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::MindMap);

        // A word: it stays as it is written
        for (int i = 0; i < 3; ++i) {
            const double x = 100 + 25 * i;
            f.strokeOnPage(0, handDrawn({{x, 130}, {x + 8, 100}, {x + 16, 130}}, 2, 0.3));
        }
        QCOMPARE(f.strokes().size(), 3);

        // A loop around it becomes an ellipse around the writing, in the colour of the tool
        QList<QPointF> loop;
        for (int i = 0; i <= 80; ++i) {
            const double t = -2.0 + 2 * M_PI * i / 80;
            loop.append(QPointF(133 + 70 * std::cos(t), 115 + 35 * std::sin(t)));
        }
        f.strokeOnPage(0, loop);
        QCOMPARE(f.strokes().size(), 4);
        const Stroke node = f.strokes()[3];
        QCOMPARE(node.points.first(), node.points.last());
        QCOMPARE(node.color, PEN_COLOR);
        QVERIFY(QPolygonF(node.points).boundingRect().width() < 2 * 70);

        // Undo brings back the loop as it was drawn, then removes it
        c->undo();
        QCOMPARE(f.strokes().size(), 4);
        QCOMPARE(f.strokes()[3].points.size(), loop.size());
        c->undo();
        QCOMPARE(f.strokes().size(), 3);
        c->redo();
        c->redo();
        QCOMPARE(f.strokes()[3].points, node.points);

        // A stroke from the node into empty space becomes a branch from its outline
        f.strokeOnPage(0, handDrawn({{150, 125}, {250, 220}, {320, 300}}, 3, 1.0));
        QCOMPARE(f.strokes().size(), 5);
        const Stroke branch = f.strokes()[4];
        QVERIFY(branch.hasPressure());
        double fromOutline = 1e9;
        for (qsizetype i = 0; i + 1 < node.points.size(); ++i) {
            const QPointF a = node.points[i];
            const QPointF d = node.points[i + 1] - a;
            const double t = std::clamp(QPointF::dotProduct(branch.points[0] - a, d) / QPointF::dotProduct(d, d), 0.0,
                                        1.0);
            fromOutline = std::min(fromOutline, QLineF(branch.points[0], a + d * t).length());
        }
        QVERIFY(fromOutline < 0.5);
        QVERIFY(QLineF(branch.points.last(), QPointF(320, 300)).length() < 2);

        // A "V" at its end puts a head on it, and goes itself
        const QPointF end = branch.points.last();
        const QPointF back = branch.points[branch.points.size() - 4];
        const double angle = std::atan2(end.y() - back.y(), end.x() - back.x());
        auto leg = [&](double sign) {
            return end - 12 * QPointF(std::cos(angle + sign * M_PI / 6), std::sin(angle + sign * M_PI / 6));
        };
        f.strokeOnPage(0, handDrawn({leg(1), end, leg(-1)}, 1.5, 0.3));
        QCOMPARE(f.strokes().size(), 5);
        QVERIFY(MindMap::hasArrowHead(f.strokes()[4].points));

        // Undo brings back the "V" and the branch without its head, then removes the "V"
        c->undo();
        QCOMPARE(f.strokes().size(), 6);
        QCOMPARE(f.strokes()[4].points, branch.points);
        c->undo();
        QCOMPARE(f.strokes().size(), 5);
        c->redo();
        c->redo();
        QCOMPARE(f.strokes().size(), 5);
        QVERIFY(MindMap::hasArrowHead(f.strokes()[4].points));
    }

    void mindMapEmptyLoops() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::MindMap);
        // Loops as a hand draws them, whose ends overlap
        auto loop = [](const QPointF& center, double a, double b) {
            QList<QPointF> points;
            for (int i = 0; i <= 88; ++i) {
                const double t = -2.0 + 2 * M_PI * i / 80;
                points.append(center + QPointF(a * std::cos(t), b * std::sin(t)));
            }
            return points;
        };
        auto closed = [](const Stroke& stroke) { return stroke.points.first() == stroke.points.last(); };

        // A large "o" and the rest of the word right after it: all writing, also after a while
        const QList<QPointF> o = loop({120, 120}, 22, 22);
        f.strokeOnPage(0, o);
        f.strokeOnPage(0, handDrawn({{150, 90}, {150, 140}, {170, 125}, {152, 120}}, 2, 0.3));
        const QList<Stroke> word = f.strokes();
        QTest::qWait(PageCanvas::EMPTY_NODE_DELAY_MS + 300);
        QCOMPARE(f.strokes().size(), 2);
        QCOMPARE(f.strokes()[0].points, word[0].points);
        QCOMPARE(f.strokes()[1].points, word[1].points);
        QVERIFY(!closed(word[0]));

        // An empty loop by itself becomes a node after a moment, as a step of its own
        f.strokeOnPage(0, loop({200, 400}, 60, 30));
        QVERIFY(!closed(f.strokes()[2]));
        QTRY_VERIFY_WITH_TIMEOUT(closed(f.strokes()[2]), 3 * PageCanvas::EMPTY_NODE_DELAY_MS);
        c->undo();
        QCOMPARE(f.strokes().size(), 3);
        QVERIFY(!closed(f.strokes()[2]));
        c->undo();
        QCOMPARE(f.strokes().size(), 2);

        // Writing put into it at once makes it a node then, around the writing
        f.strokeOnPage(0, loop({200, 400}, 60, 30));
        f.strokeOnPage(0, handDrawn({{180, 410}, {188, 390}, {196, 410}}, 2, 0.3));
        QCOMPARE(f.strokes().size(), 4);
        QVERIFY(closed(f.strokes()[2]));

        // A branch drawn from it at once too
        f.strokeOnPage(0, loop({500, 400}, 50, 30));
        f.strokeOnPage(0, handDrawn({{500, 432}, {520, 560}, {540, 700}}, 3, 1.0));
        QCOMPARE(f.strokes().size(), 6);
        QVERIFY(closed(f.strokes()[4]));

        // And anything drawn far from it
        f.strokeOnPage(0, loop({420, 150}, 40, 40));
        f.strokeOnPage(0, handDrawn({{150, 300}, {170, 320}}, 2, 0.3));
        QCOMPARE(f.strokes().size(), 8);
        QVERIFY(closed(f.strokes()[6]));

        // Written slowly: the "o" became a node after the pause, and the next letter beside it turns it back
        const QList<QPointF> slow = loop({300, 250}, 22, 22);
        f.strokeOnPage(0, slow);
        const QList<QPointF> drawn = f.strokes()[8].points;
        QTRY_VERIFY_WITH_TIMEOUT(closed(f.strokes()[8]), 3 * PageCanvas::EMPTY_NODE_DELAY_MS);
        f.strokeOnPage(0, handDrawn({{330, 220}, {330, 270}, {350, 255}, {332, 250}}, 2, 0.3));
        QCOMPARE(f.strokes().size(), 10);
        QCOMPARE(f.strokes()[8].points, drawn);
        // Undo brings back the node, then removes it
        c->undo();
        c->undo();
        QCOMPARE(f.strokes().size(), 9);
        QVERIFY(closed(f.strokes()[8]));

        // Switched off, a large one stays writing too
        c->input()->mindMapEmptyNodes = false;
        f.strokeOnPage(0, loop({300, 160}, 50, 25));
        QTest::qWait(PageCanvas::EMPTY_NODE_DELAY_MS + 300);
        QCOMPARE(f.strokes().size(), 10);
        QVERIFY(!closed(f.strokes()[9]));
        c->input()->mindMapEmptyNodes = true;

        // Large on the page, but no larger on the screen than a letter, as on a phone: writing, also after a while.
        // Zoomed out so far that 10 mm on the screen are 70 points across
        const double dpi = c->window()->screen()->physicalDotsPerInch();
        const double radius = 70 / 2 / M_SQRT2;
        c->zoomTo(10 / 25.4 * dpi / 70 * 72 / c->displayDpi());
        f.strokeOnPage(0, loop({450, 300}, radius, radius));
        QTest::qWait(PageCanvas::EMPTY_NODE_DELAY_MS + 300);
        QCOMPARE(f.strokes().size(), 11);
        QVERIFY(!closed(f.strokes()[10]));
    }

    void mindMapNodesMove() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::MindMap);
        for (int i = 0; i < 3; ++i) {
            const double x = 100 + 25 * i;
            f.strokeOnPage(0, handDrawn({{x, 130}, {x + 8, 100}, {x + 16, 130}}, 2, 0.3));
        }
        QList<QPointF> loop;
        for (int i = 0; i <= 80; ++i) {
            const double t = -2.0 + 2 * M_PI * i / 80;
            loop.append(QPointF(133 + 70 * std::cos(t), 115 + 35 * std::sin(t)));
        }
        f.strokeOnPage(0, loop);
        f.strokeOnPage(0, handDrawn({{150, 125}, {250, 220}, {320, 300}}, 3, 1.0));
        QCOMPARE(f.strokes().size(), 5);
        const QList<Stroke> before = f.strokes();

        // Pressed on its outline and dragged, the node moves with its writing, and the branch follows
        c->setTool(PageCanvas::SelectObject);
        const QPointF onOutline = before[3].points[before[3].points.size() / 4];
        f.stroke(f.onPage(0, onOutline), f.onPage(0, onOutline + QPointF(-40, 60)));
        QVERIFY(c->hasSelection());
        const QList<Stroke> after = f.strokes();
        for (int i = 0; i < 4; ++i) {
            COMPARE_POINT(after[i].points.first(), before[i].points.first() + QPointF(-40, 60));
        }
        const QPolygonF outline(after[3].points);
        COMPARE_POINT(after[4].points.last(), before[4].points.last());
        double fromOutline = 1e9;
        for (qsizetype i = 0; i + 1 < outline.size(); ++i) {
            const QPointF a = outline[i];
            const QPointF d = outline[i + 1] - a;
            const double t = std::clamp(QPointF::dotProduct(after[4].points.first() - a, d) / QPointF::dotProduct(d, d),
                                        0.0, 1.0);
            fromOutline = std::min(fromOutline, QLineF(after[4].points.first(), a + d * t).length());
        }
        QVERIFY(fromOutline < 0.5);

        // One step to undo
        c->undo();
        for (int i = 0; i < 5; ++i) {
            QCOMPARE(f.strokes()[i].points, before[i].points);
        }

        // Without the setting, the outline moves by itself
        c->input()->mindMapSelection = false;
        f.stroke(f.onPage(0, onOutline), f.onPage(0, onOutline + QPointF(-40, 60)));
        COMPARE_POINT(f.strokes()[3].points.first(), before[3].points.first() + QPointF(-40, 60));
        COMPARE_POINT(f.strokes()[0].points.first(), before[0].points.first());
        COMPARE_POINT(f.strokes()[4].points.first(), before[4].points.first());
        c->undo();

        // Scribbling over the branch with the mind map tool deletes it, and the scribble goes too
        c->clearSelection();
        c->setTool(PageCanvas::MindMap);
        QList<QPointF> zigzag;
        for (int i = 0; i <= 8; ++i) {
            zigzag.append(QPointF(i % 2 == 0 ? 230 : 290, 200 + 4 * i));
        }
        f.strokeOnPage(0, handDrawn(zigzag, 3, 0.5));
        QCOMPARE(f.strokes().size(), 4);
        // Undo brings back the branch and the scribble, then removes the scribble
        c->undo();
        QCOMPARE(f.strokes().size(), 6);
        QCOMPARE(f.strokes()[4].points, before[4].points);
        c->undo();
        QCOMPARE(f.strokes().size(), 5);
    }

    void eraserSplitsStrokes() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {{100, 100}, {200, 100}, {300, 100}});
        f.strokeOnPage(0, {{100, 200}, {300, 200}});

        c->setTool(PageCanvas::Eraser);
        QCOMPARE(c->eraserType(), PageCanvas::EraseStandard);
        f.stroke(f.onPage(0, QPointF(250, 60)), f.onPage(0, QPointF(250, 140)));

        // Two pieces instead of the first stroke: what was under the eraser (17 pt wide) and its padding is gone
        QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 3);
        const double gap = 8.5 + 0.4 * 5.67;
        COMPARE_POINT(strokes[0].points.first(), QPointF(100, 100));
        COMPARE_POINT(strokes[0].points.last(), QPointF(250 - gap, 100));
        QCOMPARE(strokes[0].points.size(), 3);
        COMPARE_POINT(strokes[1].points.first(), QPointF(250 + gap, 100));
        COMPARE_POINT(strokes[1].points.last(), QPointF(300, 100));
        COMPARE_POINT(strokes[2].points.first(), QPointF(100, 200));

        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(250, 100)).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(230, 100)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(270, 100)).toPoint()), PEN_COLOR);

        // One step to undo
        c->undo();
        strokes = f.strokes();
        QCOMPARE(strokes.size(), 2);
        QCOMPARE(strokes[0].points.size(), 3);
        COMPARE_POINT(strokes[0].points.last(), QPointF(300, 100));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(250, 100))), PEN_COLOR);
        c->redo();
        QCOMPARE(f.strokes().size(), 3);

        // Erasing along a stroke removes it piece by piece, and completely in the end
        f.stroke(f.onPage(0, QPointF(90, 200)), f.onPage(0, QPointF(310, 200)));
        QCOMPARE(f.strokes().size(), 2);
        c->undo();
        QCOMPARE(f.strokes().size(), 3);

        // Next to the strokes nothing happens
        f.stroke(f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(300, 300)));
        QCOMPARE(f.strokes().size(), 3);
    }

    void eraserTypes() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        f.strokeOnPage(0, {{100, 200}, {300, 200}});
        c->setTool(PageCanvas::Eraser);

        // Whiteout paints white over the stroke
        c->setEraserType(PageCanvas::EraseWhiteout);
        f.stroke(f.onPage(0, QPointF(200, 60)), f.onPage(0, QPointF(200, 140)));
        QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 3);
        QCOMPARE(strokes[0].points.size(), 2);
        QCOMPARE(strokes[2].tool, Stroke::Tool::Eraser);
        QCOMPARE(strokes[2].color, QColor(Qt::white));
        QCOMPARE(strokes[2].width, 8.5);
        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 100)).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(220, 100)).toPoint()), PEN_COLOR);

        // Delete strokes: the whole stroke goes
        c->setEraserType(PageCanvas::EraseStrokes);
        f.stroke(f.onPage(0, QPointF(250, 160)), f.onPage(0, QPointF(250, 240)));
        strokes = f.strokes();
        QCOMPARE(strokes.size(), 2);
        COMPARE_POINT(strokes[0].points.first(), QPointF(100, 100));
        QCOMPARE(strokes[1].tool, Stroke::Tool::Eraser);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), Qt::white);
        c->undo();
        QCOMPARE(f.strokes().size(), 3);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), PEN_COLOR);

        // The eraser tip of the pen erases with any tool
        c->setTool(PageCanvas::Pen);
        f.stroke(f.onPage(0, QPointF(250, 160)), f.onPage(0, QPointF(250, 240)), QPointingDevice::PointerType::Eraser);
        QCOMPARE(f.strokes().size(), 2);
    }

    void handTool() {
        Fixture f;
        f.canvas->setTool(PageCanvas::Hand);
        const double top = f.canvas->pageViewRect(0).top();
        f.stroke(QPointF(400, 400), QPointF(400, 300));
        QVERIFY(near(f.canvas->pageViewRect(0).top(), top - 100, 1.0));
        QVERIFY(f.strokes().isEmpty());
        QVERIFY(!f.canvas->canUndo());

        // Also next to the page
        f.stroke(QPointF(3, 300), QPointF(3, 250));
        QVERIFY(near(f.canvas->pageViewRect(0).top(), top - 150, 1.0));
    }

    void verticalSpace() {
        FreeFixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        f.strokeOnPage(0, {{100, 300}, {300, 300}});
        f.strokeOnPage(0, {{100, 350}, {300, 350}});

        // What is below the starting point moves down
        c->setTool(PageCanvas::VerticalSpace);
        f.tablet(QEvent::TabletPress, f.onPage(0, QPointF(50, 200)), 0.5);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(50, 240)), 0.5);
        // While moving the strokes are shown at their new place
        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 340)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 300)).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 100)).toPoint()), PEN_COLOR);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(50, 260)), 0.5);
        f.tablet(QEvent::TabletRelease, f.onPage(0, QPointF(50, 260)), 0);

        QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 3);
        COMPARE_POINT(strokes[0].points[0], QPointF(100, 100));
        COMPARE_POINT(strokes[1].points[0], QPointF(100, 360));
        COMPARE_POINT(strokes[2].points[1], QPointF(300, 410));
        QVERIFY(strokes[1].bounds.contains(QPointF(200, 360)));
        image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 360)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 300)).toPoint()), Qt::white);

        c->undo();
        COMPARE_POINT(f.strokes()[1].points[0], QPointF(100, 300));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 300))), PEN_COLOR);
        c->redo();
        COMPARE_POINT(f.strokes()[1].points[0], QPointF(100, 360));
        c->undo();

        // With Control what is above moves
        f.stroke(f.onPage(0, QPointF(50, 200)), f.onPage(0, QPointF(50, 150)), QPointingDevice::PointerType::Pen,
                 Qt::ControlModifier);
        strokes = f.strokes();
        COMPARE_POINT(strokes[0].points[0], QPointF(100, 50));
        COMPARE_POINT(strokes[1].points[0], QPointF(100, 300));

        // Nothing moved: nothing to undo
        c->undo();
        QVERIFY(c->canUndo());
        const int steps = 3;
        f.tap(f.onPage(0, QPointF(50, 200)));
        f.stroke(f.onPage(0, QPointF(50, 700)), f.onPage(0, QPointF(50, 750)));
        for (int i = 0; i < steps; ++i) {
            QVERIFY(c->canUndo());
            c->undo();
        }
        QVERIFY(!c->canUndo());
    }

    void verticalSpaceMovesAllKindsOfElements() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("snapGrid", false);
        for (const char* name: {"text-fileversion-5.xopp", "image-fileversion-5.xopp", "links-fileversion-5.xopp",
                                "latex-fileversion-5.xopp", "text-fileversion-4.xopp"}) {
            c->openFile(dataFile(QString::fromLatin1(name)));
            const Document before = c->document();
            size_t count = 0;
            for (const Layer& layer: before.pages[0].layers) {
                count += layer.elements.size();
            }
            QVERIFY(count > 0);

            // From the top of the page: everything moves
            c->setTool(PageCanvas::VerticalSpace);
            f.stroke(f.onPage(0, QPointF(10, 0.5)), f.onPage(0, QPointF(10, 40.5)));
            const Document& after = c->document();
            for (size_t l = 0; l < before.pages[0].layers.size(); ++l) {
                QCOMPARE(after.pages[0].layers[l].elements.size(), before.pages[0].layers[l].elements.size());
                for (size_t e = 0; e < before.pages[0].layers[l].elements.size(); ++e) {
                    const QRectF a = Renderer::elementBounds(before.pages[0].layers[l].elements[e]);
                    const QRectF b = Renderer::elementBounds(after.pages[0].layers[l].elements[e]);
                    QVERIFY2(a.isValid() && near(b.topLeft(), a.topLeft() + QPointF(0, 40), 1e-6) &&
                                     near(b.width(), a.width()) && near(b.height(), a.height()),
                             name);
                }
            }
            // The file can still be written and read
            Document reloaded;
            QTemporaryDir dir;
            QVERIFY(saveXopp(dir.filePath(QStringLiteral("moved.xopp")), after, nullptr));
            QVERIFY(loadXopp(dir.filePath(QStringLiteral("moved.xopp")), reloaded, nullptr));
            QCOMPARE(reloaded.pages[0].layers.size(), before.pages[0].layers.size());
        }
    }

    void laserPointer() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("laserFadeOutTime", 50);
        c->setTool(PageCanvas::LaserPen);
        c->setToolSize(PageCanvas::VeryThick);
        f.stroke(f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));

        // Visible, but not part of the document
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), QColor(Qt::red));
        QVERIFY(f.strokes().isEmpty());
        QVERIFY(!c->canUndo());
        QVERIFY(!c->modified());

        // It fades away
        QTRY_VERIFY_WITH_TIMEOUT(similar(f.pixel(f.onPage(0, QPointF(200, 100))), Qt::white), 3000);

        c->setTool(PageCanvas::LaserHighlighter);
        f.stroke(f.onPage(0, QPointF(100, 200)), f.onPage(0, QPointF(300, 200)));
        QVERIFY(!similar(f.pixel(f.onPage(0, QPointF(200, 200))), Qt::white));
        // A new document does not keep it
        c->newDocument();
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 200))), Qt::white);
    }

    void setsquare() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy geometryToolChanged(c, &PageCanvas::geometryToolChanged);
        c->setGeometryTool(PageCanvas::Setsquare);
        QCOMPARE(geometryToolChanged.count(), 1);
        QCOMPARE(c->geometryTool(), PageCanvas::Setsquare);

        // It lies in the middle of what is visible of the page, with the hypotenuse at the top
        const QRectF pageView = c->pageViewRect(0);
        const QRectF visible = QRectF(-pageView.topLeft() / c->zoom(), QSizeF(800, 600) / c->zoom()) &
                               QRectF(QPointF(0, 0), c->pageSize(0));
        const QPointF origin = visible.center();
        QImage image = f.grab();
        const QColor inside = image.pixelColor(f.onPage(0, origin + QPointF(40, 60)).toPoint());
        QVERIFY2(inside.red() < 250 && inside.red() > 200, qPrintable(inside.name()));  // transparent gray
        COMPARE_COLOR(image.pixelColor(f.onPage(0, origin + QPointF(40, -20)).toPoint()), Qt::white);

        // A stroke that starts at the hypotenuse follows it, however the pen wobbles
        f.strokeOnPage(0, {origin + QPointF(-50, 3), origin + QPointF(0, 9), origin + QPointF(60, -4)});
        QCOMPARE(f.strokes().size(), 1);
        QCOMPARE(f.strokes()[0].points.size(), 2);
        COMPARE_POINT(f.strokes()[0].points[0], origin + QPointF(-50, 0));
        COMPARE_POINT(f.strokes()[0].points[1], origin + QPointF(60, 0));
        QCOMPARE(f.strokes()[0].width, 5.67);

        // The keys move, turn and resize it: down by a quarter of a centimetre, then turned by 5°
        f.key(Qt::Key_Down);
        f.strokeOnPage(0, {origin + QPointF(-50, 10), origin + QPointF(50, 10)});
        COMPARE_POINT(f.strokes()[1].points[0], origin + QPointF(-50, 7.085));
        f.key(Qt::Key_R, Qt::ShiftModifier);
        const QPointF moved = origin + QPointF(0, 7.085);
        f.strokeOnPage(0, {moved + QPointF(-30, 1), moved + QPointF(50, 12)});
        const QLineF turned(f.strokes()[2].points[0], f.strokes()[2].points[1]);
        QVERIFY2(near(std::atan2(turned.dy(), turned.dx()), 5 * M_PI / 180, 1e-6),
                 qPrintable(describe(f.strokes()[2].points)));

        // From a leg the stroke starts in the middle of the hypotenuse
        f.key(Qt::Key_R);
        f.strokeOnPage(0, {moved + QPointF(100, 120), moved + QPointF(150, 200)});
        QCOMPARE(f.strokes()[3].points.size(), 2);
        COMPARE_POINT(f.strokes()[3].points[0], moved);
        COMPARE_POINT(f.strokes()[3].points[1], moved + QPointF(150, 200));

        // "m" marks the origin
        f.key(Qt::Key_M);
        QCOMPARE(f.strokes().size(), 5);
        QCOMPARE(f.strokes()[4].points.size(), 5);
        COMPARE_POINT(f.strokes()[4].points[2], moved);

        // Next to the tool the pen draws freely
        f.strokeOnPage(0, {moved + QPointF(-50, -30), moved + QPointF(0, -40), moved + QPointF(50, -30)});
        QCOMPARE(f.strokes()[5].points.size(), 3);

        // The hand moves the tool instead of the page
        c->setTool(PageCanvas::Hand);
        const double top = c->pageViewRect(0).top();
        f.stroke(f.onPage(0, moved + QPointF(0, 80)), f.onPage(0, moved + QPointF(30, 180)));
        QCOMPARE(c->pageViewRect(0).top(), top);
        c->setTool(PageCanvas::Pen);
        f.strokeOnPage(0, {moved + QPointF(30, 102), moved + QPointF(90, 102)});
        COMPARE_POINT(f.strokes()[6].points[0], moved + QPointF(30, 100));

        // Without the tool the pen is free again, and nothing of the tool is left on the screen
        c->setGeometryTool(PageCanvas::NoGeometryTool);
        f.strokeOnPage(0, {moved + QPointF(-50, 103), moved + QPointF(0, 109), moved + QPointF(60, 96)});
        QCOMPARE(f.strokes()[7].points.size(), 3);
        QCOMPARE(c->geometryTool(), PageCanvas::NoGeometryTool);
    }

    void compass() {
        FreeFixture f;
        PageCanvas* c = f.canvas;
        c->setGeometryTool(PageCanvas::Compass);
        const QRectF pageView = c->pageViewRect(0);
        const QPointF origin = (QRectF(-pageView.topLeft() / c->zoom(), QSizeF(800, 600) / c->zoom()) &
                                QRectF(QPointF(0, 0), c->pageSize(0)))
                                       .center();
        const double radius = 3 * GeometryTool::CM;

        // Along the outline the pen draws an arc
        QList<QPointF> drawn;
        for (int i = 0; i <= 18; ++i) {
            const double angle = i * 5 * M_PI / 180;
            drawn.append(origin + (radius - 3 - i % 3) * QPointF(std::cos(angle), std::sin(angle)));
        }
        f.strokeOnPage(0, drawn);
        QCOMPARE(f.strokes().size(), 1);
        QList<QPointF> arc = f.strokes()[0].points;
        QCOMPARE(arc.size(), 101);
        for (const QPointF& p: arc) {
            QVERIFY(near(QLineF(p, origin).length(), radius, 1e-6));
        }
        COMPARE_POINT(arc.first(), origin + QPointF(radius, 0));
        COMPARE_POINT(arc.last(), origin + QPointF(0, radius));
        // The outline of the tool lies on top of the arc
        QVERIFY(similar(f.pixel(f.onPage(0, origin + radius * QPointF(std::cos(0.7), std::sin(0.7)))), PEN_COLOR, 90));

        // With filling the arc becomes a sector
        c->setFill(true);
        f.strokeOnPage(0, drawn);
        arc = f.strokes()[1].points;
        QCOMPARE(arc.size(), 103);
        COMPARE_POINT(arc.first(), origin);
        COMPARE_POINT(arc.last(), origin);
        c->setFill(false);

        // Along the marked radius: a straight line from the center
        f.strokeOnPage(0, {origin + QPointF(10, 2), origin + QPointF(40, -5), origin + QPointF(70, 6)});
        QCOMPARE(f.strokes()[2].points.size(), 2);
        COMPARE_POINT(f.strokes()[2].points[0], origin + QPointF(std::hypot(10, 2), 0));
        COMPARE_POINT(f.strokes()[2].points[1], origin + QPointF(std::hypot(70, 6), 0));

        // In the middle of the tool the pen is free
        f.strokeOnPage(0, {origin + QPointF(-30, -40), origin + QPointF(-10, -45), origin + QPointF(10, -40)});
        QCOMPARE(f.strokes()[3].points.size(), 3);

        // "s" makes it larger by a tenth
        f.key(Qt::Key_S);
        f.strokeOnPage(0, {origin + QPointF(radius * 1.1 - 3, 3), origin + QPointF(radius * 1.1 - 5, 20)});
        QVERIFY(near(QLineF(f.strokes()[4].points.last(), origin).length(), radius * 1.1, 1e-6));

        // A change of the pages takes the tool away
        c->insertPage(0);
        QCOMPARE(c->geometryTool(), PageCanvas::NoGeometryTool);
    }

    void colorPalette() {
        ColorPalette palette;
        QSignalSpy changed(&palette, &ColorPalette::changed);
        QSignalSpy failed(&palette, &ColorPalette::loadFailed);
        QCOMPARE(palette.colors().size(), 11);
        QCOMPARE(palette.name(), QStringLiteral("Xournal++ Palette"));
        QCOMPARE(palette.colors()[0].toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Yellow"));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("two.gpl"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("GIMP Palette\nName: Two\n#\n255 0 0 Signal red\n0 0 255 Blue\n");
        file.close();
        QVERIFY(palette.load(QUrl::fromLocalFile(path)));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(palette.name(), QStringLiteral("Two"));
        QCOMPARE(palette.colors().size(), 2);
        QCOMPARE(palette.colors()[0].toMap().value(QStringLiteral("color")).value<QColor>(), QColor(Qt::red));

        // A file that is not a palette leaves the colours as they are
        QVERIFY(!palette.load(dataFile(QStringLiteral("strokes.xopp"))));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains(QStringLiteral("GIMP Palette")));
        QCOMPARE(palette.colors().size(), 2);

        palette.reset();
        QCOMPARE(palette.colors().size(), 11);
    }
};

QTEST_MAIN(TestDrawing)
#include "tst_drawing.moc"
