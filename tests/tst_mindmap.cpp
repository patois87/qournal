/*
 * Qournal
 *
 * Tests for the recognition of mind maps drawn by hand: loops, the shape of nodes, nodes around writing and
 * branches between them
 *
 * @license GNU GPLv2 or later
 */

#include <QTest>
#include <cmath>
#include <map>

#include "Document.h"
#include "MindMap.h"
#include "Selection.h"

namespace {

using MindMap::Result;
using MindMap::Shape;

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

/**
 * An ellipse drawn by hand: from the angle start, going round turns times, with a wobble
 * @param power 2 for an ellipse, more for a shape between an ellipse and a rectangle
 */
QList<QPointF> loop(const QPointF& center, double a, double b, double turns = 1.0, double start = -2.0,
                    double power = 2.0) {
    QList<QPointF> points;
    const int count = static_cast<int>(80 * turns);
    for (int i = 0; i <= count; ++i) {
        const double t = start + 2 * M_PI * turns * i / count;
        const double c = std::cos(t);
        const double s = std::sin(t);
        const double x = std::copysign(std::pow(std::abs(c), 2 / power), c);
        const double y = std::copysign(std::pow(std::abs(s), 2 / power), s);
        const double wobble = 1 + 0.03 * std::sin(7 * t);
        points.append(center + QPointF(a * x * wobble, b * y * wobble));
    }
    return points;
}

Stroke strokeOf(const QList<QPointF>& points) {
    Stroke s;
    s.points = points;
    s.updateBounds();
    return s;
}

/// A word written by hand in the rectangle: a stroke for each letter, going up and down
void write(Layer& layer, const QRectF& rect, int letters = 4) {
    const double w = rect.width() / letters;
    for (int i = 0; i < letters; ++i) {
        const double x = rect.left() + i * w;
        layer.elements.push_back(strokeOf(handDrawn({{x + 0.1 * w, rect.bottom()},
                                                     {x + 0.4 * w, rect.top()},
                                                     {x + 0.6 * w, rect.bottom()},
                                                     {x + 0.9 * w, rect.top() + 0.4 * rect.height()}},
                                                    1.5, 0.3)));
    }
}

/// The index of the stroke added to the layer
size_t add(Layer& layer, const Stroke& stroke) {
    layer.elements.push_back(stroke);
    return layer.elements.size() - 1;
}

double distanceToOutline(const QPointF& p, const QList<QPointF>& outline) {
    double best = 1e9;
    for (qsizetype i = 0; i + 1 < outline.size(); ++i) {
        const QPointF a = outline[i];
        const QPointF d = outline[i + 1] - a;
        const double t = std::clamp(QPointF::dotProduct(p - a, d) / QPointF::dotProduct(d, d), 0.0, 1.0);
        best = std::min(best, QLineF(p, a + d * t).length());
    }
    return best;
}

/// The layer with the elements moved by the offset
Layer moved(const Layer& layer, const std::vector<size_t>& elements, const QPointF& offset) {
    Layer result = layer;
    for (const size_t i: elements) {
        Selection::transform(result.elements[i], QTransform::fromTranslate(offset.x(), offset.y()));
    }
    return result;
}

std::vector<size_t> range(size_t first, size_t count) {
    std::vector<size_t> result;
    for (size_t i = 0; i < count; ++i) {
        result.push_back(first + i);
    }
    return result;
}

}  // namespace

class TestMindMap : public QObject {
    Q_OBJECT

private slots:
    void recognizesLoops() {
        QVERIFY(MindMap::isLoop(loop({200, 150}, 60, 60)));
        QVERIFY(MindMap::isLoop(loop({200, 150}, 90, 30)));
        // With a gap, and overlapping at the end
        QVERIFY(MindMap::isLoop(loop({200, 150}, 90, 30, 0.92)));
        QVERIFY(MindMap::isLoop(loop({200, 150}, 90, 30, 1.2)));
        // Drawn the other way round
        QList<QPointF> reversed = loop({200, 150}, 90, 30);
        std::reverse(reversed.begin(), reversed.end());
        QVERIFY(MindMap::isLoop(reversed));
        QVERIFY(MindMap::isLoop(handDrawn({{20, 20}, {182, 23}, {180, 121}, {18, 118}, {21, 25}}, 4, 0.7)));
    }

    void leavesOtherStrokesAsTheyAre() {
        QVERIFY(!MindMap::isLoop(handDrawn({{10, 100}, {200, 104}}, 4, 0.6)));
        // A U, a wave, an open circle, a spiral
        QVERIFY(!MindMap::isLoop(handDrawn({{10, 10}, {10, 100}, {100, 100}, {100, 10}}, 4, 0.5)));
        QList<QPointF> wave;
        for (int i = 0; i <= 100; ++i) {
            wave.append(QPointF(10 + 3 * i, 100 + 40 * std::sin(i / 8.0)));
        }
        QVERIFY(!MindMap::isLoop(wave));
        QVERIFY(!MindMap::isLoop(loop({200, 150}, 60, 60, 0.7)));
        QVERIFY(!MindMap::isLoop(loop({200, 150}, 60, 60, 3)));
        QVERIFY(!MindMap::isLoop({{0, 0}, {10, 0}, {10, 10}}));
        QVERIFY(!MindMap::isLoop({}));
    }

