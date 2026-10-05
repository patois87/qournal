/*
 * Qournal
 *
 * Tests for the algorithms of the drawing tools: eraser, stroke builder and stabilizer, snapping, shapes,
 * shape recognizer and palettes. The test vectors of the eraser come from the unit tests of Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>

#include <QTest>

#include "Document.h"
#include "Eraser.h"
#include "Palette.h"
#include "ShapeRecognizer.h"
#include "Shapes.h"
#include "Snapping.h"
#include "SplineSegment.h"
#include "StrokeBuilder.h"
#include "StrokeStabilizer.h"

namespace {

using Eraser::PathParameter;

/// The stroke of the eraser tests of Xournal++; the widths belong to the segments starting at the points
Stroke testStroke() {
    Stroke s;
    s.points = {{0, 0}, {2, 2}, {5, 2}, {7, 4}, {3, 6}, {2, 8}, {5, 11}, {7, 10}, {7, 6}, {6, 7}, {4, 4}, {1, 3}};
    s.widths = {2, 2.5, 3, 2.5, 2, 1.5, 1, 1.5, 2, 2.5, 3};
    s.width = 3;
    s.updateBounds();
    return s;
}

Stroke closedStroke() {
    Stroke s;
    s.points = {{0, 0}, {2, 2}, {5, 2}, {1, 4}, {3, 6}, {0, 0}};
    s.widths = {2, 2.5, 3, 2.5, 2};
    s.width = 3;
    s.updateBounds();
    return s;
}

bool near(double a, double b, double tolerance = 1e-9) { return std::abs(a - b) <= tolerance; }
bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-9) {
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
            const QPointF p = a + (b - a) * (d / length);
            points.append(p + QPointF(wobble * std::sin(n * 1.7), wobble * std::cos(n * 2.3)));
        }
    }
    points.append(corners.last());
    return points;
}

Stroke strokeOf(const QList<QPointF>& points) {
    Stroke s;
    s.points = points;
    s.updateBounds();
    return s;
}

const SnapSettings NO_SNAPPING{false, false};

}  // namespace

class TestTools: public QObject {
    Q_OBJECT

private slots:
    // Eraser

    void eraserIntersections_data() {
        QTest::addColumn<QPointF>("center");
        QTest::addColumn<double>("halfSize");
        QTest::addColumn<double>("padded");
        QTest::addColumn<QList<double>>("expected");  // index, t, index, t, ...

        QTest::newRow("start in padding, pass the box, end in padding going away")
                << QPointF(1, 1) << 0.5 << 3.0 << QList<double>{0, 0.0, 1, 2.0 / 3.0};
        QTest::newRow("start on the boundary of the box")
                << QPointF(1, 1) << 1.0 << 3.0 << QList<double>{0, 0.0, 1, 2.0 / 3.0};
        QTest::newRow("end in padding going towards the box")
                << QPointF(1, 1) << 1.9 << 3.5 << QList<double>{0, 0.0, 1, 5.0 / 6.0, 9, 5.0 / 6.0, 10, 1.0};
        QTest::newRow("start on the boundary of the padding")
                << QPointF(1, 1) << 0.5 << 1.0 << QList<double>{0, 0.0, 0, 1.0};
        QTest::newRow("through the padding only") << QPointF(2, 11) << 1.0 << 2.0 << QList<double>{};
        QTest::newRow("through padding and box") << QPointF(2, 8) << 1.0 << 4.0 << QList<double>{3, 0.25, 6, 0.5};
        QTest::newRow("through the box, end on the boundary of the padding")
                << QPointF(2.5, 3.5) << 0.5 << 2.0 << QList<double>{9, 0.75, 10, 1.0};
        QTest::newRow("twice") << QPointF(6, 6) << 1.1 << 2.0 << QList<double>{2, 1.0, 3, 0.75, 7, 0.5, 9, 1.0};
        QTest::newRow("across a corner of the stroke")
                << QPointF(7, 7.5) << 1.0 << 1.5 << QList<double>{7, 0.25, 9, 0.25};
        QTest::newRow("next to the stroke") << QPointF(6, 8) << 0.5 << 1.0 << QList<double>{};
    }

    void eraserIntersections() {
        QFETCH(QPointF, center);
        QFETCH(double, halfSize);
        QFETCH(double, padded);
        QFETCH(QList<double>, expected);

        const auto result = Eraser::intersectWithPaddedBox(testStroke(), center, halfSize, padded);
        QStringList found;
        for (const PathParameter& p: result) {
            found.append(QStringLiteral("(%1, %2)").arg(p.index).arg(p.t));
        }
        QVERIFY2(static_cast<qsizetype>(result.size()) * 2 == expected.size(), qPrintable(found.join(u' ')));
        for (size_t i = 0; i < result.size(); ++i) {
            QVERIFY2(result[i].index == static_cast<int>(expected[static_cast<qsizetype>(2 * i)]) &&
                             near(result[i].t, expected[static_cast<qsizetype>(2 * i + 1)], 1e-12),
                     qPrintable(found.join(u' ')));
        }
    }

    void eraserSplitsStroke() {
        const Stroke stroke = testStroke();
        const auto pieces = Eraser::split(stroke, {{1, 0.5}, {2, 0.5}, {3, 0.5}, {4, 0.5}});
        QCOMPARE(pieces.size(), size_t(3));

        QCOMPARE(pieces[0].points, (QList<QPointF>{{0, 0}, {2, 2}, {3.5, 2}}));
        QCOMPARE(pieces[0].widths, (QList<double>{2, 2.5}));
        QCOMPARE(pieces[1].points, (QList<QPointF>{{6, 3}, {7, 4}, {5, 5}}));
        QCOMPARE(pieces[1].widths, (QList<double>{3, 2.5}));
        QCOMPARE(pieces[2].points,
                 (QList<QPointF>{{2.5, 7}, {2, 8}, {5, 11}, {7, 10}, {7, 6}, {6, 7}, {4, 4}, {1, 3}}));
        QCOMPARE(pieces[2].widths, (QList<double>{2, 1.5, 1, 1.5, 2, 2.5, 3}));
        for (const Stroke& piece: pieces) {
            QCOMPARE(piece.width, 3.0);
            QVERIFY(piece.hasPressure());
            QVERIFY(piece.bounds.isValid());
        }
    }

    void eraserKeepsClosedStrokeInOnePiece() {
        Stroke stroke = closedStroke();
        stroke.fill = 123;
        stroke.audio.filename = QStringLiteral("assets/bar.mp3");

        // The pieces at the end and at the beginning are joined: the stroke is only opened
        auto pieces = Eraser::split(stroke, {{1, 0.5}, {2, 0.5}, {3, 0.5}, {4, 0.5}});
        QCOMPARE(pieces.size(), size_t(2));
        QCOMPARE(pieces[0].points, (QList<QPointF>{{1.5, 3}, {0, 0}, {2, 2}, {3.5, 2}}));
        QCOMPARE(pieces[0].widths, (QList<double>{2, 2, 2.5}));
        QCOMPARE(pieces[1].points, (QList<QPointF>{{3, 3}, {1, 4}, {2, 5}}));
        QCOMPARE(pieces[1].widths, (QList<double>{3, 2.5}));
        QCOMPARE(pieces[0].fill, 123);
        QCOMPARE(pieces[1].audio.filename, QStringLiteral("assets/bar.mp3"));

        // Erased at both ends: nothing to join
        stroke.tool = Stroke::Tool::Highlighter;
        pieces = Eraser::split(stroke, {{0, 0.0}, {2, 0.5}, {3, 0.5}, {4, 1.0}});
        QCOMPARE(pieces.size(), size_t(1));
        QCOMPARE(pieces[0].points, (QList<QPointF>{{3, 3}, {1, 4}, {2, 5}}));
        QCOMPARE(pieces[0].tool, Stroke::Tool::Highlighter);
    }

    void eraserOnSimpleStrokes() {
        Stroke line = strokeOf({{0, 0}, {100, 0}});
        line.width = 2;
        line.updateBounds();

        // In the middle: two pieces, shortened by the eraser and its padding (0.4 * width for round caps)
        auto pieces = Eraser::erase(line, QPointF(50, 0), 5);
        QVERIFY(pieces.has_value());
        QCOMPARE(pieces->size(), size_t(2));
        QVERIFY(near((*pieces)[0].points.last(), QPointF(50 - 5.8, 0)));
        QVERIFY(near((*pieces)[1].points.first(), QPointF(50 + 5.8, 0)));
        QVERIFY(!(*pieces)[0].hasPressure());

        // At the end: one piece
        pieces = Eraser::erase(line, QPointF(98, 1), 5);
        QVERIFY(pieces.has_value());
        QCOMPARE(pieces->size(), size_t(1));
        QCOMPARE((*pieces)[0].points.first(), QPointF(0, 0));

        // Not touched
        QVERIFY(!Eraser::erase(line, QPointF(50, 20), 5).has_value());
        QVERIFY(!Eraser::erase(line, QPointF(500, 0), 5).has_value());

        // Completely covered
        pieces = Eraser::erase(strokeOf({{10, 10}, {12, 11}, {11, 12}}), QPointF(11, 11), 5);
        QVERIFY(pieces.has_value());
        QVERIFY(pieces->empty());

        // A dot
        const Stroke dot = strokeOf({{10, 10}, {10, 10}});
        pieces = Eraser::erase(dot, QPointF(11, 11), 5);
        QVERIFY(pieces.has_value());
        QVERIFY(pieces->empty());
        QVERIFY(!Eraser::erase(dot, QPointF(30, 30), 5).has_value());
    }

    void eraserDeletingStrokes() {
        const Stroke line = strokeOf({{0, 0}, {100, 100}});
        QVERIFY(Eraser::intersects(line, QPointF(50, 50), 3));
        QVERIFY(Eraser::intersects(line, QPointF(51, 49), 3));
        QVERIFY(Eraser::intersects(line, QPointF(1, 1), 3));
        QVERIFY(!Eraser::intersects(line, QPointF(60, 40), 3));
        QVERIFY(!Eraser::intersects(line, QPointF(120, 120), 3));
        QVERIFY(!Eraser::intersects(Stroke(), QPointF(0, 0), 3));
    }

    // Stroke builder and stabilizer

    void builderDropsSmallMovements() {
        Stroke style;
        style.width = 2;
        StrokeBuilder builder;
        builder.begin(style, QPointF(10, 10), StrokeBuilder::NO_PRESSURE);
        QVERIFY(builder.active());
        builder.paintTo(QPointF(10.1, 10.1), StrokeBuilder::NO_PRESSURE);
        QCOMPARE(builder.pointCount(), 1);
        builder.paintTo(QPointF(20, 10), StrokeBuilder::NO_PRESSURE);
        builder.paintTo(QPointF(20, 30), StrokeBuilder::NO_PRESSURE);
        QCOMPARE(builder.pointCount(), 3);
        QVERIFY(builder.takeDirtyRect().contains(QRectF(10, 10, 10, 20)));
        QVERIFY(builder.takeDirtyRect().isNull());

        const Stroke stroke = builder.finish(StrokeBuilder::NO_PRESSURE);
        QVERIFY(!builder.active());
        QVERIFY(!stroke.hasPressure());
        QCOMPARE(stroke.points, (QList<QPointF>{{10, 10}, {20, 10}, {20, 30}}));
        QCOMPARE(stroke.width, 2.0);
        QVERIFY(stroke.bounds.contains(QPointF(20, 30)));
    }

    void builderWithPressure() {
        Stroke style;
        style.width = 4;
        StrokeBuilder builder;
        builder.begin(style, QPointF(0, 0), 0.5);
        QVERIFY(builder.hasPressure());
        builder.paintTo(QPointF(10, 0), 0.5);
        // The width of a segment is the pressure at its start times the width of the tool
        QCOMPARE(builder.stroke().widths, (QList<double>{2.0}));

        // A jump of the pressure is spread over several segments. As in Xournal++, the first step is twice as large
        builder.paintTo(QPointF(20, 0), 1.0);
        const Stroke stroke = builder.finish(1.0);
        QVERIFY(stroke.hasPressure());
        QCOMPARE(stroke.points.size(), 9);
        QCOMPARE(stroke.points.last(), QPointF(20, 0));
        for (qsizetype i = 0; i + 1 < stroke.widths.size(); ++i) {
            QVERIFY(stroke.widths[i + 1] - stroke.widths[i] <= 2 * StrokeBuilder::MAX_WIDTH_VARIATION);
            QVERIFY(stroke.widths[i + 1] >= stroke.widths[i]);
            QVERIFY(stroke.points[i + 1].x() > stroke.points[i].x());
        }
        QVERIFY(near(stroke.widths.last(), 4.0));
    }

    void builderMakesDots() {
        Stroke style;
        style.width = 4;
        StrokeBuilder builder;
        builder.begin(style, QPointF(5, 5), 0.25);
        builder.paintTo(QPointF(5, 5.1), 0.75);  // pressing harder on the spot
        Stroke dot = builder.finish(0.5);
        QCOMPARE(dot.points, (QList<QPointF>{{5, 5}, {5, 5}}));
        QCOMPARE(dot.widths, (QList<double>{3.0}));

        builder.begin(style, QPointF(5, 5), StrokeBuilder::NO_PRESSURE);
        dot = builder.finish(StrokeBuilder::NO_PRESSURE);
        QCOMPARE(dot.points.size(), 2);
        QVERIFY(dot.widths.isEmpty());

        builder.begin(style, QPointF(5, 5), StrokeBuilder::NO_PRESSURE);
        builder.cancel();
        QVERIFY(!builder.active());
        builder.paintTo(QPointF(50, 50), StrokeBuilder::NO_PRESSURE);
        QCOMPARE(builder.pointCount(), 0);
    }

    void stabilizerOff() {
        StrokeBuilder builder;
        builder.begin(Stroke(), QPointF(0, 0), StrokeBuilder::NO_PRESSURE);
        StrokeStabilizer stabilizer(StabilizerSettings(), builder, 2.0, {0, 0, 0}, 0);
        stabilizer.processEvent({20, 0, 0}, 10);
        stabilizer.processEvent({20, 40, 0}, 20);
        stabilizer.finalizeStroke();
        // Positions are in pixels: divided by the zoom
        QCOMPARE(builder.stroke().points, (QList<QPointF>{{0, 0}, {10, 0}, {10, 20}}));
    }

    void stabilizerSmoothsJitter_data() {
        QTest::addColumn<int>("averaging");
        QTest::addColumn<int>("preprocessor");
        using A = StabilizerSettings::Averaging;
        using P = StabilizerSettings::Preprocessor;
        QTest::newRow("deadzone") << int(A::None) << int(P::Deadzone);
        QTest::newRow("inertia") << int(A::None) << int(P::Inertia);
        QTest::newRow("arithmetic") << int(A::Arithmetic) << int(P::None);
        QTest::newRow("gaussian") << int(A::VelocityGaussian) << int(P::None);
        QTest::newRow("arithmetic + deadzone") << int(A::Arithmetic) << int(P::Deadzone);
        QTest::newRow("arithmetic + inertia") << int(A::Arithmetic) << int(P::Inertia);
        QTest::newRow("gaussian + deadzone") << int(A::VelocityGaussian) << int(P::Deadzone);
        QTest::newRow("gaussian + inertia") << int(A::VelocityGaussian) << int(P::Inertia);
    }

    /// A horizontal line drawn with a shaky hand gets straighter, and still ends where the pen was lifted
    void stabilizerSmoothsJitter() {
        QFETCH(int, averaging);
        QFETCH(int, preprocessor);
        StabilizerSettings settings;
        settings.averaging = static_cast<StabilizerSettings::Averaging>(averaging);
        settings.preprocessor = static_cast<StabilizerSettings::Preprocessor>(preprocessor);
        settings.bufferSize = 6;
        settings.deadzoneRadius = 4;
        settings.sigma = 2;

        auto roughness = [](const Stroke& stroke) {
            double sum = 0;
            for (qsizetype i = 1; i < stroke.points.size(); ++i) {
                sum += std::abs(stroke.points[i].y() - stroke.points[i - 1].y());
            }
            return sum;
        };
        auto draw = [&](const StabilizerSettings& s) {
            Stroke style;
            style.width = 2;
            StrokeBuilder builder;
            builder.begin(style, QPointF(0, 100), 0.5);
            StrokeStabilizer stabilizer(s, builder, 1.0, {0, 100, 0.5}, 0);
            for (int i = 1; i <= 100; ++i) {
                stabilizer.processEvent({3.0 * i, 100 + (i % 2 == 0 ? 1.5 : -1.5), 0.5}, quint64(i) * 5);
            }
            stabilizer.finalizeStroke();
            return builder.finish(0.5);
        };

        const Stroke raw = draw(StabilizerSettings());
        const Stroke smooth = draw(settings);
        QVERIFY(smooth.points.size() > 20);
        QVERIFY2(roughness(smooth) < 0.5 * roughness(raw),
                 qPrintable(QStringLiteral("%1 instead of %2").arg(roughness(smooth)).arg(roughness(raw))));
        // The end of the stroke is drawn up to the last position of the pen
        QVERIFY2(near(smooth.points.last(), QPointF(300, 101.5), 1e-6), qPrintable(describe({smooth.points.last()})));
        for (const QPointF& p: smooth.points) {
            QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()));
            QVERIFY(p.y() > 95 && p.y() < 105 && p.x() >= -1 && p.x() <= 301);
        }
        QVERIFY(smooth.hasPressure());
    }

    void stabilizerWithoutFinalizing() {
        StabilizerSettings settings;
        settings.preprocessor = StabilizerSettings::Preprocessor::Deadzone;
        settings.deadzoneRadius = 10;
        settings.finalizeStroke = false;

        StrokeBuilder builder;
        builder.begin(Stroke(), QPointF(0, 0), StrokeBuilder::NO_PRESSURE);
        StrokeStabilizer stabilizer(settings, builder, 1.0, {0, 0, 0}, 0);
        stabilizer.processEvent({5, 0, 0}, 10);  // inside the deadzone
        QCOMPARE(builder.pointCount(), 1);
        stabilizer.processEvent({50, 0, 0}, 20);
        stabilizer.finalizeStroke();
        // The stroke follows the pen at the distance of the radius
        QVERIFY(near(builder.stroke().points.last(), QPointF(40, 0)));
    }

    void deadzoneKeepsCusps() {
        StabilizerSettings settings;
        settings.preprocessor = StabilizerSettings::Preprocessor::Deadzone;
        settings.deadzoneRadius = 5;

        StrokeBuilder builder;
        builder.begin(Stroke(), QPointF(0, 0), StrokeBuilder::NO_PRESSURE);
        StrokeStabilizer stabilizer(settings, builder, 1.0, {0, 0, 0}, 0);
        for (int i = 1; i <= 20; ++i) {
            stabilizer.processEvent({5.0 * i, 0, 0}, quint64(i));
        }
        for (int i = 19; i >= 0; --i) {  // and straight back
            stabilizer.processEvent({5.0 * i, 0, 0}, quint64(40 - i));
        }
        stabilizer.finalizeStroke();

        // The tip of the cusp is reached although the deadzone would cut it off
        double maxX = 0;
        for (const QPointF& p: builder.stroke().points) {
            maxX = std::max(maxX, p.x());
        }
        QVERIFY(near(maxX, 100.0, 1e-6));
        QVERIFY(near(builder.stroke().points.last(), QPointF(0, 0), 1e-6));
    }

    // Snapping and shapes

    void snapping() {
        QCOMPARE(Snapping::snapToGrid(QPointF(15, 29), 14, 14, 0.5, 0, 0), QPointF(14, 28));
        QCOMPARE(Snapping::snapToGrid(QPointF(21, 21), 14, 14, 0.5, 0, 0), QPointF(21, 21));  // too far from a vertex
        QCOMPARE(Snapping::snapToGrid(QPointF(16, 17), 14, 14, 0.5, 3, 3), QPointF(17, 17));
        QCOMPARE(Snapping::snapVertically(81, 10, 0.5, 0), 80.0);
        QCOMPARE(Snapping::snapVertically(84, 10, 0.5, 0), 84.0);
        QCOMPARE(Snapping::snapHorizontally(79, 10, 0.5, 0), 80.0);

        // Multiples of 15°
        QVERIFY(near(Snapping::snapAngle(0.02, 0.3), 0.0));
        QVERIFY(near(Snapping::snapAngle(M_PI / 4 + 0.03, 0.3), M_PI / 4));
        QVERIFY(near(Snapping::snapAngle(0.13, 0.3), 0.13));
        QVERIFY(near(Snapping::snapRotation(QPointF(100, 2), QPointF(0, 0), 0.3), QPointF(std::hypot(100, 2), 0)));

        QVERIFY(near(Snapping::projToLine(QPointF(5, 7), QPointF(0, 0), QPointF(10, 0)), QPointF(5, 0)));
        QVERIFY(near(Snapping::distanceLine(QPointF(5, 7), QPointF(0, 0), QPointF(10, 0)), 7.0));
        QVERIFY(near(Snapping::distanceLine(QPointF(13, 4), QPointF(0, 0), QPointF(10, 0)), 5.0));
    }

    void snapperFollowsTheRuling() {
        Page page;
        const SnapSettings settings;
        // Plain paper: the grid of 5 mm from the corner of the page
        QVERIFY(near(Snapper(settings, page).snapToGrid(QPointF(15, 29)), QPointF(14.17, 28.34)));
        // Alt toggles snapping
        QCOMPARE(Snapper(settings, page).snapToGrid(QPointF(15, 29), true), QPointF(15, 29));
        QCOMPARE(Snapper(NO_SNAPPING, page).snapToGrid(QPointF(15, 29)), QPointF(15, 29));
        QVERIFY(near(Snapper(NO_SNAPPING, page).snapToGrid(QPointF(15, 29), true), QPointF(14.17, 28.34)));

        // Ruled paper: the lines start at 80 pt
        page.background.style = QStringLiteral("ruled");
        QVERIFY(near(Snapper(settings, page).snapVertically(81), 80.0));
        QVERIFY(near(Snapper(settings, page).snapVertically(80 + 14.17 + 1), 80 + 14.17));

        // Isometric paper: the vertices of the triangles, centered on the page
        page.background.style = QStringLiteral("isograph");
        page.background.config = QStringLiteral("r1=20");
        const double xstep = std::sqrt(3.0) / 2 * 20;
        const int cols = static_cast<int>((page.width - 40) / xstep);
        const double xOffset = (page.width - cols * xstep) / 2;
        const int rows = static_cast<int>((page.height - 40) / 10);
        const double yOffset = (page.height - rows * 10) / 2;
        const QPointF vertex(xOffset + 3 * xstep, yOffset + 50);
        QVERIFY(near(Snapper(settings, page).snapToGrid(vertex + QPointF(1.5, -2)), vertex));

        // Lines snap to multiples of 15° first, then to the grid
        page.background = Background();
        const Snapper snapper(SnapSettings{false, true}, page);
        QVERIFY(near(snapper.snap(QPointF(100, 3), QPointF(0, 0)), QPointF(std::hypot(100, 3), 0)));
        QVERIFY(near(snapper.snapAngle(M_PI / 2 - 0.02), M_PI / 2));
        QCOMPARE(snapper.snapAngle(M_PI / 2 - 0.02, true), M_PI / 2 - 0.02);
    }

    void rectangleShape() {
        const Snapper snapper(NO_SNAPPING, Page());
        using namespace Shapes;
        QCOMPARE(rectangle({10, 10}, {40, 30}, snapper, {}),
                 (QList<QPointF>{{10, 10}, {10, 30}, {40, 30}, {40, 10}, {10, 10}}));
        // Shift: a square with the larger side, also to the top left
        QCOMPARE(rectangle({10, 10}, {40, 30}, snapper, {false, true, false}),
                 (QList<QPointF>{{10, 10}, {10, 40}, {40, 40}, {40, 10}, {10, 10}}));
        QCOMPARE(rectangle({10, 10}, {0, -30}, snapper, {false, true, false}).at(2), QPointF(-30, -30));
        // Control: from the center
        QCOMPARE(rectangle({10, 10}, {40, 30}, snapper, {false, false, true}),
                 (QList<QPointF>{{-20, -10}, {-20, 30}, {40, 30}, {40, -10}, {-20, -10}}));
        // With snapping the corner is on the grid
        const Snapper grid{SnapSettings(), Page()};
        QVERIFY(near(rectangle({0, 0}, {29, 41}, grid, {}).at(2), QPointF(28.34, 42.51)));
        QCOMPARE(create(Type::Rectangle, {10, 10}, {40, 30}, snapper, {}, 1),
                 rectangle({10, 10}, {40, 30}, snapper, {}));
    }

    void ellipseShape() {
        const Snapper snapper(NO_SNAPPING, Page());
        using namespace Shapes;
        const QList<QPointF> points = ellipse({0, 0}, {200, 100}, snapper, {});
        // Closed, through the ends of the axes, and every point on the ellipse
        QCOMPARE(points.first(), QPointF(200, 50));
        QVERIFY(near(points.last(), points.first()));
        QVERIFY(points.size() > 100);
        QVERIFY(points.contains(QPointF(100, 100)));
        for (const QPointF& p: points) {
            QVERIFY(near(std::pow((p.x() - 100) / 100, 2) + std::pow((p.y() - 50) / 50, 2), 1.0, 1e-9));
        }

        // Shift: a circle; with Control around the starting point through the pointer
        for (const QPointF& p: ellipse({0, 0}, {30, 40}, snapper, {false, true, false})) {
            QVERIFY(near(std::hypot(p.x() - 20, p.y() - 20), 20.0, 1e-9));
        }
        for (const QPointF& p: ellipse({10, 10}, {40, 50}, snapper, {false, true, true})) {
            QVERIFY(near(std::hypot(p.x() - 10, p.y() - 10), 50.0, 1e-9));
        }
        // Small ellipses have fewer points
        QVERIFY(ellipse({0, 0}, {10, 10}, snapper, {}).size() < 40);
    }

    void lineAndArrowShapes() {
        const Snapper snapper(NO_SNAPPING, Page());
        using namespace Shapes;
        QCOMPARE(line({10, 10}, {50, 12}, snapper, {}), (QList<QPointF>{{10, 10}, {50, 12}}));
        const Snapper rotation(SnapSettings{false, true}, Page());
        QVERIFY(near(line({10, 10}, {50, 11}, rotation, {}).last(), QPointF(10 + std::hypot(40, 1), 10)));

        // A long arrow: the legs are 7 times the thickness long, 30° from the line
        QList<QPointF> points = arrow({0, 0}, {100, 0}, snapper, {}, 2, false);
        QCOMPARE(points.size(), 6);
        QCOMPARE(points[1], QPointF(100, 0));
        QVERIFY(near(points[2], QPointF(100 - 14 * std::cos(M_PI / 6), -14 * std::sin(M_PI / 6))));
        QVERIFY(near(points[4], QPointF(100 - 14 * std::cos(M_PI / 6), 14 * std::sin(M_PI / 6))));
        QCOMPARE(points.last(), QPointF(100, 0));

        // A short arrow: the head shrinks with the line
        points = arrow({0, 0}, {20, 0}, snapper, {}, 2, false);
        QVERIFY(near(std::hypot(points[2].x() - 20, points[2].y()), 8.0));

        // Both ends
        points = arrow({0, 0}, {100, 0}, snapper, {}, 2, true);
        QCOMPARE(points.size(), 10);
        QCOMPARE(points.first(), QPointF(0, 0));
        QVERIFY(near(points[1], QPointF(14 * std::cos(M_PI / 6), 14 * std::sin(M_PI / 6))));
        QCOMPARE(points[5], QPointF(100, 0));

        // Tiny arrows stay finite
        for (const QPointF& p: arrow({0, 0}, {0.5, 0.2}, snapper, {}, 5, true)) {
            QVERIFY(std::isfinite(p.x()) && std::isfinite(p.y()) && std::abs(p.x()) < 2 && std::abs(p.y()) < 2);
        }
    }

    void coordinateSystemShape() {
        const Snapper snapper(NO_SNAPPING, Page());
        using namespace Shapes;
        QCOMPARE(coordinateSystem({10, 10}, {60, 40}, snapper, {}), (QList<QPointF>{{10, 10}, {10, 40}, {60, 40}}));
        QCOMPARE(coordinateSystem({10, 10}, {60, 40}, snapper, {false, false, true}),
                 (QList<QPointF>{{10, 40}, {10, 10}, {60, 10}}));
        QCOMPARE(coordinateSystem({10, 10}, {60, 40}, snapper, {false, true, false}),
                 (QList<QPointF>{{10, 10}, {10, 60}, {60, 60}}));
    }

    void splineShape() {
        using namespace Shapes;
        // Without tangents: straight lines between the knots
        QCOMPARE(spline({{0, 0}, {100, 0}, {100, 50}}, {{0, 0}, {0, 0}, {0, 0}}),
                 (QList<QPointF>{{0, 0}, {100, 0}, {100, 50}}));
        QVERIFY(spline({}, {}).isEmpty());
        QCOMPARE(spline({{5, 5}}, {{1, 1}}), (QList<QPointF>{{5, 5}}));

        // With tangents: a smooth curve that leaves the first knot in the direction of its tangent
        const QList<QPointF> points = spline({{0, 0}, {100, 0}}, {{0, 50}, {0, -50}});
        QVERIFY(points.size() > 10);
        QCOMPARE(points.first(), QPointF(0, 0));
        QCOMPARE(points.last(), QPointF(100, 0));
        QVERIFY(points[1].y() > 0 && points[1].x() < points[1].y());
        double maxY = 0;
        for (qsizetype i = 1; i < points.size(); ++i) {
            maxY = std::max(maxY, points[i].y());
            QVERIFY(points[i].x() > points[i - 1].x());
            // No visible corners: the direction changes by small steps
            if (i >= 2) {
                const QLineF a(points[i - 2], points[i - 1]);
                const QLineF b(points[i - 1], points[i]);
                const double turn = std::abs(std::remainder(a.angle() - b.angle(), 360.0));
                QVERIFY2(turn < 15, qPrintable(QString::number(turn)));
            }
        }
        QVERIFY(near(maxY, 37.5, 0.5));  // the top of this Bézier curve
    }

    void splineSegmentWithPressure() {
        const SplineSegment segment{{0, 0, 1.0}, {10, 0}, {20, 0}, {30, 0, 3.0}};
        const auto points = segment.toPointSequence(true);
        // Straight, but subdivided until the width changes by at most 0.1 per point
        QVERIFY(points.size() >= 16);
        for (size_t i = 1; i < points.size(); ++i) {
            QVERIFY(std::abs(points[i].z - points[i - 1].z) <= 0.1 + 1e-9);
            QVERIFY(points[i].z > points[i - 1].z);
        }
        QCOMPARE(segment.toPointSequence(false).size(), size_t(1));
    }

    // Shape recognizer

    void recognizesLines() {
        // Nearly horizontal: made horizontal
        auto shape = ShapeRecognizer::recognize(strokeOf(handDrawn({{10, 100}, {200, 104}}, 4, 0.6)));
        QVERIFY(shape.has_value());
        QCOMPARE(shape->points.size(), 2);
        QCOMPARE(shape->points[0].y(), shape->points[1].y());
        QVERIFY(near(shape->points[0].y(), 102, 1.5));
        QVERIFY(near(std::abs(shape->points[1].x() - shape->points[0].x()), 190, 3));

        // Nearly vertical
        shape = ShapeRecognizer::recognize(strokeOf(handDrawn({{50, 10}, {53, 180}}, 4, 0.6)));
        QVERIFY(shape.has_value());
        QCOMPARE(shape->points[0].x(), shape->points[1].x());

        // Slanted: stays slanted
        shape = ShapeRecognizer::recognize(strokeOf(handDrawn({{10, 10}, {110, 90}}, 4, 0.6)));
        QVERIFY(shape.has_value());
        QCOMPARE(shape->points.size(), 2);
        const QLineF line(shape->points[0], shape->points[1]);
        QVERIFY(near(line.length(), std::hypot(100, 80), 3));
        QVERIFY(near(std::abs(line.dy() / line.dx()), 0.8, 0.03));
    }

    void recognizesRectangles() {
        Stroke stroke = strokeOf(handDrawn({{20, 20}, {182, 23}, {180, 121}, {18, 118}, {21, 22}}, 4, 0.7));
        stroke.color = Qt::red;
        stroke.widths.fill(1.0, stroke.points.size() - 1);
        stroke.widths[0] = 3.0;
        const auto shape = ShapeRecognizer::recognize(stroke);
        QVERIFY(shape.has_value());
        QCOMPARE(shape->points.size(), 5);
        QVERIFY(near(shape->points.first(), shape->points.last()));

        // Upright, with the size of the drawing
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (const QPointF& p: shape->points) {
            minX = std::min(minX, p.x());
            maxX = std::max(maxX, p.x());
            minY = std::min(minY, p.y());
            maxY = std::max(maxY, p.y());
        }
        for (const QPointF& p: shape->points) {
            QVERIFY2((near(p.x(), minX, 1e-6) || near(p.x(), maxX, 1e-6)) &&
                             (near(p.y(), minY, 1e-6) || near(p.y(), maxY, 1e-6)),
                     qPrintable(describe(shape->points)));
        }
        QVERIFY(near(maxX - minX, 162, 5));
        QVERIFY(near(maxY - minY, 98, 5));

        // The style is kept, the width is the average of the stroke
        QCOMPARE(shape->color, QColor(Qt::red));
        QVERIFY(!shape->hasPressure());
        QVERIFY(shape->width > 1.0 && shape->width < 1.1);
        QVERIFY(shape->bounds.contains(QPointF(100, 70)));
    }

    void recognizesTriangles() {
        const QList<QPointF> drawn = handDrawn({{100, 20}, {180, 150}, {30, 140}, {99, 22}}, 4, 0.7);
        const auto shape = ShapeRecognizer::recognize(strokeOf(drawn));
        QVERIFY(shape.has_value());
        QCOMPARE(shape->points.size(), 4);
        QVERIFY(near(shape->points.first(), shape->points.last()));
        // Each corner of the drawing is near a vertex
        for (const QPointF& corner: {QPointF(100, 20), QPointF(180, 150), QPointF(30, 140)}) {
            bool found = false;
            for (const QPointF& p: shape->points) {
                found |= QLineF(p, corner).length() < 6;
            }
            QVERIFY2(found, qPrintable(describe(shape->points)));
        }
    }

    void recognizesCircles() {
        QList<QPointF> points;
        for (int i = 0; i <= 60; ++i) {
            const double angle = 2 * M_PI * i / 58;  // overlaps a little at the end
            const double radius = 50 + 2 * std::sin(5 * angle);
            points.append(QPointF(200 + radius * std::cos(angle), 150 + radius * std::sin(angle)));
        }
        const auto shape = ShapeRecognizer::recognize(strokeOf(points));
        QVERIFY(shape.has_value());
        QVERIFY(shape->points.size() >= 25);
        QVERIFY(near(shape->points.first(), shape->points.last(), 1e-6));
        for (const QPointF& p: shape->points) {
            QVERIFY(near(std::hypot(p.x() - 200, p.y() - 150), 50, 3));
        }
    }

    void leavesOtherStrokesAlone() {
        // Too small
        QVERIFY(!ShapeRecognizer::recognize(strokeOf(handDrawn({{10, 10}, {30, 12}}, 2, 0.3))).has_value());
        QVERIFY(!ShapeRecognizer::recognize(strokeOf({{10, 10}, {300, 10}})).has_value());
        QVERIFY(!ShapeRecognizer::recognize(Stroke()).has_value());

        // A wave
        QList<QPointF> wave;
        for (int i = 0; i <= 100; ++i) {
            wave.append(QPointF(10 + 3 * i, 100 + 40 * std::sin(i / 8.0)));
        }
        QVERIFY(!ShapeRecognizer::recognize(strokeOf(wave)).has_value());

        // An L: two sides are not a shape
        const QList<QPointF> corner = handDrawn({{10, 10}, {10, 150}, {120, 150}}, 4, 0.5);
        QVERIFY(!ShapeRecognizer::recognize(strokeOf(corner)).has_value());

        // An open rectangle: the ends are too far apart
        QVERIFY(!ShapeRecognizer::recognize(
                         strokeOf(handDrawn({{20, 20}, {180, 20}, {180, 120}, {20, 120}, {20, 75}}, 4, 0.5)))
                         .has_value());
    }

    // Palettes

    void defaultPalette() {
        const Palette palette = Palette::defaultPalette();
        QCOMPARE(palette.name(), QStringLiteral("Xournal++ Palette"));
        QCOMPARE(palette.colors.size(), 11);
        QCOMPARE(palette.colors[0].name, QStringLiteral("Yellow"));
        QCOMPARE(palette.colors[0].color, QColor(255, 225, 107));
        QCOMPARE(palette.colors[8].name, QStringLiteral("Dark Blue"));
        QCOMPARE(palette.colors[8].color, QColor(0, 46, 153));
    }

    void parsesPalettes() {
        Palette palette;
        QString error;
        QVERIFY2(Palette::parse("GIMP Palette\r\nName: Test\r\nColumns: 2\r\n#comment\r\n\r\n"
                                "  1   2   3\tFirst colour \r\n255 0 255\n10 20 30 Über\n",
                                palette, &error),
                 qPrintable(error));
        QCOMPARE(palette.name(), QStringLiteral("Test"));
        QCOMPARE(palette.header.value(QStringLiteral("Columns")), QStringLiteral("2"));
        QCOMPARE(palette.colors.size(), 3);
        QCOMPARE(palette.colors[0].name, QStringLiteral("First colour"));
        QCOMPARE(palette.colors[0].color, QColor(1, 2, 3));
        QCOMPARE(palette.colors[1].name, QString());
        QCOMPARE(palette.colors[2].name, QStringLiteral("Über"));

        // A header without attribute is accepted, as in Xournal++
        QVERIFY(Palette::parse("GIMP Palette\n: Empty\n#\n111 111 111 Gray\n", palette, &error));
        QCOMPARE(palette.colors.size(), 1);
    }

    void rejectsBrokenPalettes_data() {
        QTest::addColumn<QByteArray>("data");
        QTest::addColumn<QString>("message");
        QTest::newRow("empty") << QByteArray() << QStringLiteral("GIMP Palette");
        QTest::newRow("other file") << QByteArray("<?xml version=\"1.0\"?>\n") << QStringLiteral("GIMP Palette");
        QTest::newRow("value too large")
                << QByteArray("GIMP Palette\nName: Empty\n#\n111 333 111 Gray\n222 222 222 Gray2\n")
                << QStringLiteral("Line 4");
        QTest::newRow("broken attribute")
                << QByteArray("GIMP Palette\nName Empty\n#\n111 111 111 Gray\n") << QStringLiteral("Line 2");
        QTest::newRow("two values") << QByteArray("GIMP Palette\n111 111 Gray\n") << QStringLiteral("Line 2");
        QTest::newRow("no colours") << QByteArray("GIMP Palette\nName: Empty\n#\n") << QStringLiteral("any colour");
    }

    void rejectsBrokenPalettes() {
        QFETCH(QByteArray, data);
        QFETCH(QString, message);
        Palette palette = Palette::defaultPalette();
        QString error;
        QVERIFY(!Palette::parse(data, palette, &error));
        QVERIFY2(error.contains(message), qPrintable(error));
        // The palette is not touched
        QCOMPARE(palette.colors.size(), 11);

        QVERIFY(!Palette::load(QStringLiteral("/does/not/exist.gpl"), palette, &error));
        QVERIFY(error.contains(QStringLiteral("exist.gpl")));
    }
};

QTEST_MAIN(TestTools)
#include "tst_tools.moc"
