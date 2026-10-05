/*
 * Qournal
 *
 * Tests for selecting elements, transforming the selection, and the clipboard.
 * They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cmath>

#include "CanvasFixture.h"
#include "Renderer.h"
#include "Selection.h"
#include "XoppLoader.h"
#include "XoppWriter.h"
#include "XournalClipboard.h"

namespace {

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-6) {
    return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}

Stroke line(const QPointF& a, const QPointF& b, double width = 2) {
    Stroke s;
    s.points = {a, b};
    s.width = width;
    s.updateBounds();
    return s;
}

/// A fixture without snapping, with three strokes on the page
struct SelectionFixture: Fixture {
    SelectionFixture() {
        canvas->input()->setProperty("snapGrid", false);
        canvas->input()->setProperty("snapRotation", false);
        strokeOnPage(0, {{100, 100}, {200, 150}});
        strokeOnPage(0, {{100, 200}, {200, 200}});
        strokeOnPage(0, {{300, 100}, {400, 300}});
    }

    /// Drags a rectangle with the select tool around a part of the page
    void selectArea(const QRectF& area) {
        canvas->setTool(PageCanvas::SelectRect);
        stroke(onPage(0, area.topLeft()), onPage(0, area.bottomRight()));
    }

    QPointF first(int strokeIndex) const { return strokes()[strokeIndex].points.first(); }
    QPointF last(int strokeIndex) const { return strokes()[strokeIndex].points.last(); }
};

/// The bounds of a stroke reach half its width and one point beyond its points
constexpr double PAD = 5.67 / 2 + 1;

}  // namespace

#define COMPARE_POINT(actual, expected)                                                                            \
    do {                                                                                                           \
        const QPointF actualPoint = (actual);                                                                      \
        const QPointF expectedPoint = (expected);                                                                  \
        QVERIFY2(near(actualPoint, expectedPoint, 1e-6), qPrintable(QStringLiteral("(%1, %2) instead of (%3, %4)") \
                                                                            .arg(actualPoint.x())                  \
                                                                            .arg(actualPoint.y())                  \
                                                                            .arg(expectedPoint.x())                \
                                                                            .arg(expectedPoint.y())));             \
    } while (false)

class TestSelection: public QObject {
    Q_OBJECT

private slots:
    // The model

    void areas() {
        const SelectionArea rect = SelectionArea::rectangle({50, 80}, {10, 20});
        QVERIFY(rect.contains({30, 50}));
        QVERIFY(rect.contains({10, 20}));
        QVERIFY(!rect.contains({9, 50}));
        QVERIFY(!rect.isTap(5));
        QVERIFY(SelectionArea::rectangle({10, 10}, {12, 11}).isTap(5));

        // A lasso in the shape of a "C": what is in its opening is not selected
        const SelectionArea lasso = SelectionArea::lasso(
                {{0, 0}, {100, 0}, {100, 20}, {20, 20}, {20, 80}, {100, 80}, {100, 100}, {0, 100}});
        QVERIFY(lasso.contains({10, 50}));
        QVERIFY(lasso.contains({60, 10}));
        QVERIFY(lasso.contains({60, 90}));
        QVERIFY(!lasso.contains({60, 50}));
        QVERIFY(!lasso.contains({150, 50}));
        QVERIFY(!SelectionArea::lasso({{0, 0}, {10, 10}}).contains({5, 5}));

        QVERIFY(Selection::isInArea(line({5, 5}, {15, 95}), lasso));
        QVERIFY(!Selection::isInArea(line({5, 5}, {60, 50}), lasso));
        QVERIFY(!Selection::isInArea(Stroke(), lasso));
    }

    void areasReachBeyondThePage() {
        // What sticks out of the page is selected by an area that touches the border
        const Element outside = line({-20, 50}, {30, 50});
        SelectionArea rect = SelectionArea::rectangle({0.5, 10}, {100, 100});
        QVERIFY(!Selection::isInArea(outside, rect));
        rect.extendAtPageEdges(600, 800);
        QVERIFY(Selection::isInArea(outside, rect));
        QVERIFY(!rect.contains({50, 5}));

        SelectionArea lasso = SelectionArea::lasso({{0.5, 10}, {100, 10}, {100, 100}, {0.2, 100}});
        QVERIFY(!Selection::isInArea(outside, lasso));
        lasso.extendAtPageEdges(600, 800);
        QVERIFY(Selection::isInArea(outside, lasso));
        QVERIFY(!lasso.contains({-20, 5}));

        // In the middle of the page nothing changes
        SelectionArea inner = SelectionArea::rectangle({10, 10}, {100, 100});
        inner.extendAtPageEdges(600, 800);
        QVERIFY(!inner.contains({5, 50}));
    }

    void elementAtPosition() {
        Layer layer;
        layer.elements.emplace_back(line({0, 0}, {100, 0}, 2));
        layer.elements.emplace_back(line({0, 6}, {100, 6}, 2));
        Stroke filled = line({200, 0}, {300, 0});
        filled.points = {{200, 0}, {300, 0}, {300, 100}, {200, 100}, {200, 0}};
        filled.fill = 100;
        filled.updateBounds();
        layer.elements.emplace_back(filled);
        TextElement text;
        text.text = QStringLiteral("Hello world");
        text.pos = QPointF(0, 200);
        text.size = 20;
        layer.elements.emplace_back(text);

        // On a stroke, or the closest one within 5 pt
        QCOMPARE(Selection::elementAt(layer, {50, 0.5}), std::optional<size_t>(0));
        QCOMPARE(Selection::elementAt(layer, {50, 3.5}), std::optional<size_t>(1));
        QCOMPARE(Selection::elementAt(layer, {50, 2.5}), std::optional<size_t>(0));
        QVERIFY(!Selection::elementAt(layer, {50, 20}).has_value());
        QVERIFY(!Selection::elementAt(layer, {120, 0}).has_value());
        // Inside of a filled stroke, and on a text
        QCOMPARE(Selection::elementAt(layer, {250, 50}), std::optional<size_t>(2));
        QCOMPARE(Selection::elementAt(layer, {20, 210}), std::optional<size_t>(3));
        // The front-most one wins
        layer.elements.emplace_back(line({0, 0}, {100, 0}, 2));
        QCOMPARE(Selection::elementAt(layer, {50, 0}), std::optional<size_t>(4));
    }

    void transformStroke() {
        Element element = line({10, 10}, {20, 30}, 2);
        std::get<Stroke>(element).widths = {1.0};
        Selection::transform(element, QTransform::fromScale(2, 4.5), 3);
        const auto& stroke = std::get<Stroke>(element);
        QCOMPARE(stroke.points, (QList<QPointF>{{20, 45}, {40, 135}}));
        QCOMPARE(stroke.width, 6.0);
        QCOMPARE(stroke.widths, (QList<double>{3.0}));
        QVERIFY(stroke.bounds.contains(QPointF(30, 90)));
    }

    void transformText() {
        TextElement text;
        text.text = QStringLiteral("Text");
        text.pos = QPointF(100, 50);
        text.size = 12;
        text.wrap = 200;
        Element element = text;

        // Moved and scaled evenly: a larger text at another place, without a matrix
        Selection::transform(element, QTransform::fromScale(2, 2) * QTransform::fromTranslate(10, 0));
        auto* moved = std::get_if<TextElement>(&element);
        QVERIFY(!moved->matrix.has_value());
        QCOMPARE(moved->pos, QPointF(210, 100));
        QCOMPARE(moved->size, 24.0);
        QCOMPARE(moved->wrap, 400.0);
        const QRectF bounds = Renderer::elementBounds(element);
        QVERIFY(near(bounds.topLeft(), QPointF(210, 100), 12));

        // Rotated: needs a matrix; the bounds turn with it
        Selection::transform(element, QTransform().translate(210, 100).rotate(90).translate(-210, -100));
        QVERIFY(moved->matrix.has_value());
        QCOMPARE(moved->size, 24.0);
        const QRectF rotated = Renderer::elementBounds(element);
        QVERIFY(near(rotated.width(), bounds.height(), 1e-6) && near(rotated.height(), bounds.width(), 1e-6));
        QVERIFY(near(rotated.right(), 210, 12));

        // Turned back: a plain text again
        Selection::transform(element, QTransform().translate(210, 100).rotate(-90).translate(-210, -100));
        QVERIFY(!moved->matrix.has_value());
        QVERIFY(near(moved->pos, QPointF(210, 100)));
    }

    void transformImageAndLink() {
        ImageElement image;
        image.image = QImage(40, 20, QImage::Format_RGB32);
        image.naturalSize = QSizeF(40, 20);
        image.rect = QRectF(100, 100, 80, 40);
        Element element = image;

        // Upright: still a rectangle
        Selection::transform(element, QTransform::fromScale(0.5, 2));
        auto* transformed = std::get_if<ImageElement>(&element);
        QVERIFY(!transformed->matrix.has_value());
        QCOMPARE(transformed->rect, QRectF(50, 200, 40, 80));

        // Rotated by 90° around its top left corner: a matrix, and the bounds of the rotated image
        Selection::transform(element, QTransform().translate(50, 200).rotate(90).translate(-50, -200));
        QVERIFY(transformed->matrix.has_value());
        QVERIFY(near(transformed->rect.topLeft(), QPointF(-30, 200)));
        QVERIFY(near(transformed->rect.width(), 80) && near(transformed->rect.height(), 40));
        const QList<QPointF> corners = Selection::elementCorners(element);
        QVERIFY(near(corners[0], QPointF(50, 200)));
        QVERIFY(near(corners[1], QPointF(50, 240)));

        // Without a known natural size the rectangle is taken for it
        ImageElement unknown;
        unknown.rect = QRectF(0, 0, 10, 10);
        element = unknown;
        Selection::transform(element, QTransform::fromTranslate(5, 5));
        QCOMPARE(std::get<ImageElement>(element).rect, QRectF(5, 5, 10, 10));

        LinkElement link;
        link.text = QStringLiteral("Link");
        link.matrix = {1, 0, 0, 1, 30, 40};
        element = link;
        Selection::transform(element, QTransform::fromScale(2, 2));
        QCOMPARE(std::get<LinkElement>(element).matrix, (Matrix{2, 0, 0, 2, 60, 80}));
    }

    void arrange() {
        using Selection::OrderChange;
        using V = std::vector<size_t>;
        QCOMPARE(Selection::arrange({1, 3}, 6, OrderChange::BringToFront), (V{4, 5}));
        QCOMPARE(Selection::arrange({1, 3}, 6, OrderChange::SendToBack), (V{0, 1}));
        // One place above the element in front of the highest one, together
        QCOMPARE(Selection::arrange({1, 3}, 6, OrderChange::BringForward), (V{3, 4}));
        QCOMPARE(Selection::arrange({1, 3}, 6, OrderChange::SendBackward), (V{0, 1}));
        QCOMPARE(Selection::arrange({2, 3}, 6, OrderChange::SendBackward), (V{1, 2}));
        // Already there
        QCOMPARE(Selection::arrange({4, 5}, 6, OrderChange::BringForward), (V{4, 5}));
        QCOMPARE(Selection::arrange({0, 1}, 6, OrderChange::SendBackward), (V{0, 1}));
        QVERIFY(Selection::arrange({}, 6, OrderChange::BringToFront).empty());
    }

    // The canvas

    void selectWithRectangle() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy selectionChanged(c, &PageCanvas::selectionChanged);
        QVERIFY(!c->hasSelection());

        // The first two strokes are completely inside, the third one only partly
        f.selectArea(QRectF(80, 80, 250, 150));
        QVERIFY(c->hasSelection());
        QCOMPARE(selectionChanged.count(), 1);
        QVERIFY(!c->modified() || c->canUndo());

        // The selected strokes are still on the screen, within a frame
        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(150, 200)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(350, 200)).toPoint()), PEN_COLOR);
        const QColor frame = image.pixelColor(f.onPage(0, QPointF(150, 100 - PAD)).toPoint());
        QVERIFY2(frame.red() > 200 && frame.green() < 200, qPrintable(frame.name()));

        // Deleting removes them, in one step
        c->deleteSelection();
        QVERIFY(!c->hasSelection());
        QCOMPARE(f.strokes().size(), 1);
        COMPARE_POINT(f.first(0), QPointF(300, 100));
        image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(150, 200)).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(150, 100 - PAD)).toPoint()), Qt::white);
        c->undo();
        QCOMPARE(f.strokes().size(), 3);
        COMPARE_POINT(f.first(0), QPointF(100, 100));
        COMPARE_POINT(f.first(2), QPointF(300, 100));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), PEN_COLOR);

        // An area without elements selects nothing
        f.selectArea(QRectF(400, 400, 100, 100));
        QVERIFY(!c->hasSelection());
        QVERIFY(!c->canRedo() || c->canUndo());
    }

    void selectWithLassoAndTap() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;

        // Around the second stroke only
        c->setTool(PageCanvas::SelectRegion);
        f.strokeOnPage(0, {{80, 180}, {220, 180}, {220, 220}, {80, 220}, {80, 185}});
        QVERIFY(c->hasSelection());
        c->deleteSelection();
        QCOMPARE(f.strokes().size(), 2);
        COMPARE_POINT(f.first(1), QPointF(300, 100));
        c->undo();

        // A tap with a selection tool selects the element under it
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(350, 200)));
        QVERIFY(c->hasSelection());
        c->deleteSelection();
        QCOMPARE(f.strokes().size(), 2);
        COMPARE_POINT(f.first(1), QPointF(100, 200));
        c->undo();

        // A tap next to everything clears the selection
        f.tap(f.onPage(0, QPointF(350, 200)));
        QVERIFY(c->hasSelection());
        f.tap(f.onPage(0, QPointF(500, 350)));
        QVERIFY(!c->hasSelection());
        QCOMPARE(f.strokes().size(), 3);
    }

    void selectObjectAndDrag() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::SelectObject);

        // Pressing on an object selects it, and it follows the pointer at once
        f.stroke(f.onPage(0, QPointF(150, 200)), f.onPage(0, QPointF(180, 260)));
        QVERIFY(c->hasSelection());
        COMPARE_POINT(f.first(1), QPointF(130, 260));
        COMPARE_POINT(f.last(1), QPointF(230, 260));
        COMPARE_POINT(f.first(0), QPointF(100, 100));
        QCOMPARE(f.strokes().size(), 3);

        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(180, 260)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(150, 200)).toPoint()), Qt::white);

        // Undo puts it back and drops the selection
        c->undo();
        QVERIFY(!c->hasSelection());
        COMPARE_POINT(f.first(1), QPointF(100, 200));
        image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(150, 200)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(180, 260)).toPoint()), Qt::white);

        // Next to the objects nothing is selected
        f.stroke(f.onPage(0, QPointF(450, 400)), f.onPage(0, QPointF(480, 460)));
        QVERIFY(!c->hasSelection());
    }

    /// A selection dragged to the edge of the view moves the view along, as in Xournal++
    void edgePanning() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->insertPage(1);
        c->setCurrentPage(0);
        c->zoomTo(1.0);
        f.selectArea(QRectF(80, 80, 150, 150));
        c->setTool(PageCanvas::Hand);  // drags the selection, as in moveSelection()
        const double top = c->pageViewRect(0).top();
        const QPointF start = f.onPage(0, QPointF(150, 150));
        const QPointF edge(start.x(), c->height() - 2);
        f.tablet(QEvent::TabletPress, start, 0.5);
        f.tablet(QEvent::TabletMove, (start + edge) / 2, 0.5);
        f.tablet(QEvent::TabletMove, edge, 0.5);
        QTest::qWait(300);  // held at the edge
        f.tablet(QEvent::TabletRelease, edge, 0);
        QVERIFY2(c->pageViewRect(0).top() < top - 20,
                 qPrintable(QStringLiteral("%1 %2").arg(c->pageViewRect(0).top()).arg(top)));
        // The selection stayed under the pointer, and went with the view onto the next page
        QCOMPARE(f.strokes(0).size(), 1);
        QCOMPARE(f.strokes(1).size(), 2);
    }

    void moveSelection() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        f.selectArea(QRectF(80, 80, 150, 150));

        // Dragging inside of the frame moves the selection, also with the hand
        c->setTool(PageCanvas::Hand);
        f.stroke(f.onPage(0, QPointF(150, 150)), f.onPage(0, QPointF(190, 350)));
        QVERIFY(c->hasSelection());
        QCOMPARE(f.strokes().size(), 3);
        COMPARE_POINT(f.first(0), QPointF(140, 300));
        COMPARE_POINT(f.last(1), QPointF(240, 400));
        COMPARE_POINT(f.first(2), QPointF(300, 100));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(190, 400))), PEN_COLOR);

        // The arrow keys move it by small steps; with Shift by larger ones
        f.key(Qt::Key_Right);
        f.key(Qt::Key_Up, Qt::ShiftModifier);
        COMPARE_POINT(f.first(0), QPointF(143, 280));

        // Every move is a step to undo
        c->undo();
        COMPARE_POINT(f.first(0), QPointF(143, 300));
        c->undo();
        c->undo();
        COMPARE_POINT(f.first(0), QPointF(100, 100));
        QVERIFY(!c->canUndo() || f.strokes().size() == 3);

        // A press next to the selection drops it and uses the tool
        f.selectArea(QRectF(80, 80, 150, 150));
        const double top = c->pageViewRect(0).top();
        c->setTool(PageCanvas::Hand);
        QVERIFY(c->hasSelection());
        f.stroke(f.onPage(0, QPointF(400, 300)), f.onPage(0, QPointF(400, 300)) + QPointF(0, -30));
        QVERIFY(!c->hasSelection());
        QVERIFY(c->pageViewRect(0).top() < top - 20);
        c->setCurrentPage(0);
        // The tools that draw drop it when they are chosen
        f.selectArea(QRectF(80, 80, 150, 150));
        c->setTool(PageCanvas::Pen);
        QVERIFY(!c->hasSelection());
        f.strokeOnPage(0, {{400, 300}, {450, 300}});
        QCOMPARE(f.strokes().size(), 4);
        // Escape drops it as well
        f.selectArea(QRectF(80, 80, 150, 150));
        f.key(Qt::Key_Escape);
        QVERIFY(!c->hasSelection());
        f.selectArea(QRectF(80, 80, 150, 150));
        f.key(Qt::Key_Delete);
        QCOMPARE(f.strokes().size(), 2);
    }

    void moveSnapsToTheGrid() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("snapGrid", true);
        f.selectArea(QRectF(80, 80, 150, 150));

        // The corner of the frame next to where it is grabbed lands on the grid
        f.stroke(f.onPage(0, QPointF(140, 140)), f.onPage(0, QPointF(171, 199)));
        const QPointF corner = f.first(0) - QPointF(PAD, PAD);
        QVERIFY2(near(std::remainder(corner.x(), 14.17), 0, 1e-6) && near(std::remainder(corner.y(), 14.17), 0, 1e-6),
                 qPrintable(QStringLiteral("%1, %2").arg(corner.x()).arg(corner.y())));
        COMPARE_POINT(corner, QPointF(9 * 14.17, 11 * 14.17));
        // The other stroke of the selection moved by the same amount
        COMPARE_POINT(f.first(1) - f.first(0), QPointF(0, 100));

        // A thin frame can be grabbed in its middle as well
        c->undo();
        c->input()->setProperty("snapGrid", false);
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 200)));
        f.stroke(f.onPage(0, QPointF(150, 200)), f.onPage(0, QPointF(160, 230)));
        COMPARE_POINT(f.first(1), QPointF(110, 230));
        COMPARE_POINT(f.last(1), QPointF(210, 230));
    }

    void scaleSelection() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 125)));
        QVERIFY(c->hasSelection());

        // The frame of the first stroke; dragging its bottom right corner to twice the size
        const QRectF frame(100 - PAD, 100 - PAD, 100 + 2 * PAD, 50 + 2 * PAD);
        f.stroke(f.onPage(0, frame.bottomRight()),
                 f.onPage(0, frame.bottomRight() + QPointF(frame.width(), frame.height())));
        QVERIFY(c->hasSelection());
        Stroke stroke = f.strokes()[0];
        COMPARE_POINT(stroke.points[0], frame.topLeft() + 2 * QPointF(PAD, PAD));
        COMPARE_POINT(stroke.points[1], frame.topLeft() + 2 * QPointF(PAD + 100, PAD + 50));
        QVERIFY(near(stroke.width, 2 * 5.67));
        COMPARE_COLOR(f.pixel(f.onPage(0, (stroke.points[0] + stroke.points[1]) / 2)), PEN_COLOR);

        // The right side only makes it wider; the lines get wider by the geometric mean
        const QRectF scaled(frame.topLeft(), frame.size() * 2);
        f.stroke(f.onPage(0, QPointF(scaled.right(), scaled.center().y())),
                 f.onPage(0, QPointF(scaled.right() + scaled.width(), scaled.center().y() + 30)));
        stroke = f.strokes()[0];
        COMPARE_POINT(stroke.points[1], QPointF(frame.left() + 4 * (PAD + 100), frame.top() + 2 * (PAD + 50)));
        QVERIFY(near(stroke.width, 2 * 5.67 * std::sqrt(2.0)));

        // The top left corner: the bottom right one stays
        c->undo();
        c->undo();
        f.tap(f.onPage(0, QPointF(150, 125)));
        f.stroke(f.onPage(0, frame.topLeft()), f.onPage(0, frame.center()));
        stroke = f.strokes()[0];
        COMPARE_POINT(stroke.points[1], frame.bottomRight() - QPointF(PAD, PAD) / 2);
        COMPARE_POINT(stroke.points[0], frame.center() + QPointF(PAD, PAD) / 2);

        // It cannot be made smaller than a few pixels
        f.stroke(f.onPage(0, frame.center()), f.onPage(0, frame.bottomRight() - QPointF(1, 1)));
        stroke = f.strokes()[0];
        QVERIFY(stroke.points[0].x() < stroke.points[1].x());
        QVERIFY(stroke.bounds.width() > 3 && stroke.bounds.width() < 20);

        // Dragged past the opposite corner, it is mirrored
        c->undo();
        stroke = f.strokes()[0];
        f.tap(f.onPage(0, (stroke.points[0] + stroke.points[1]) / 2));
        QVERIFY(c->hasSelection());
        f.stroke(f.onPage(0, frame.center()), f.onPage(0, frame.bottomRight() + QPointF(100, 100)));
        stroke = f.strokes()[0];
        QVERIFY(stroke.points[0].x() > stroke.points[1].x());
        QVERIFY(stroke.points[0].y() > stroke.points[1].y());
        QVERIFY(stroke.bounds.width() > 50);
        QVERIFY(stroke.width > 0);
    }

    void rotateSelection() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 200)));

        // The handle to the right of the frame; a quarter turn around the center of the frame
        const QRectF frame(100 - PAD, 200 - PAD, 100 + 2 * PAD, 2 * PAD);
        const QPointF center = frame.center();
        const QPointF handle = f.onPage(0, QPointF(frame.right(), center.y())) + QPointF(16, 0);
        const double radius = (handle.x() - f.onPage(0, center).x());
        f.stroke(handle, f.onPage(0, center) + QPointF(0, radius));
        Stroke stroke = f.strokes()[1];
        COMPARE_POINT(stroke.points[0], QPointF(150, 150));
        COMPARE_POINT(stroke.points[1], QPointF(150, 250));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 230))), PEN_COLOR);

        // The frame has turned with it: its handle is below now, and its sides scale along the stroke
        QVERIFY(c->hasSelection());
        f.stroke(f.onPage(0, center) + QPointF(0, radius), f.onPage(0, center) + QPointF(radius, 0));
        stroke = f.strokes()[1];
        COMPARE_POINT(stroke.points[0], QPointF(100, 200));
        COMPARE_POINT(stroke.points[1], QPointF(200, 200));

        // With snapping the rotation stops at multiples of 15°
        c->input()->setProperty("snapRotation", true);
        f.stroke(handle, f.onPage(0, center) + QPointF(radius * std::cos(0.8), radius * std::sin(0.8)));
        stroke = f.strokes()[1];
        const QPointF direction = stroke.points[1] - stroke.points[0];
        const double angle = std::atan2(direction.y(), direction.x());
        QVERIFY2(near(angle, M_PI / 4, 1e-6), qPrintable(QString::number(angle)));
    }

    void changeColourSizeAndFill() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        f.selectArea(QRectF(80, 80, 150, 150));

        c->setColor(Qt::red);
        QCOMPARE(f.strokes()[0].color, QColor(Qt::red));
        QCOMPARE(f.strokes()[1].color, QColor(Qt::red));
        QCOMPARE(f.strokes()[2].color, PEN_COLOR);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), QColor(Qt::red));

        c->setToolSize(PageCanvas::Fine);
        QCOMPARE(f.strokes()[0].width, 0.85);
        QCOMPARE(f.strokes()[2].width, 5.67);
        c->setFill(true);
        QCOMPARE(f.strokes()[0].fill, 128);
        c->setLineStyle(QStringLiteral("dot"));
        QCOMPARE(f.strokes()[1].style, QStringLiteral("dot"));
        QVERIFY(c->hasSelection());

        // Four steps to undo
        for (int i = 0; i < 4; ++i) {
            c->undo();
        }
        QCOMPARE(f.strokes()[0].color, PEN_COLOR);
        QCOMPARE(f.strokes()[0].width, 5.67);
        QCOMPARE(f.strokes()[0].fill, -1);
        QVERIFY(f.strokes()[1].style.isEmpty());

        // A highlighter stroke keeps its transparency and gets the size of the highlighter
        c->setTool(PageCanvas::Highlighter);
        c->setFill(false);
        f.strokeOnPage(0, {{100, 400}, {200, 400}});
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 400)));
        c->setColor(Qt::green);
        c->setToolSize(PageCanvas::Thick);
        QCOMPARE(f.strokes()[3].color, QColor(0, 255, 0, 0x7f));
        QCOMPARE(f.strokes()[3].width, 19.84);
    }

    void arrangeSelection() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 125)));

        c->arrangeSelection(PageCanvas::BringToFront);
        COMPARE_POINT(f.first(2), QPointF(100, 100));
        COMPARE_POINT(f.first(0), QPointF(100, 200));
        c->arrangeSelection(PageCanvas::BringToFront);  // already there
        c->arrangeSelection(PageCanvas::SendBackward);
        COMPARE_POINT(f.first(1), QPointF(100, 100));
        c->arrangeSelection(PageCanvas::SendToBack);
        COMPARE_POINT(f.first(0), QPointF(100, 100));
        c->arrangeSelection(PageCanvas::BringForward);
        COMPARE_POINT(f.first(1), QPointF(100, 100));
        QVERIFY(c->hasSelection());

        // The selection is still the same element
        c->deleteSelection();
        QCOMPARE(f.strokes().size(), 2);
        COMPARE_POINT(f.first(0), QPointF(100, 200));
        COMPARE_POINT(f.first(1), QPointF(300, 100));
        for (int i = 0; i < 5; ++i) {
            c->undo();
        }
        COMPARE_POINT(f.first(0), QPointF(100, 100));
        COMPARE_POINT(f.first(1), QPointF(100, 200));
        COMPARE_POINT(f.first(2), QPointF(300, 100));
    }

    void copyCutAndPaste() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        QGuiApplication::clipboard()->clear();
        QVERIFY(!c->canPaste());
        f.selectArea(QRectF(80, 80, 150, 150));

        QSignalSpy clipboardChanged(c, &PageCanvas::clipboardChanged);
        c->copy();
        QVERIFY(clipboardChanged.count() >= 1);
        QVERIFY(c->canPaste());
        QCOMPARE(f.strokes().size(), 3);
        // Other applications get a picture of the selection
        QVERIFY(QGuiApplication::clipboard()->mimeData()->hasImage());
        const QImage picture = QGuiApplication::clipboard()->image();
        QVERIFY(picture.width() > 200 && picture.height() > 200);

        // The copies are placed in the middle of the view and selected
        c->paste();
        QCOMPARE(f.strokes().size(), 5);
        QVERIFY(c->hasSelection());
        const QRectF pageView = c->pageViewRect(0);
        const QPointF middle = (QRectF(-pageView.topLeft() / c->zoom(), QSizeF(800, 600) / c->zoom()) &
                                QRectF(QPointF(0, 0), c->pageSize(0)))
                                       .center();
        COMPARE_POINT((f.first(3) + f.last(4)) / 2, middle);
        QCOMPARE(f.strokes()[3].points.size(), 2);
        QCOMPARE(f.strokes()[3].color, PEN_COLOR);
        QCOMPARE(f.strokes()[3].width, 5.67);
        COMPARE_COLOR(f.pixel(f.onPage(0, (f.first(4) + f.last(4)) / 2)), PEN_COLOR);

        // Pasting is one step
        c->undo();
        QCOMPARE(f.strokes().size(), 3);
        QVERIFY(!c->hasSelection());

        // Cut removes the selection; it can be pasted on another page
        f.selectArea(QRectF(80, 80, 150, 150));
        c->cut();
        QCOMPARE(f.strokes().size(), 1);
        c->insertPage(1);
        c->paste();
        QCOMPARE(f.strokes(1).size(), 2);
        QCOMPARE(f.strokes(0).size(), 1);
        QVERIFY(c->hasSelection());

        // Without a selection there is nothing to copy
        c->clearSelection();
        QGuiApplication::clipboard()->clear();
        c->copy();
        c->cut();
        c->paste();
        QCOMPARE(f.strokes(1).size(), 2);
    }

    void pasteFromOtherApplications() {
        Fixture f;
        PageCanvas* c = f.canvas;

        // An image
        QImage image(200, 100, QImage::Format_RGB32);
        image.fill(Qt::darkGreen);
        QGuiApplication::clipboard()->setImage(image);
        QVERIFY(c->canPaste());
        c->paste();
        QVERIFY(c->hasSelection());
        const Document& doc = c->document();
        QCOMPARE(doc.pages[0].layers[0].elements.size(), size_t(1));
        const auto* pasted = std::get_if<ImageElement>(&doc.pages[0].layers[0].elements[0]);
        QVERIFY(pasted);
        QCOMPARE(pasted->rect.size(), QSizeF(200, 100));
        QVERIFY(pasted->data.startsWith("\x89PNG"));
        const QPointF imageCenter = pasted->rect.center();
        COMPARE_COLOR(f.pixel(f.onPage(0, imageCenter)), QColor(Qt::darkGreen));

        // An image larger than the page is made to fit
        QImage large(3000, 1000, QImage::Format_RGB32);
        large.fill(Qt::blue);
        QGuiApplication::clipboard()->setImage(large);
        c->paste();
        const auto* fitted = std::get_if<ImageElement>(&doc.pages[0].layers[0].elements[1]);
        QVERIFY(fitted);
        QVERIFY(near(fitted->rect.width(), 0.9 * doc.pages[0].width, 1e-6));
        QVERIFY(near(fitted->rect.height(), 0.3 * doc.pages[0].width, 1e-6));

        // Text
        QGuiApplication::clipboard()->setText(QStringLiteral("Pasted text\nin two lines"));
        c->paste();
        const auto* text = std::get_if<TextElement>(&doc.pages[0].layers[0].elements[2]);
        QVERIFY(text);
        QCOMPARE(text->text, QStringLiteral("Pasted text\nin two lines"));
        QCOMPARE(text->size, 12.0);
        QVERIFY(c->hasSelection());

        // What was pasted is written to the file and read again
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pasted.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document reloaded;
        QVERIFY(loadXopp(path, reloaded, nullptr));
        QCOMPARE(reloaded.pages[0].layers[0].elements.size(), size_t(3));
        const auto* image2 = std::get_if<ImageElement>(&reloaded.pages[0].layers[0].elements[0]);
        QVERIFY(image2 && image2->image.size() == QSize(200, 100));
        QVERIFY(near(image2->rect.center(), imageCenter, 1e-4));

        // Three steps to undo; an empty clipboard pastes nothing
        for (int i = 0; i < 3; ++i) {
            c->undo();
        }
        QVERIFY(doc.pages[0].layers[0].elements.empty());
        QGuiApplication::clipboard()->clear();
        QVERIFY(!c->canPaste());
        c->paste();
        QVERIFY(doc.pages[0].layers[0].elements.empty());
    }

    /// Selections are exchanged with Xournal++ in its own clipboard format
    void xournalppClipboard() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        f.selectArea(QRectF(80, 80, 150, 150));
        const qsizetype before = f.strokes().size();
        c->copy();

        // What Xournal++ would take from the clipboard
        const QByteArray data = QGuiApplication::clipboard()->mimeData()->data(XournalClipboard::mimeType());
        std::vector<Element> copied;
        QVERIFY(XournalClipboard::read(data, copied));
        QVERIFY(!copied.empty());

        // What Xournal++ puts there: only its own format counts, not ours
        auto* mime = new QMimeData;
        mime->setData(XournalClipboard::mimeType(), data);
        QGuiApplication::clipboard()->setMimeData(mime);
        QVERIFY(c->canPaste());
        c->paste();
        QCOMPARE(f.strokes().size(), before + static_cast<qsizetype>(copied.size()));
        c->undo();
        QCOMPARE(f.strokes().size(), before);

        // Another version of Xournal++: a message, and nothing is pasted
        QSignalSpy failed(c, &PageCanvas::pasteFailed);
        mime = new QMimeData;
        QByteArray other = data;
        other.replace("xournalpp 1.3.8", "xournalpp 9.9.9");
        mime->setData(XournalClipboard::mimeType(), other);
        QGuiApplication::clipboard()->setMimeData(mime);
        c->paste();
        QCOMPARE(failed.count(), 1);
        QCOMPARE(f.strokes().size(), before);
        QGuiApplication::clipboard()->clear();
    }

    void moveToAnotherPage() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        c->insertPage(1);
        c->setCurrentPage(0);
        c->zoomTo(0.4);  // both pages are visible
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 200)));
        QVERIFY(c->hasSelection());

        // Dropped on the second page: the stroke is there, at the place where it was dropped
        f.stroke(f.onPage(0, QPointF(150, 200)), f.onPage(1, QPointF(250, 300)));
        QCOMPARE(f.strokes(0).size(), 2);
        QCOMPARE(f.strokes(1).size(), 1);
        QVERIFY(near(f.strokes(1)[0].points[0], QPointF(200, 300), 1e-6));
        QVERIFY(c->hasSelection());
        QCOMPARE(c->currentPage(), 1);

        // It is still selected there
        f.key(Qt::Key_Right);
        QVERIFY(near(f.strokes(1)[0].points[0], QPointF(203, 300), 1e-6));
        c->undo();
        c->undo();
        QCOMPARE(f.strokes(0).size(), 3);
        QCOMPARE(f.strokes(1).size(), 0);
        COMPARE_POINT(f.first(1), QPointF(100, 200));
    }

    void selectAllAndLayers() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->openFile(dataFile(QStringLiteral("layers.xopp")));
        const Document& doc = c->document();
        const size_t layerCount = doc.pages[0].layers.size();
        QVERIFY(layerCount > 1);
        std::vector<size_t> counts;
        for (const Layer& layer: doc.pages[0].layers) {
            counts.push_back(layer.elements.size());
        }

        // Everything on the top layer
        c->selectAll();
        QVERIFY(c->hasSelection() == (counts.back() > 0));
        c->deleteSelection();
        QCOMPARE(doc.pages[0].layers.back().elements.size(), size_t(0));
        for (size_t l = 0; l + 1 < layerCount; ++l) {
            QCOMPARE(doc.pages[0].layers[l].elements.size(), counts[l]);
        }

        // The top layer is empty now: a rectangle around the page selects nothing there ...
        c->setTool(PageCanvas::SelectRect);
        f.stroke(f.onPage(0, QPointF(0.5, 0.5)), f.onPage(0, QPointF(doc.pages[0].width - 0.5, 400)));
        QVERIFY(!c->hasSelection());
        // ... but on the layers below, if they are included: the top-most one with something in the area
        c->setSelectAllLayers(true);
        QVERIFY(c->selectAllLayers());
        f.stroke(f.onPage(0, QPointF(0.5, 0.5)), f.onPage(0, QPointF(doc.pages[0].width - 0.5, 400)));
        size_t lower = layerCount - 1;
        while (lower > 0 && counts[lower - 1] == 0) {
            --lower;
        }
        if (lower > 0) {
            QVERIFY(c->hasSelection());
            c->deleteSelection();
            QVERIFY(doc.pages[0].layers[lower - 1].elements.size() < counts[lower - 1]);
        }
    }

    void selectionSurvivesWhatDoesNotTouchIt() {
        SelectionFixture f;
        PageCanvas* c = f.canvas;
        f.selectArea(QRectF(80, 80, 150, 150));

        // Zooming and scrolling keep the selection
        c->zoomTo(2.0);
        f.wheel(QPointF(400, 300), -120);
        QVERIFY(c->hasSelection());
        c->fitWidth();
        c->setCurrentPage(0);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), PEN_COLOR);

        // Saving writes the selected elements as well
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("selected.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document reloaded;
        QVERIFY(loadXopp(path, reloaded, nullptr));
        QCOMPARE(reloaded.pages[0].layers[0].elements.size(), size_t(3));
        QVERIFY(c->hasSelection());

        // Changes of the pages and a new document drop it
        c->insertPage(1);
        QVERIFY(!c->hasSelection());
        c->setCurrentPage(0);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(150, 200))), PEN_COLOR);
        f.selectArea(QRectF(80, 80, 150, 150));
        QVERIFY(c->hasSelection());
        c->newDocument();
        QVERIFY(!c->hasSelection());
        c->deleteSelection();
        c->arrangeSelection(PageCanvas::BringToFront);
        c->selectAll();
        QVERIFY(!c->hasSelection());
    }
};

QTEST_MAIN(TestSelection)
#include "tst_selection.moc"