    void recognizesTheShapeDrawn() {
        QCOMPARE(MindMap::shapeOf(loop({200, 150}, 60, 60)), Shape::Ellipse);
        QCOMPARE(MindMap::shapeOf(loop({200, 150}, 90, 30, 1.1)), Shape::Ellipse);
        QCOMPARE(MindMap::shapeOf(handDrawn({{20, 20}, {182, 23}, {180, 121}, {18, 118}, {21, 25}}, 4, 0.7)),
                 Shape::Rectangle);
        // Started in the middle of a side, overlapping a little
        QCOMPARE(MindMap::shapeOf(handDrawn({{100, 20}, {180, 22}, {181, 80}, {19, 82}, {20, 21}, {120, 20}}, 4,
                                            0.7)),
                 Shape::Rectangle);
        // Neither clearly: an ellipse
        QCOMPARE(MindMap::shapeOf(loop({200, 150}, 60, 60, 1.0, -2.0, 3.0)), Shape::Ellipse);
    }

    void fitsOutlinesToTheWriting() {
        const QRectF writing(100, 100, 80, 20);
        const QList<QPointF> rectangle = MindMap::outline(Shape::Rectangle, writing, 6);
        QCOMPARE(rectangle.size(), 5);
        QCOMPARE(rectangle.first(), rectangle.last());
        QCOMPARE(QPolygonF(rectangle).boundingRect(), QRectF(94, 94, 92, 32));

        const QList<QPointF> ellipse = MindMap::outline(Shape::Ellipse, writing, 6);
        QVERIFY(ellipse.size() > 24);
        QCOMPARE(ellipse.first(), ellipse.last());
        // The corners of the writing are inside, at least the margin away from the outline
        const QPolygonF polygon(ellipse);
        for (const QPointF& corner: {writing.topLeft(), writing.topRight(), writing.bottomLeft(),
                                     writing.bottomRight()}) {
            QVERIFY(polygon.containsPoint(corner, Qt::OddEvenFill));
            QVERIFY(distanceToOutline(corner, ellipse) >= 4);
        }
        // And not much larger than needed
        QVERIFY(polygon.boundingRect().width() < 80 * M_SQRT2 + 13);
        QVERIFY(polygon.boundingRect().height() < 20 * M_SQRT2 + 13);
    }

    void makesNodesAroundWriting() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        Stroke drawn = strokeOf(loop({140, 110}, 70, 30));
        drawn.color = Qt::red;
        drawn.widths.fill(2.0, drawn.points.size() - 1);
        const Result result = MindMap::recognize(drawn, layer);
        QCOMPARE(result.kind, Result::Kind::Node);
        QCOMPARE(result.contents, range(0, 4));
        // An ellipse around the writing, in the colour of the stroke, of one width
        QVERIFY(result.stroke.points.size() > 24);
        QCOMPARE(result.stroke.points.first(), result.stroke.points.last());
        QCOMPARE(result.stroke.color, QColor(Qt::red));
        QVERIFY(!result.stroke.hasPressure());
        QCOMPARE(result.stroke.width, 2.0);
        const QPolygonF outline(result.stroke.points);
        for (const Element& element: layer.elements) {
            for (const QPointF& p: std::get<Stroke>(element).points) {
                QVERIFY(outline.containsPoint(p, Qt::OddEvenFill));
            }
        }
        // Tighter than the loop drawn
        QVERIFY(outline.boundingRect().width() < 2 * 70);

        // A rectangle
        const Result box = MindMap::recognize(
                strokeOf(handDrawn({{80, 85}, {200, 87}, {201, 135}, {79, 134}, {81, 88}}, 4, 0.7)), layer);
        QCOMPARE(box.kind, Result::Kind::Node);
        QCOMPARE(box.stroke.points.size(), 5);
        QVERIFY(QPolygonF(box.stroke.points).boundingRect().contains(QRectF(100, 100, 80, 20)));
    }

    void makesNodesAroundTextsAndFormulas() {
        Layer layer;
        TextElement text;
        text.text = QStringLiteral("Idea");
        text.pos = QPointF(100, 100);
        layer.elements.push_back(text);
        Result result = MindMap::recognize(strokeOf(loop({120, 108}, 60, 30)), layer);
        QCOMPARE(result.kind, Result::Kind::Node);
        QCOMPARE(result.contents, range(0, 1));

        ImageElement formula;
        formula.tex = true;
        formula.texSource = QStringLiteral("x^2");
        formula.rect = QRectF(300, 300, 40, 20);
        layer.elements.push_back(formula);
        result = MindMap::recognize(strokeOf(loop({320, 310}, 50, 30)), layer);
        QCOMPARE(result.kind, Result::Kind::Node);
        QCOMPARE(result.contents, range(1, 1));
    }

    void leavesSmallLoopsAlone() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        // Small, around nothing
        QCOMPARE(MindMap::recognize(strokeOf(loop({400, 300}, 12, 8)), layer).kind, Result::Kind::Ink);
        // An "o" in the word
        QCOMPARE(MindMap::recognize(strokeOf(loop({190, 112}, 5, 6)), layer).kind, Result::Kind::Ink);
        // Around a node: a group
        const Result node = MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer);
        add(layer, node.stroke);
        QCOMPARE(MindMap::recognize(strokeOf(loop({140, 110}, 120, 70)), layer).kind, Result::Kind::Group);
    }

    void findsTheNodesOfALayer() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t o = add(layer, strokeOf(loop({190, 112}, 5, 6)));  // an "o" in the word
        const size_t node = add(layer, MindMap::recognize(strokeOf(loop({145, 110}, 75, 30)), layer).stroke);
        write(layer, QRectF(400, 100, 60, 20), 3);
        const size_t second = add(layer, MindMap::recognize(strokeOf(loop({430, 110}, 60, 30)), layer).stroke);
        add(layer, strokeOf(loop({300, 110}, 260, 90)));  // a group around both
        // A node without writing yet, as the tool made it; as it was drawn, a large empty loop is writing too
        const size_t empty = add(layer, MindMap::recognize(strokeOf(loop({300, 400}, 40, 40)), layer).stroke);
        add(layer, strokeOf(loop({300, 500}, 10, 8)));  // a small loop: writing
        add(layer, strokeOf(loop({500, 500}, 40, 40, 1.1)));  // its ends overlap, as those of a hand do

        const std::vector<MindMap::Node> nodes = MindMap::findNodes(layer);
        QCOMPARE(nodes.size(), size_t(3));
        QCOMPARE(nodes[2].element, empty);
        QVERIFY(nodes[2].contents.empty());
        QCOMPARE(nodes[0].element, node);
        std::vector<size_t> contents = range(0, 4);
        contents.push_back(o);
        QCOMPARE(nodes[0].contents, contents);
        QCOMPARE(nodes[1].element, second);
        QCOMPARE(nodes[1].contents, range(second - 3, 3));
    }

    void smoothsPaths() {
        // Nearly straight: straight
        QList<QPointF> path = MindMap::smoothPath({0, 0}, handDrawn({{0, 0}, {200, 0}}, 4, 1.0), {200, 0});
        QVERIFY(path.size() > 8);
        for (const QPointF& p: path) {
            QVERIFY(std::abs(p.y()) < 1e-9);
        }
        QCOMPARE(path.first(), QPointF(0, 0));
        QCOMPARE(path.last(), QPointF(200, 0));

        // An arc: a curve through it
        QList<QPointF> arc;
        for (int i = 1; i < 40; ++i) {
            const double t = M_PI * i / 40;
            arc.append(QPointF(100 - 100 * std::cos(t), -50 * std::sin(t)));
        }
        path = MindMap::smoothPath({0, 0}, arc, {200, 0});
        QCOMPARE(path.first(), QPointF(0, 0));
        QCOMPARE(path.last(), QPointF(200, 0));
        for (const QPointF& p: arc) {
            QVERIFY2(distanceToOutline(p, path) < 4, qPrintable(QStringLiteral("%1 %2").arg(p.x()).arg(p.y())));
        }
        // Close enough to each other to look smooth
        for (qsizetype i = 0; i + 1 < path.size(); ++i) {
            QVERIFY(QLineF(path[i], path[i + 1]).length() < 6);
        }
    }

    void makesBranchesBetweenNodes() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 250, 60, 20), 3);
        const size_t b = add(layer, MindMap::recognize(strokeOf(loop({330, 260}, 60, 30)), layer).stroke);
        const QList<QPointF> outlineA = std::get<Stroke>(layer.elements[a]).points;
        const QList<QPointF> outlineB = std::get<Stroke>(layer.elements[b]).points;

        // From inside the first node to near the second one, bent: a curve from outline to outline
        const QList<QPointF> bent = handDrawn({{150, 120}, {260, 160}, {300, 235}}, 3, 1.0);
        Result result = MindMap::recognize(strokeOf(bent), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.from, std::optional<size_t>(a));
        QCOMPARE(result.to, std::optional<size_t>(b));
        const QList<QPointF>& path = result.stroke.points;
        QVERIFY(path.size() > 8);
        QVERIFY(distanceToOutline(path.first(), outlineA) < 0.5);
        QVERIFY(distanceToOutline(path.last(), outlineB) < 0.5);
        QVERIFY(distanceToOutline(QPointF(260, 160), path) < 10);
        // Thicker at the node it comes from
        QVERIFY(result.stroke.hasPressure());
        QVERIFY(std::abs(result.stroke.widths.first() - 1.5 * 1.41) < 0.1);
        QVERIFY(std::abs(result.stroke.widths.last() - 1.41) < 0.1);

        // Straight, the other way round
        result = MindMap::recognize(strokeOf(handDrawn({{320, 240}, {180, 130}}, 3, 1.0)), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.from, std::optional<size_t>(b));
        QCOMPARE(result.to, std::optional<size_t>(a));
        const QLineF chord(result.stroke.points.first(), result.stroke.points.last());
        QVERIFY(distanceToOutline(chord.p1(), outlineB) < 0.5);
        QVERIFY(distanceToOutline(chord.p2(), outlineA) < 0.5);
        for (const QPointF& p: result.stroke.points) {
            QVERIFY(distanceToOutline(p, {chord.p1(), chord.p2()}) < 1e-6);
        }

        // Without tapering: of one width
        MindMap::Options options;
        options.taper = false;
        result = MindMap::recognize(strokeOf(bent), layer, options);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QVERIFY(!result.stroke.hasPressure());
    }

    void makesBranchesIntoEmptySpace() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        const QList<QPointF> outline = std::get<Stroke>(layer.elements[a]).points;

        Result result = MindMap::recognize(strokeOf(handDrawn({{170, 125}, {300, 300}}, 3, 1.0)), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.from, std::optional<size_t>(a));
        QVERIFY(!result.to.has_value());
        QVERIFY(distanceToOutline(result.stroke.points.first(), outline) < 0.5);
        QCOMPARE(result.stroke.points.last(), QPointF(300, 300));
        // Thin at its free end
        QVERIFY(std::abs(result.stroke.widths.last() - 0.5 * 1.41) < 0.1);

        // Drawn towards the node, ending close to its outline: it starts at the node
        const QList<QPointF> towards = handDrawn({{40, 300}, {110, 135}}, 3, 1.0);
        result = MindMap::recognize(strokeOf(towards), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.from, std::optional<size_t>(a));
        QVERIFY(!result.to.has_value());
        QVERIFY(distanceToOutline(result.stroke.points.first(), outline) < 0.5);
        QCOMPARE(result.stroke.points.last(), towards.first());
        QVERIFY(result.stroke.widths.first() > result.stroke.widths.last());
    }

    void makesUnderlinedNodes() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        const QList<QPointF> outline = std::get<Stroke>(layer.elements[a]).points;
        write(layer, QRectF(300, 200, 60, 20), 3);

        // From the node down to the word, and along under it
        const QList<QPointF> drawn = handDrawn({{150, 125}, {280, 200}, {295, 226}, {370, 228}}, 3, 0.8);
        Result result = MindMap::recognize(strokeOf(drawn), layer);
        QCOMPARE(result.kind, Result::Kind::Underline);
        QCOMPARE(result.from, std::optional<size_t>(a));
        QCOMPARE(result.contents, range(a + 1, 3));
        // A line as long as the word, just under it, joined to the node at the end near it
        const QList<QPointF>& path = result.stroke.points;
        QVERIFY(distanceToOutline(path.first(), outline) < 0.5);
        const QRectF word = QPolygonF(drawn).boundingRect();
        QVERIFY(word.isValid());
        const QPointF nearEnd = path[path.size() - 2];
        const QPointF farEnd = path.last();
        QCOMPARE(nearEnd.y(), farEnd.y());
        QVERIFY(farEnd.x() > nearEnd.x());
        QVERIFY(nearEnd.y() > 220 && nearEnd.y() < 226);
        QVERIFY(nearEnd.x() > 290 && nearEnd.x() < 300);
        QVERIFY(farEnd.x() > 360 && farEnd.x() < 370);
        // The branch to it tapers, the line keeps its width
        QVERIFY(result.stroke.widths.first() > result.stroke.widths.last());
        QCOMPARE(result.stroke.widths.last(), 1.41);
        QVERIFY(std::abs(result.stroke.widths[result.stroke.widths.size() - 2] - 1.41) < 0.05);

        const size_t b = add(layer, result.stroke);
        const std::vector<MindMap::Node> nodes = MindMap::findNodes(layer);
        QCOMPARE(nodes.size(), size_t(2));
        QCOMPARE(nodes[1].kind, MindMap::Node::Kind::Underline);
        QCOMPARE(nodes[1].element, b);
        QCOMPARE(nodes[1].contents, range(a + 1, 3));
        QCOMPARE(nodes[1].outline.last(), farEnd);

        // A branch from the end of the line
        result = MindMap::recognize(strokeOf(handDrawn({{372, 222}, {450, 300}}, 3, 0.8)), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.from, std::optional<size_t>(b));
        QCOMPARE(result.stroke.points.first(), farEnd);
        const size_t c = add(layer, result.stroke);
        // Started a little past the end, or on the line before it: also from its end
        for (const QPointF& start: {farEnd + QPointF(10, 2), farEnd - QPointF(15, 3)}) {
            result = MindMap::recognize(strokeOf(handDrawn({start, {450, 120}}, 3, 0.8)), layer);
            QCOMPARE(result.kind, Result::Kind::Branch);
            QCOMPARE(result.stroke.points.first(), farEnd);
        }

        // A line under writing at the free end of that branch, drawn towards the branch
        write(layer, QRectF(460, 283, 60, 20), 3);
        result = MindMap::recognize(strokeOf(handDrawn({{525, 307}, {453, 305}}, 3, 0.8)), layer);
        QCOMPARE(result.kind, Result::Kind::Underline);
        QCOMPARE(result.from, std::optional<size_t>(c));
        QCOMPARE(result.stroke.points.first(), QPointF(450, 300));
        QVERIFY(result.stroke.points.last().x() > 515);
    }

    void makesNodesOfEmptyLoops() {
        Layer layer;
        const QList<QPointF> drawn = loop({200, 150}, 60, 30);
        const Result empty = MindMap::recognize(strokeOf(drawn), layer);
        QCOMPARE(empty.kind, Result::Kind::Node);
        QVERIFY(empty.contents.empty());
        // A clean ellipse where the loop was drawn
        const QRectF drawnBounds = QPolygonF(drawn).boundingRect();
        const QRectF bounds = QPolygonF(empty.stroke.points).boundingRect();
        QVERIFY(std::abs(bounds.width() - drawnBounds.width()) < 1);
        QVERIFY(std::abs(bounds.height() - drawnBounds.height()) < 1);
        const size_t node = add(layer, empty.stroke);
        const size_t branch =
                add(layer, MindMap::recognize(strokeOf(handDrawn({{200, 172}, {200, 300}}, 3, 0.8)), layer).stroke);
        QCOMPARE(MindMap::findNodes(layer).size(), size_t(1));

        // Writing that fits: nothing changes
        const Stroke letter = strokeOf(handDrawn({{180, 160}, {188, 140}, {196, 160}}, 2, 0.3));
        Result result = MindMap::recognize(letter, layer);
        QCOMPARE(result.kind, Result::Kind::Ink);
        QVERIFY(result.changes.empty());
        add(layer, letter);

        // Writing that sticks out: the node grows around all of it, and the branch starts at its new outline
        const Stroke word = strokeOf(handDrawn({{200, 160}, {240, 140}, {290, 160}}, 2, 0.3));
        result = MindMap::recognize(word, layer);
        QCOMPARE(result.kind, Result::Kind::Ink);
        QCOMPARE(result.changes.size(), size_t(2));
        QCOMPARE(result.changes[0].first, node);
        const QList<QPointF> grown = result.changes[0].second.points;
        for (const Stroke& stroke: {letter, word}) {
            for (const QPointF& p: stroke.points) {
                QVERIFY(QPolygonF(grown).containsPoint(p, Qt::OddEvenFill));
            }
        }
        QVERIFY(QPolygonF(grown).boundingRect().adjusted(-1, -1, 1, 1).contains(bounds));
        QCOMPARE(result.changes[1].first, branch);
        const Stroke& cut = result.changes[1].second;
        QVERIFY(distanceToOutline(cut.points.first(), grown) < 0.5);
        QCOMPARE(cut.points.last(), std::get<Stroke>(layer.elements[branch]).points.last());
        QCOMPARE(cut.widths.size(), cut.points.size() - 1);
    }

    void joinsBranchesToNodesDrawnLater() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        auto branch = [&layer](const QList<QPointF>& corners) {
            return add(layer, MindMap::recognize(strokeOf(handDrawn(corners, 3, 1.0)), layer).stroke);
        };
        const size_t first = branch({{170, 125}, {300, 300}});
        const size_t second = branch({{110, 125}, {100, 300}});
        const QPointF start = std::get<Stroke>(layer.elements[first]).points.first();

        // A loop around writing at the free end of a branch
        write(layer, QRectF(290, 295, 60, 20), 3);
        Result result = MindMap::recognize(strokeOf(loop({320, 305}, 60, 30)), layer);
        QCOMPARE(result.kind, Result::Kind::Node);
        QCOMPARE(result.contents.size(), size_t(3));
        QCOMPARE(result.changes.size(), size_t(1));
        QCOMPARE(result.changes[0].first, first);
        const Stroke& joined = result.changes[0].second;
        QCOMPARE(joined.points.first(), start);
        QVERIFY(distanceToOutline(joined.points.last(), result.stroke.points) < 0.5);
        // Not thin at its end any more: it goes to a node
        QVERIFY(std::abs(joined.widths.last() - 1.41) < 0.1);

        // An empty loop near the free end of the other one
        result = MindMap::recognize(strokeOf(loop({100, 330}, 40, 25)), layer);
        QCOMPARE(result.kind, Result::Kind::Node);
        QCOMPARE(result.changes.size(), size_t(1));
        QCOMPARE(result.changes[0].first, second);
        QVERIFY(distanceToOutline(result.changes[0].second.points.last(), result.stroke.points) < 0.5);
    }

    void makesCrossLinks() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 250, 60, 20), 3);
        const size_t b = add(layer, MindMap::recognize(strokeOf(loop({330, 260}, 60, 30)), layer).stroke);
        const QList<QPointF> outlineA = std::get<Stroke>(layer.elements[a]).points;
        const QList<QPointF> outlineB = std::get<Stroke>(layer.elements[b]).points;

        // Drawn with a hook at its end, as its head
        Result result = MindMap::recognize(strokeOf(handDrawn({{150, 120}, {315, 248}, {300, 236}}, 3, 0.8)), layer);
        QCOMPARE(result.kind, Result::Kind::CrossLink);
        QCOMPARE(result.from, std::optional<size_t>(a));
        QCOMPARE(result.to, std::optional<size_t>(b));
        const QList<QPointF>& link = result.stroke.points;
        QVERIFY(MindMap::hasArrowHead(link));
        QVERIFY(distanceToOutline(link.first(), outlineA) < 0.5);
        QVERIFY(distanceToOutline(link.last(), outlineB) < 0.5);
        QVERIFY(!result.stroke.hasPressure());
        // The legs of the head point back along the line
        const QPointF tip = link.last();
        for (const QPointF& leg: {link[link.size() - 2], link[link.size() - 4]}) {
            QVERIFY(leg.x() < tip.x() && leg.y() < tip.y());
        }

        // A branch, and a "V" drawn at its end afterwards
        const size_t c =
                add(layer, MindMap::recognize(strokeOf(handDrawn({{180, 130}, {320, 240}}, 3, 0.8)), layer).stroke);
        const QList<QPointF> line = std::get<Stroke>(layer.elements[c]).points;
        const QPointF end = line.last();
        const QPointF direction = end - line[line.size() - 4];
        const double angle = std::atan2(direction.y(), direction.x());
        auto leg = [&](double sign) {
            return end - 12 * QPointF(std::cos(angle + sign * M_PI / 6), std::sin(angle + sign * M_PI / 6));
        };
        result = MindMap::recognize(strokeOf(handDrawn({leg(1), end, leg(-1)}, 1.5, 0.3)), layer);
        QCOMPARE(result.kind, Result::Kind::ArrowHead);
        QVERIFY(!result.keep);
        QCOMPARE(result.to, std::optional<size_t>(c));
        QCOMPARE(result.changes.size(), size_t(1));
        QCOMPARE(result.changes[0].first, c);
        const Stroke& arrow = result.changes[0].second;
        QVERIFY(MindMap::hasArrowHead(arrow.points));
        QCOMPARE(arrow.points.last(), end);
        QCOMPARE(arrow.widths.size(), arrow.points.size() - 1);

        // Opening the other way, or far from any line: not a head
        const QPointF forward = 2 * direction / 3;
        const Stroke backwards = strokeOf(handDrawn({leg(1) + forward, end, leg(-1) + forward}, 1.5, 0.3));
        QVERIFY(MindMap::recognize(backwards, layer).kind != Result::Kind::ArrowHead);
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{500, 490}, {510, 510}, {520, 490}}, 1.5, 0.3)), layer).kind,
                 Result::Kind::Ink);
    }

    void joinsLinesUnderWritingLater() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 200, 60, 20), 3);
        // A line under the word, on its own: it stays as it is
        const Stroke underline = strokeOf(handDrawn({{297, 226}, {365, 228}}, 3, 0.8));
        QCOMPARE(MindMap::recognize(underline, layer).kind, Result::Kind::Ink);
        const size_t u = add(layer, underline);

        // A branch to its start: the line is made as long as the word, and joined
        Result result = MindMap::recognize(strokeOf(handDrawn({{150, 125}, {270, 200}, {294, 224}}, 3, 0.8)), layer);
        QCOMPARE(result.kind, Result::Kind::Branch);
        QCOMPARE(result.to, std::optional<size_t>(u));
        QCOMPARE(result.changes.size(), size_t(1));
        QCOMPARE(result.changes[0].first, u);
        const QList<QPointF>& line = result.changes[0].second.points;
        QCOMPARE(line.size(), 2);
        QCOMPARE(line[0].y(), line[1].y());
        QVERIFY(line[0].x() < line[1].x());
        QVERIFY(line[0].x() > 290 && line[0].x() < 300 && line[1].x() > 360 && line[1].x() < 370);
        QCOMPARE(result.stroke.points.last(), line[0]);
    }

    void smoothsLoops() {
        // Drawn round once and a bit more: closed, round once, near the drawing
        const QList<QPointF> drawn = loop({200, 150}, 90, 50, 1.15);
        const QList<QPointF> smooth = MindMap::smoothLoop(drawn);
        QCOMPARE(smooth.first(), smooth.last());
        QVERIFY(MindMap::isLoop(smooth));
        for (const QPointF& p: smooth) {
            const double r = std::hypot((p.x() - 200) / 90, (p.y() - 150) / 50);
            QVERIFY2(std::abs(r - 1) < 0.06, qPrintable(QStringLiteral("%1 %2").arg(p.x()).arg(p.y())));
        }
        // With a gap: closed over it
        const QList<QPointF> open = MindMap::smoothLoop(loop({200, 150}, 90, 50, 0.9));
        QCOMPARE(open.first(), open.last());
    }

    void makesGroups() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 250, 60, 20), 3);
        const size_t b = add(layer, MindMap::recognize(strokeOf(loop({330, 260}, 60, 30)), layer).stroke);
        const size_t branch =
                add(layer, MindMap::recognize(strokeOf(handDrawn({{180, 130}, {320, 240}}, 3, 0.8)), layer).stroke);

        Stroke drawn = strokeOf(loop({235, 185}, 190, 130, 1.1));
        drawn.color = Qt::red;
        const Result result = MindMap::recognize(drawn, layer);
        QCOMPARE(result.kind, Result::Kind::Group);
        QCOMPARE(result.contents, range(0, layer.elements.size()));
        QCOMPARE(result.stroke.color, QColor(Qt::red));
        QCOMPARE(result.stroke.points.first(), result.stroke.points.last());
        // All that is in it stays in it
        const QPolygonF outline(result.stroke.points);
        for (const Element& element: layer.elements) {
            for (const QPointF& p: std::get<Stroke>(element).points) {
                QVERIFY(outline.containsPoint(p, Qt::OddEvenFill));
            }
        }

        // Chosen, it brings all that is in it
        const size_t group = add(layer, result.stroke);
        QCOMPARE(MindMap::withContents(layer, {group}), range(0, layer.elements.size()));
        // A node brings its writing, not the branch or the other node
        std::vector<size_t> node = range(0, 4);
        node.push_back(a);
        QCOMPARE(MindMap::withContents(layer, {a}), node);
        QCOMPARE(MindMap::withContents(layer, {branch}), std::vector<size_t>{branch});
        QCOMPARE(MindMap::findNodes(layer).size(), size_t(2));
        Q_UNUSED(b);
    }

    void branchesFollowMovedNodes() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const size_t a = add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 250, 60, 20), 3);
        const size_t b = add(layer, MindMap::recognize(strokeOf(loop({330, 260}, 60, 30)), layer).stroke);
        const size_t branch =
                add(layer, MindMap::recognize(strokeOf(handDrawn({{180, 130}, {320, 240}}, 3, 0.8)), layer).stroke);
        const size_t link = add(
                layer,
                MindMap::recognize(strokeOf(handDrawn({{110, 125}, {200, 300}, {300, 268}, {288, 260}}, 3, 0.8)), layer)
                        .stroke);
        QVERIFY(MindMap::hasArrowHead(std::get<Stroke>(layer.elements[link]).points));
        write(layer, QRectF(470, 330, 60, 18), 3);
        const size_t under = add(
                layer, MindMap::recognize(strokeOf(handDrawn({{350, 270}, {440, 340}, {465, 352}, {535, 353}}, 3, 0.8)),
                                          layer)
                               .stroke);
        QCOMPARE(MindMap::findNodes(layer).size(), size_t(3));
        const Layer before = layer;
        auto points = [](const Layer& l, size_t i) { return std::get<Stroke>(l.elements[i]).points; };

        // The second node is moved with its writing
        const std::vector<size_t> node = MindMap::withContents(before, {b});
        QCOMPARE(node.size(), size_t(4));
        const QPointF offset(160, -120);
        Layer after = moved(before, node, offset);
        const auto changes = MindMap::follow(before, after, node);
        std::map<size_t, QList<QPointF>> changed;
        for (const auto& [i, stroke]: changes) {
            changed[i] = stroke.points;
        }
        QCOMPARE(changed.size(), size_t(3));
        // Bent, not torn away: every point stays near where the line was and where its ends are now
        for (const auto& [i, line]: changed) {
            const QRectF area = (QPolygonF(points(before, i)).boundingRect() |
                                 QPolygonF(points(after, b)).boundingRect())
                                        .adjusted(-80, -80, 80, 80);
            for (const QPointF& p: line) {
                QVERIFY(area.contains(p));
            }
        }
        const QList<QPointF> outlineA = points(before, a);
        const QList<QPointF> outlineB = points(after, b);
        // The branch: from where it was at the first node to the new outline of the second
        QCOMPARE(changed[branch].first(), points(before, branch).first());
        QVERIFY(distanceToOutline(changed[branch].last(), outlineB) < 0.5);
        // The cross link keeps its head, at the new outline
        QVERIFY(MindMap::hasArrowHead(changed[link]));
        QCOMPARE(changed[link].first(), points(before, link).first());
        QVERIFY(distanceToOutline(changed[link].last(), outlineB) < 0.5);
        // The underlined node stays; the curve to it starts at the new outline
        QVERIFY(distanceToOutline(changed[under].first(), outlineB) < 0.5);
        QCOMPARE(changed[under].last(), points(before, under).last());
        QCOMPARE(changed[under][changed[under].size() - 2], points(before, under)[points(before, under).size() - 2]);
        Q_UNUSED(outlineA);

        // The underlined node is moved with its writing: the curve to it still starts at its parent
        const std::vector<size_t> underlined = MindMap::withContents(before, {under});
        QCOMPARE(underlined.size(), size_t(4));
        after = moved(before, underlined, QPointF(20, 50));
        const auto lineChanges = MindMap::follow(before, after, underlined);
        QCOMPARE(lineChanges.size(), size_t(1));
        QCOMPARE(lineChanges[0].first, under);
        const QList<QPointF>& line = lineChanges[0].second.points;
        QVERIFY(distanceToOutline(line.first(), points(before, b)) < 0.5);
        QCOMPARE(line.last(), points(before, under).last() + QPointF(20, 50));

        // Moving all of them together changes nothing else
        after = moved(before, range(0, before.elements.size()), offset);
        QVERIFY(MindMap::follow(before, after, range(0, before.elements.size())).empty());
    }

    void followsTheSettings() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        const QList<QPointF> box = handDrawn({{80, 85}, {200, 87}, {201, 135}, {79, 134}, {81, 88}}, 4, 0.7);
        MindMap::Options options;
        options.nodeShape = MindMap::Options::NodeShape::Ellipse;
        QVERIFY(MindMap::recognize(strokeOf(box), layer, options).stroke.points.size() > 24);
        options.nodeShape = MindMap::Options::NodeShape::Rectangle;
        QCOMPARE(MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer, options).stroke.points.size(), 5);

        // Straight branches
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        options = MindMap::Options();
        options.curved = false;
        const Result straight =
                MindMap::recognize(strokeOf(handDrawn({{150, 120}, {260, 160}, {300, 235}}, 3, 1.0)), layer, options);
        QCOMPARE(straight.kind, Result::Kind::Branch);
        const QList<QPointF>& path = straight.stroke.points;
        for (const QPointF& p: path) {
            QVERIFY(distanceToOutline(p, {path.first(), path.last()}) < 1e-6);
        }
    }

    void colorsMainBranches() {
        MindMap::Options options;
        options.branchColors = true;
        const QList<QColor> colors = MindMap::branchColors();
        Layer layer;
        auto apply = [&](const QList<QPointF>& points) {
            const Result result = MindMap::recognize(strokeOf(points), layer, options);
            for (const auto& [i, changed]: result.changes) {
                layer.elements[i] = changed;
            }
            add(layer, result.stroke);
            return result;
        };
        write(layer, QRectF(100, 100, 80, 20));
        apply(loop({140, 110}, 70, 30));

        // Each main branch has a colour of its own
        QCOMPARE(apply(handDrawn({{170, 125}, {300, 300}}, 3, 1.0)).stroke.color, colors[0]);
        QCOMPARE(apply(handDrawn({{110, 125}, {60, 300}}, 3, 1.0)).stroke.color, colors[1]);
        // What grows from a node at the end of one takes its colour
        write(layer, QRectF(290, 295, 60, 20), 3);
        apply(loop({320, 305}, 60, 30));
        QCOMPARE(apply(handDrawn({{340, 320}, {420, 420}}, 3, 1.0)).stroke.color, colors[0]);
        write(layer, QRectF(200, 400, 60, 20), 3);
        const Result underline = apply(handDrawn({{300, 320}, {250, 400}, {195, 426}, {265, 427}}, 3, 0.8));
        QCOMPARE(underline.kind, Result::Kind::Underline);
        QCOMPARE(underline.stroke.color, colors[0]);
        // Without the setting the colour of the tool
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{150, 90}, {150, 20}}, 3, 1.0)), layer).stroke.color,
                 QColor(Qt::black));
    }

    void erasesByScribbling() {
        // Back and forth: a scribble; writing goes on in one direction
        QList<QPointF> zigzag;
        for (int i = 0; i <= 8; ++i) {
            zigzag.append(QPointF(i % 2 == 0 ? 200 : 260, 200 + 4 * i));
        }
        const QList<QPointF> scribbled = handDrawn(zigzag, 3, 0.5);
        QVERIFY(MindMap::isScribble(scribbled));
        QList<QPointF> m;
        for (int i = 0; i <= 8; ++i) {
            m.append(QPointF(200 + 10 * i, i % 2 == 0 ? 220 : 200));
        }
        QVERIFY(!MindMap::isScribble(handDrawn(m, 2, 0.3)));
        QVERIFY(!MindMap::isScribble(loop({200, 150}, 60, 30)));
        QVERIFY(!MindMap::isScribble(handDrawn({{0, 0}, {200, 10}}, 3, 0.5)));

        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        const size_t branch =
                add(layer, MindMap::recognize(strokeOf(handDrawn({{170, 125}, {230, 230}}, 3, 1.0)), layer).stroke);
        // Over the branch: it goes, and the scribble too
        Result result = MindMap::recognize(strokeOf(scribbled), layer);
        QCOMPARE(result.kind, Result::Kind::Erase);
        QVERIFY(!result.keep);
        QCOMPARE(result.removed, std::vector<size_t>{branch});
        // Over two letters
        QList<QPointF> overLetters;
        for (int i = 0; i <= 8; ++i) {
            overLetters.append(QPointF(i % 2 == 0 ? 102 : 138, 104 + 1.5 * i));
        }
        result = MindMap::recognize(strokeOf(handDrawn(overLetters, 2, 0.3)), layer);
        QCOMPARE(result.kind, Result::Kind::Erase);
        QCOMPARE(result.removed, range(0, 2));
        // Over nothing, or with the setting off: ink
        QList<QPointF> elsewhere = scribbled;
        for (QPointF& p: elsewhere) {
            p += QPointF(200, 200);
        }
        QVERIFY(MindMap::recognize(strokeOf(elsewhere), layer).kind != Result::Kind::Erase);
        MindMap::Options options;
        options.scribbleErases = false;
        QVERIFY(MindMap::recognize(strokeOf(scribbled), layer, options).kind != Result::Kind::Erase);
    }

    void leavesUnderliningAlone() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);
        write(layer, QRectF(300, 200, 60, 20), 3);

        // Under the word, but not joined to anything
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{295, 226}, {370, 227}}, 3, 0.8)), layer).kind,
                 Result::Kind::Ink);
        // Under the writing of the node: writing in the node
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{105, 125}, {178, 126}}, 3, 0.5)), layer).kind,
                 Result::Kind::Ink);
        // From the node, but far below the word: a branch
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{150, 125}, {280, 260}, {370, 262}}, 3, 0.8)), layer).kind,
                 Result::Kind::Branch);
    }

    void leavesWritingNearNodesAlone() {
        Layer layer;
        write(layer, QRectF(100, 100, 80, 20));
        add(layer, MindMap::recognize(strokeOf(loop({140, 110}, 70, 30)), layer).stroke);

        // Far from any node
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{400, 400}, {500, 450}}, 3, 1.0)), layer).kind,
                 Result::Kind::Ink);
        // Writing put into the node
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{105, 125}, {175, 127}}, 3, 0.5)), layer).kind,
                 Result::Kind::Ink);
        // A short stroke next to it
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{215, 110}, {225, 100}}, 2, 0.3)), layer).kind,
                 Result::Kind::Ink);
        // A letter that winds, starting at the node
        QList<QPointF> winding;
        for (int i = 0; i <= 120; ++i) {
            winding.append(QPointF(212 + i * 0.4, 110 + 12 * std::sin(i / 4.0)));
        }
        QCOMPARE(MindMap::recognize(strokeOf(winding), layer).kind, Result::Kind::Ink);
        // Underlining the writing outside the node, without a node at either end
        QCOMPARE(MindMap::recognize(strokeOf(handDrawn({{300, 150}, {380, 151}}, 3, 0.5)), layer).kind,
                 Result::Kind::Ink);
    }
};

QTEST_MAIN(TestMindMap)
#include "tst_mindmap.moc"
