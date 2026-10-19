#include "MindMap.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

#include <QLineF>

#include "Renderer.h"

namespace {

using MindMap::Node;

constexpr int LOOP_MIN_POINTS = 8;
constexpr double LOOP_MIN_TURN = 1.75 * M_PI;  // how far a loop goes round its centre at least
constexpr double LOOP_MAX_TURN = 3.2 * M_PI;   // more is a spiral or a scribble
constexpr double LOOP_MAX_GAP = 0.3;           // between its ends, of the diagonal, if it goes round less than once
constexpr double RECTANGLE_MIN_FILL = 0.87;    // of its bounds a rectangle covers (an ellipse: 0.785)
constexpr double RECTANGLE_MAX_CORNER = 0.08;  // distance of a corner of the bounds to the loop, of the diagonal
constexpr double INSIDE_MIN_SHARE = 0.8;       // of the points of an element that are inside a loop around it
constexpr double BRANCH_MIN_STRAIGHTNESS = 0.5;  // distance of the ends of a branch to its length
constexpr int MAX_SAMPLES = 64;

constexpr double STRAIGHT_TOLERANCE = 3;           // how far a straight line drawn by hand strays from it
constexpr double STRAIGHT_TOLERANCE_SHARE = 0.05;  // or of its length
constexpr double PATH_STEP = 4;                    // between the points of a smooth curve
constexpr double TAPER_START = 1.5;  // width at the node a branch comes from, of the width of the tool
constexpr double TAPER_END = 0.5;    // at a free end
constexpr double TAPER_NODE = 1.0;   // at the node it goes to, and along the line under writing

constexpr double UNDERLINE_BAND = 6;  // how far the line under writing goes up and down
constexpr double UNDERLINE_MIN_LENGTH = 20;
constexpr double UNDERLINE_MAX_WINDING = 1.3;  // its length to its width: it does not go back and forth
constexpr double UNDERLINE_SLACK = 5;          // how far writing may stick out at the ends of the line
constexpr double UNDERLINE_MIN_OVERLAP = 0.6;  // of each element of the writing, over the line
constexpr double UNDERLINE_MIN_COVER = 0.5;    // of the writing the line is under
constexpr double UNDERLINE_BELOW = 4;          // how far the writing may reach below the line
constexpr double UNDERLINE_MAX_GAP = 10;       // between the writing and the line, at least
constexpr double UNDERLINE_GAP_SHARE = 0.6;    // or of the height of the writing
constexpr double END_TOLERANCE = 1.5;          // how far the end of a branch made here is from where it joins

constexpr double ARROW_ANGLE = M_PI / 6;   // between a leg of an arrow head and the line, as in Xournal++
constexpr double ARROW_LEG = 5;            // the length of a leg, in widths of the line
constexpr double ARROW_MIN_LEG = 6;        // in points
constexpr double ARROW_MAX_LEG_SHARE = 0.4;  // of the line
constexpr double ARROW_DIRECTION = 15;     // how far back along the line its direction at the tip is taken
constexpr double HOOK_MIN_LENGTH = 6;      // of the part of a stroke that goes back from its tip, drawn as a head
constexpr double HOOK_MAX_LENGTH = 45;
constexpr double HOOK_MAX_SHARE = 0.4;     // of the line
constexpr double HOOK_MAX_COS = -0.34;     // it turns back by more than 110°
constexpr double V_MIN_LEG = 4;            // a head drawn afterwards: its legs
constexpr double V_MAX_LEG = 45;
constexpr double V_MAX_RATIO = 3;          // of the longer leg to the shorter one
constexpr double V_STRAIGHTNESS = 0.25;    // how far the points of a leg stray from it, of its length
constexpr double V_MIN_ANGLE = 15 * M_PI / 180;
constexpr double V_MAX_ANGLE = 130 * M_PI / 180;
constexpr double V_MIN_COS = 0.5;          // it opens back along the line, within 60°
constexpr double GROW_INSIDE_SHARE = 0.5;  // of the points of writing put into a node
constexpr int GROUP_POINTS = 128;          // of the smooth curve of a group
constexpr int GROUP_HARMONICS = 6;         // the waves of the loop drawn that it keeps
constexpr double GROUP_WIDEN = 1.05;       // how much it is widened in a step if it cuts what it is around
constexpr int GROUP_WIDEN_STEPS = 8;
constexpr int SCRIBBLE_MIN_POINTS = 12;
constexpr int SCRIBBLE_MIN_TURNS = 4;           // how often it goes back along its main direction, at least
constexpr double SCRIBBLE_MIN_EXTENT = 10;      // along its main direction
constexpr double SCRIBBLE_HYSTERESIS = 0.15;    // a turn is a way back of this share of the extent, at least
constexpr double SCRIBBLE_MIN_HYSTERESIS = 3;
constexpr double SCRIBBLE_MIN_COVER = 0.3;      // of the points of the scribble over a text or an image it deletes
constexpr double CLOSED_EXACTLY = 1e-3;         // between the ends of an outline the tool made, at most

QRectF boundsOf(const QList<QPointF>& points) {
    if (points.isEmpty()) {
        return QRectF();
    }
    double minX = points.first().x(), maxX = minX, minY = points.first().y(), maxY = minY;
    for (const QPointF& p: points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    return QRectF(QPointF(minX, minY), QPointF(maxX, maxY));
}

double diagonal(const QRectF& rect) { return std::hypot(rect.width(), rect.height()); }

double length(const QList<QPointF>& points) {
    double sum = 0;
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        sum += QLineF(points[i], points[i + 1]).length();
    }
    return sum;
}

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF d = b - a;
    const double len2 = QPointF::dotProduct(d, d);
    const double t = len2 > 0 ? std::clamp(QPointF::dotProduct(p - a, d) / len2, 0.0, 1.0) : 0.0;
    return QLineF(p, a + d * t).length();
}

double distanceToOutline(const QPointF& p, const QPolygonF& outline) {
    double best = std::numeric_limits<double>::max();
    for (qsizetype i = 0; i + 1 < outline.size(); ++i) {
        best = std::min(best, distanceToSegment(p, outline[i], outline[i + 1]));
    }
    return best;
}

/// Points of an element for telling whether it is inside a loop
QList<QPointF> samples(const Element& element) {
    if (const auto* stroke = std::get_if<Stroke>(&element)) {
        const qsizetype step = std::max<qsizetype>(1, stroke->points.size() / MAX_SAMPLES);
        QList<QPointF> points;
        for (qsizetype i = 0; i < stroke->points.size(); i += step) {
            points.append(stroke->points[i]);
        }
        return points;
    }
    const QRectF rect = Renderer::elementBounds(element);
    if (rect.isNull()) {
        return {};
    }
    return {rect.topLeft(), rect.topRight(), rect.bottomLeft(), rect.bottomRight(), rect.center()};
}

bool encloses(const QPolygonF& loop, const QRectF& loopBounds, const Element& element) {
    if (!loopBounds.intersects(Renderer::elementBounds(element))) {
        return false;
    }
    const QList<QPointF> points = samples(element);
    if (points.isEmpty()) {
        return false;
    }
    int inside = 0;
    for (const QPointF& p: points) {
        inside += loop.containsPoint(p, Qt::WindingFill) ? 1 : 0;
    }
    return inside >= INSIDE_MIN_SHARE * static_cast<double>(points.size());
}

/// The elements of the layer inside the loop, without the one at skip
std::vector<size_t> enclosed(const QPolygonF& loop, const Layer& layer, std::optional<size_t> skip) {
    const QRectF loopBounds = loop.boundingRect();
    std::vector<size_t> result;
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        if (i != skip && encloses(loop, loopBounds, layer.elements[i])) {
            result.push_back(i);
        }
    }
    return result;
}

QRectF writingBounds(const Layer& layer, const std::vector<size_t>& contents) {
    QRectF writing;
    for (const size_t i: contents) {
        writing |= Renderer::elementBounds(layer.elements[i]);
    }
    return writing;
}

/// The end of a stroke that runs along a horizontal line
struct Tail {
    qsizetype first = 0;  ///< the points from here to the last one
    double y = 0;
    double left = 0;
    double right = 0;
};

std::optional<Tail> horizontalTail(const QList<QPointF>& points) {
    if (points.size() < 2) {
        return std::nullopt;
    }
    qsizetype first = points.size() - 1;
    double minY = points.last().y();
    double maxY = minY;
    while (first > 0) {
        const double y = points[first - 1].y();
        if (std::max(maxY, y) - std::min(minY, y) > UNDERLINE_BAND) {
            break;
        }
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
        --first;
    }
    const QList<QPointF> tail = points.mid(first);
    const QRectF bounds = boundsOf(tail);
    if (bounds.width() < UNDERLINE_MIN_LENGTH || length(tail) > UNDERLINE_MAX_WINDING * bounds.width()) {
        return std::nullopt;
    }
    double sum = 0;
    for (const QPointF& p: tail) {
        sum += p.y();
    }
    return Tail{first, sum / static_cast<double>(tail.size()), bounds.left(), bounds.right()};
}

/// The writing just above the tail, of the elements that are not taken by other parts of the mind map
std::vector<size_t> writingAbove(const Tail& tail, const Layer& layer, const std::vector<bool>& taken,
                                 std::optional<size_t> skip) {
    std::vector<size_t> result;
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        if (i == skip || taken[i]) {
            continue;
        }
        const QRectF b = Renderer::elementBounds(layer.elements[i]);
        if (b.isEmpty()) {
            continue;
        }
        const double overlap =
                std::min(b.right(), tail.right + UNDERLINE_SLACK) - std::max(b.left(), tail.left - UNDERLINE_SLACK);
        const double maxGap = std::max(UNDERLINE_MAX_GAP, UNDERLINE_GAP_SHARE * b.height());
        if (overlap >= UNDERLINE_MIN_OVERLAP * b.width() && b.bottom() <= tail.y + UNDERLINE_BELOW &&
            b.bottom() >= tail.y - maxGap && b.top() < tail.y) {
            result.push_back(i);
        }
    }
    if (result.empty()) {
        return result;
    }
    // A line under a few letters of a longer word is not under the word
    const QRectF writing = writingBounds(layer, result);
    const double cover = std::min(writing.right(), tail.right) - std::max(writing.left(), tail.left);
    if (cover < UNDERLINE_MIN_COVER * writing.width()) {
        result.clear();
    }
    return result;
}

/// A free end of a branch
struct BranchEnd {
    size_t element = 0;
    QPointF point;
};

/// The parts of the mind map on a layer
struct Analysis {
    std::vector<Node> nodes;
    std::vector<BranchEnd> branchEnds;
    std::vector<bool> structure;  ///< nodes and groups
    std::vector<bool> taken;      ///< nodes, groups, branches and the writing of nodes: not free writing
    std::vector<bool> branch;     ///< branches and cross links: strokes with an end where they join a node
    std::vector<std::pair<size_t, std::vector<size_t>>> groups;  ///< loops around nodes, with all that is in them
};

/// Whether the point is where a branch joins the node: on its outline, or at the end of its line
bool joins(const Node& node, const QPointF& p, double tolerance) {
    if (node.kind == Node::Kind::Underline) {
        return QLineF(p, node.outline.last()).length() <= tolerance;
    }
    return distanceToOutline(p, node.outline) <= tolerance;
}

/// What the tool makes of a loop ends where it begins; a drawn loop does not
bool closedExactly(const QPolygonF& points) {
    return points.size() > 2 && QLineF(points.first(), points.last()).length() < CLOSED_EXACTLY;
}

Analysis analyze(const Layer& layer, const MindMap::Options& options) {
    Analysis analysis;
    const size_t count = layer.elements.size();
    analysis.structure.assign(count, false);
    analysis.taken.assign(count, false);
    analysis.branch.assign(count, false);
    std::vector<bool> loop(count, false);

    // The smaller loops first, so that whether a loop is a node or a group is known before the loops around it
    std::vector<size_t> loops;
    for (size_t i = 0; i < count; ++i) {
        const auto* stroke = std::get_if<Stroke>(&layer.elements[i]);
        if (stroke && MindMap::isLoop(stroke->points)) {
            loops.push_back(i);
            loop[i] = true;
        }
    }
    auto area = [&layer](size_t i) {
        const QRectF rect = boundsOf(std::get<Stroke>(layer.elements[i]).points);
        return rect.width() * rect.height();
    };
    std::stable_sort(loops.begin(), loops.end(), [&area](size_t a, size_t b) { return area(a) < area(b); });

    for (const size_t i: loops) {
        const QPolygonF outline(std::get<Stroke>(layer.elements[i]).points);
        std::vector<size_t> contents = enclosed(outline, layer, i);
        // An empty loop is a node only as the tool made it, closed exactly: as it was drawn it is writing, the "o" of
        // a word (the canvas keeps it so while the word goes on)
        if (contents.empty() &&
            (diagonal(outline.boundingRect()) < options.emptyNodeMinSize || !closedExactly(outline))) {
            continue;
        }
        analysis.structure[i] = true;
        analysis.taken[i] = true;
        const bool group = std::any_of(contents.begin(), contents.end(),
                                       [&analysis](size_t j) { return analysis.structure[j]; });
        if (group) {
            analysis.groups.emplace_back(i, std::move(contents));
        } else {
            for (const size_t j: contents) {
                analysis.taken[j] = true;
            }
            analysis.nodes.push_back(Node{Node::Kind::Outline, i, outline, std::move(contents)});
        }
    }

    // Lines under writing
    for (size_t i = 0; i < count; ++i) {
        const auto* stroke = std::get_if<Stroke>(&layer.elements[i]);
        if (!stroke || loop[i] || analysis.taken[i]) {
            continue;
        }
        const std::optional<Tail> tail = horizontalTail(stroke->points);
        if (!tail) {
            continue;
        }
        std::vector<size_t> contents = writingAbove(*tail, layer, analysis.taken, i);
        if (contents.empty()) {
            continue;
        }
        analysis.structure[i] = true;
        analysis.taken[i] = true;
        for (const size_t j: contents) {
            analysis.taken[j] = true;
        }
        const QPolygonF line{stroke->points[tail->first], stroke->points.last()};
        analysis.nodes.push_back(Node{Node::Kind::Underline, i, line, std::move(contents)});
    }

    // Branches: strokes with an end where a node is joined
    for (size_t i = 0; i < count; ++i) {
        const auto* stroke = std::get_if<Stroke>(&layer.elements[i]);
        if (!stroke || loop[i] || analysis.taken[i] || stroke->points.size() < 2) {
            continue;
        }
        const double tolerance = END_TOLERANCE + stroke->width;
        auto joined = [&analysis, tolerance](const QPointF& p) {
            return std::any_of(analysis.nodes.begin(), analysis.nodes.end(),
                               [&p, tolerance](const Node& node) { return joins(node, p, tolerance); });
        };
        const bool first = joined(stroke->points.first());
        const bool last = joined(stroke->points.last());
        if (first || last) {
            analysis.taken[i] = true;
            analysis.branch[i] = true;
        }
        if (first != last) {
            analysis.branchEnds.push_back({i, first ? stroke->points.last() : stroke->points.first()});
        }
    }

    std::sort(analysis.nodes.begin(), analysis.nodes.end(),
              [](const Node& a, const Node& b) { return a.element < b.element; });
    return analysis;
}

/// What the end of a stroke joins
struct Anchor {
    size_t element = 0;
    std::optional<size_t> node;  ///< the index of a node with an outline the stroke is cut at
    QPointF point;               ///< otherwise where the stroke starts or ends
    std::optional<size_t> line;  ///< the index of an underlined node the stroke ends at
};

/**
 * What the end of a stroke joins: a node it is inside, or the nearest outline, far half of the line of an underlined
 * node or (if branchEnds) free end of a branch it is close to. With incoming, for the end of a stroke, any part of
 * the line of an underlined node, joined at its nearer end
 */
std::optional<Anchor> anchorAt(const QPointF& p, const Analysis& analysis, double tolerance, bool branchEnds,
                               bool incoming = false) {
    std::optional<Anchor> found;
    double best = tolerance;
    for (size_t i = 0; i < analysis.nodes.size(); ++i) {
        const Node& node = analysis.nodes[i];
        if (node.kind == Node::Kind::Outline) {
            if (node.outline.containsPoint(p, Qt::WindingFill)) {
                return Anchor{node.element, i, {}};
            }
            const double distance = distanceToOutline(p, node.outline);
            if (distance <= best) {
                best = distance;
                found = Anchor{node.element, i, {}};
            }
        } else if (incoming) {
            const QPointF a = node.outline.first();
            const QPointF b = node.outline.last();
            const double distance = distanceToSegment(p, a, b);
            if (distance <= best) {
                best = distance;
                found = Anchor{node.element, std::nullopt, QLineF(p, a).length() < QLineF(p, b).length() ? a : b, i};
            }
        } else {
            // Anywhere along the far half of the line: its branches start at its end
            const QPointF middle = (node.outline.first() + node.outline.last()) / 2;
            const double distance = distanceToSegment(p, middle, node.outline.last());
            if (distance <= best) {
                best = distance;
                found = Anchor{node.element, std::nullopt, node.outline.last()};
            }
        }
    }
    if (branchEnds) {
        for (const BranchEnd& end: analysis.branchEnds) {
            const double distance = QLineF(p, end.point).length();
            if (distance <= best) {
                best = distance;
                found = Anchor{end.element, std::nullopt, end.point};
            }
        }
    }
    return found;
}

/// Where the line from a to b crosses the outline: the first crossing or the last one, a or b if there is none
QPointF crossing(const QPointF& a, const QPointF& b, const QPolygonF& outline, bool last) {
    const QLineF line(a, b);
    std::optional<double> found;
    for (qsizetype i = 0; i + 1 < outline.size(); ++i) {
        QPointF p;
        if (line.intersects(QLineF(outline[i], outline[i + 1]), &p) != QLineF::BoundedIntersection) {
            continue;
        }
        const double t = line.length() > 0 ? QLineF(a, p).length() / line.length() : 0.0;
        if (!found || (last ? t > *found : t < *found)) {
            found = t;
        }
    }
    if (!found) {
        return last ? a : b;
    }
    return line.pointAt(*found);
}

/// The path of a branch between its anchors, through the points drawn outside the nodes it joins
QList<QPointF> branchPath(const QList<QPointF>& drawn, const std::optional<Anchor>& from,
                          const std::optional<Anchor>& to, const Analysis& analysis, bool curved) {
    auto inside = [&analysis](const std::optional<Anchor>& anchor, const QPointF& p) {
        return anchor && anchor->node && analysis.nodes[*anchor->node].outline.containsPoint(p, Qt::WindingFill);
    };
    auto center = [&analysis](const Anchor& anchor) {
        return analysis.nodes[*anchor.node].outline.boundingRect().center();
    };
    QList<QPointF> outside;
    for (const QPointF& p: drawn) {
        if (!inside(from, p) && !inside(to, p)) {
            outside.append(p);
        }
    }

    QPointF start = drawn.first();
    QPointF end = drawn.last();
    if (from) {
        if (from->node) {
            const QPointF towards = !outside.isEmpty() ? outside.first() : to && to->node ? center(*to) : end;
            start = crossing(center(*from), towards, analysis.nodes[*from->node].outline, true);
        } else {
            start = from->point;
        }
    }
    if (to) {
        if (to->node) {
            const QPointF towards = !outside.isEmpty() ? outside.last() : from && from->node ? center(*from) : start;
            end = crossing(towards, center(*to), analysis.nodes[*to->node].outline, false);
        } else {
            end = to->point;
        }
    }
    return MindMap::smoothPath(start, curved ? outside : QList<QPointF>(), end);
}

/**
 * The colour of a branch that starts at the anchor, if main branches have colours of their own: that of the line
 * that leads to where it starts, or, at a node no line leads to, the first colour its other branches do not have
 */
std::optional<QColor> branchColor(const Anchor& from, const Layer& layer, const Analysis& analysis) {
    auto strokeAt = [&layer](size_t i) -> const Stroke& { return std::get<Stroke>(layer.elements[i]); };
    const auto node = std::find_if(analysis.nodes.begin(), analysis.nodes.end(),
                                   [&from](const Node& n) { return n.element == from.element; });
    if (node == analysis.nodes.end() || node->kind == Node::Kind::Underline) {
        return strokeAt(from.element).color;  // the end of a branch, or the line of an underlined node
    }
    // Lines that end at the node lead to it, lines that start there are its branches; cross links do not count
    QList<QColor> used;
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        if (!analysis.branch[i] && !analysis.structure[i]) {
            continue;
        }
        const auto* line = std::get_if<Stroke>(&layer.elements[i]);
        if (!line || i == node->element || line->points.size() < 2 || MindMap::hasArrowHead(line->points) ||
            MindMap::isLoop(line->points)) {
            continue;
        }
        const double tolerance = END_TOLERANCE + line->width;
        if (joins(*node, line->points.last(), tolerance)) {
            return line->color;
        }
        if (joins(*node, line->points.first(), tolerance)) {
            used.append(line->color);
        }
    }
    const QList<QColor> colors = MindMap::branchColors();
    for (const QColor& color: colors) {
        if (!used.contains(color)) {
            return color;
        }
    }
    return colors[used.size() % colors.size()];
}

/// Widths of the segments, going from the factor start to the factor end of the width along the points
QList<double> taper(const QList<QPointF>& points, double width, double start, double end) {
    const double total = length(points);
    QList<double> widths;
    double travelled = 0;
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const double segment = QLineF(points[i], points[i + 1]).length();
        const double t = total > 0 ? (travelled + segment / 2) / total : 0.0;
        widths.append(width * (start + (end - start) * t));
        travelled += segment;
    }
    return widths;
}

/// The width of the stroke: the average of its widths if it had pressure
double widthOf(const Stroke& stroke) {
    if (!stroke.hasPressure()) {
        return stroke.width;
    }
    double sum = 0;
    for (double width: stroke.widths) {
        sum += width;
    }
    return sum / static_cast<double>(stroke.widths.size());
}

/// The stroke with other points and widths: of one width if there are none
Stroke replaced(const Stroke& stroke, const QList<QPointF>& points, const QList<double>& widths = {}) {
    Stroke shape = stroke;
    shape.width = widthOf(stroke);
    shape.points = points;
    shape.widths = widths;
    shape.updateBounds();
    return shape;
}

/// The stroke as an underlined node, if it ends under writing and starts at a node or a branch
std::optional<MindMap::Result> underline(const Stroke& stroke, const QList<QPointF>& points, const Layer& layer,
                                         const Analysis& analysis, const MindMap::Options& options) {
    const std::optional<Tail> tail = horizontalTail(points);
    if (!tail) {
        return std::nullopt;
    }
    std::vector<size_t> contents = writingAbove(*tail, layer, analysis.taken, std::nullopt);
    if (contents.empty()) {
        return std::nullopt;
    }
    const std::optional<Anchor> from = anchorAt(points.first(), analysis, options.attachTolerance + stroke.width, true);
    if (!from) {
        return std::nullopt;
    }

    // The line is as long as the writing, and is joined at the end that is nearer to where it comes from
    const QRectF writing = writingBounds(layer, contents);
    const double y = writing.bottom() + options.underlineGap;
    QPointF nearEnd(writing.left() - options.underlineGap, y);
    QPointF farEnd(writing.right() + options.underlineGap, y);
    const QPointF origin = from->node ? analysis.nodes[*from->node].outline.boundingRect().center() : from->point;
    if (QLineF(origin, farEnd).length() < QLineF(origin, nearEnd).length()) {
        std::swap(nearEnd, farEnd);
    }
    // What was drawn before the line, to where the line begins
    QList<QPointF> head = points.mid(0, tail->first + 1);
    head.append(nearEnd);
    QList<QPointF> path = branchPath(head, from, Anchor{0, std::nullopt, nearEnd}, analysis, options.curved);
    const double width = widthOf(stroke);
    QList<double> widths;
    if (options.taper) {
        widths = taper(path, width, TAPER_START, TAPER_NODE);
        widths.append(width * TAPER_NODE);
    }
    path.append(farEnd);

    MindMap::Result result;
    result.kind = MindMap::Result::Kind::Underline;
    result.stroke = replaced(stroke, path, widths);
    if (options.branchColors) {
        result.stroke.color = branchColor(*from, layer, analysis).value_or(stroke.color);
    }
    result.contents = std::move(contents);
    result.from = from->element;
    return result;
}

using Changes = std::vector<std::pair<size_t, Stroke>>;

/// The rectangle of writing that outline() puts an outline of these bounds around
QRectF innerRect(MindMap::Shape shape, const QRectF& bounds, double margin) {
    double w = std::max(0.0, bounds.width() - 2 * margin);
    double h = std::max(0.0, bounds.height() - 2 * margin);
    if (shape == MindMap::Shape::Ellipse) {
        w = std::max(0.0, bounds.width() / 2 - margin) * M_SQRT2;
        h = std::max(0.0, bounds.height() / 2 - margin) * M_SQRT2;
    }
    QRectF rect(0, 0, w, h);
    rect.moveCenter(bounds.center());
    return rect;
}

void reverseStroke(Stroke& stroke) {
    std::reverse(stroke.points.begin(), stroke.points.end());
    std::reverse(stroke.widths.begin(), stroke.widths.end());
}

/// The point the given distance back along the points from their end, or their first point
QPointF backAlong(const QList<QPointF>& points, double distance) {
    double travelled = 0;
    for (qsizetype i = points.size() - 1; i > 0; --i) {
        travelled += QLineF(points[i - 1], points[i]).length();
        if (travelled >= distance) {
            return points[i - 1];
        }
    }
    return points.first();
}

QPointF unit(const QPointF& v) {
    const double len = std::hypot(v.x(), v.y());
    return len > 0 ? v / len : QPointF();
}

/// Puts an arrow head on the end of the points as Xournal++ draws one; the widths, if any, go on with width
void addArrowHead(QList<QPointF>& points, QList<double>& widths, double width) {
    const QPointF tip = points.last();
    const QPointF back = backAlong(points, ARROW_DIRECTION);
    if (back == tip) {
        return;
    }
    const double angle = std::atan2(tip.y() - back.y(), tip.x() - back.x());
    const double leg = std::min(std::max(ARROW_LEG * width, ARROW_MIN_LEG), ARROW_MAX_LEG_SHARE * length(points));
    auto legEnd = [&tip, leg](double a) { return QPointF(tip.x() - leg * std::cos(a), tip.y() - leg * std::sin(a)); };
    points.append({legEnd(angle + ARROW_ANGLE), tip, legEnd(angle - ARROW_ANGLE), tip});
    if (!widths.isEmpty()) {
        widths.append({width, width, width, width});
    }
}

/// Removes the arrow head from the end of the stroke, if it has one; whether it had one
bool removeArrowHead(Stroke& stroke) {
    if (!MindMap::hasArrowHead(stroke.points)) {
        return false;
    }
    const bool pressure = stroke.hasPressure();
    stroke.points.resize(stroke.points.size() - 4);
    if (pressure) {
        stroke.widths.resize(stroke.widths.size() - 4);
    }
    return true;
}

/**
 * Makes the points, whose end joins the outline, end where they reach it: cut where they first enter it, or
 * extended to it towards its centre. The widths, if any, follow
 */
void endAt(QList<QPointF>& points, QList<double>& widths, const QPolygonF& outline) {
    for (qsizetype i = 0; i + 1 < points.size(); ++i) {
        const QLineF segment(points[i], points[i + 1]);
        std::optional<double> nearest;
        QPointF hit;
        for (qsizetype j = 0; j + 1 < outline.size(); ++j) {
            QPointF p;
            if (segment.intersects(QLineF(outline[j], outline[j + 1]), &p) != QLineF::BoundedIntersection) {
                continue;
            }
            const double t = QLineF(points[i], p).length();
            if ((i > 0 || t > 1e-6) && (!nearest || t < *nearest)) {
                nearest = t;
                hit = p;
            }
        }
        if (nearest) {
            points.resize(i + 1);
            points.append(hit);
            if (!widths.isEmpty()) {
                widths.resize(i + 1);
            }
            return;
        }
    }
    const QPointF center = outline.boundingRect().center();
    const QPointF p = crossing(points.last(), center, outline, false);
    if (p != center) {
        points.append(p);
        if (!widths.isEmpty()) {
            widths.append(widths.last());
        }
    }
}

/**
 * The line made to end at the outline at one of its ends, keeping an arrow head there
 * @param factors the widths it gets, from its other end to the outline, if it has widths
 */
Stroke endedAt(Stroke line, bool atStart, const QPolygonF& outline, std::optional<std::pair<double, double>> factors) {
    if (atStart) {
        reverseStroke(line);
    }
    const bool arrow = removeArrowHead(line);
    endAt(line.points, line.widths, outline);
    if (factors && line.hasPressure()) {
        line.widths = taper(line.points, line.width, factors->first, factors->second);
    }
    if (arrow) {
        addArrowHead(line.points, line.widths, line.width);
    }
    if (atStart) {
        reverseStroke(line);
    }
    line.updateBounds();
    return line;
}

/// The branches with a free end at or in the outline of a node drawn now: they join it
Changes joinBranchEnds(const QPolygonF& outline, const Layer& layer, const Analysis& analysis,
                       const MindMap::Options& options) {
    Changes changes;
    for (const BranchEnd& end: analysis.branchEnds) {
        const Stroke& line = std::get<Stroke>(layer.elements[end.element]);
        if (!outline.containsPoint(end.point, Qt::WindingFill) &&
            distanceToOutline(end.point, outline) > options.attachTolerance + line.width) {
            continue;
        }
        const bool atStart = end.point == line.points.first() && end.point != line.points.last();
        std::optional<std::pair<double, double>> widths;
        if (options.taper) {
            widths = std::pair(TAPER_START, TAPER_NODE);
        }
        changes.emplace_back(end.element, endedAt(line, atStart, outline, widths));
    }
    return changes;
}

/// The node with an outline most of the stroke is inside, as writing put into it
const Node* nodeAround(const Stroke& stroke, const Analysis& analysis) {
    for (const Node& node: analysis.nodes) {
        if (node.kind != Node::Kind::Outline) {
            continue;
        }
        int inside = 0;
        for (const QPointF& p: stroke.points) {
            inside += node.outline.containsPoint(p, Qt::WindingFill) ? 1 : 0;
        }
        if (inside >= GROW_INSIDE_SHARE * static_cast<double>(stroke.points.size())) {
            return &node;
        }
    }
    return nullptr;
}

bool insideNode(const Stroke& stroke, const Analysis& analysis) { return nodeAround(stroke, analysis) != nullptr; }

/**
 * The node the stroke is written into grows around it, if it has no room for it, and the strokes that join it are
 * made to end at its new outline
 */
Changes grow(const Stroke& stroke, const Layer& layer, const Analysis& analysis, const MindMap::Options& options) {
    const Node* found = nodeAround(stroke, analysis);
    if (!found) {
        return {};
    }
    const Node& node = *found;
    bool fits = true;
    for (const QPointF& p: stroke.points) {
        fits = fits && node.outline.containsPoint(p, Qt::WindingFill) &&
               distanceToOutline(p, node.outline) >= (options.margin + stroke.width) / 2;
    }
    if (fits) {
        return {};
    }

    // Around the writing, the new one and what the outline had room for
    const Stroke& old = std::get<Stroke>(layer.elements[node.element]);
    const MindMap::Shape shape = MindMap::shapeOf(old.points);
    const double half = stroke.width / 2;
    const QRectF writing = writingBounds(layer, node.contents) |
                           boundsOf(stroke.points).adjusted(-half, -half, half, half) |
                           innerRect(shape, node.outline.boundingRect(), options.margin);
    Stroke grown = old;
    grown.points = MindMap::outline(shape, writing, options.margin);
    grown.updateBounds();
    Changes changes{{node.element, grown}};

    const QPolygonF outline(grown.points);
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        const auto* line = std::get_if<Stroke>(&layer.elements[i]);
        if (!line || i == node.element || !(analysis.branch[i] || analysis.structure[i]) ||
            line->points.size() < 2 || MindMap::isLoop(line->points)) {
            continue;
        }
        const double tolerance = END_TOLERANCE + line->width;
        const bool atStart = distanceToOutline(line->points.first(), node.outline) <= tolerance;
        const bool atEnd = distanceToOutline(line->points.last(), node.outline) <= tolerance;
        if (!atStart && !atEnd) {
            continue;
        }
        Stroke changed = *line;
        if (atEnd) {
            changed = endedAt(changed, false, outline, std::nullopt);
        }
        if (atStart) {
            changed = endedAt(changed, true, outline, std::nullopt);
        }
        changes.emplace_back(i, changed);
    }
    return changes;
}

/// The tip of a "V" drawn as an arrow head, and the direction it opens to
std::optional<std::pair<QPointF, QPointF>> vShape(const QList<QPointF>& points) {
    if (points.size() < 5) {
        return std::nullopt;
    }
    const QPointF a = points.first();
    const QPointF b = points.last();
    qsizetype apex = 0;
    double farthest = 0;
    for (qsizetype i = 1; i + 1 < points.size(); ++i) {
        const double distance = distanceToSegment(points[i], a, b);
        if (distance > farthest) {
            farthest = distance;
            apex = i;
        }
    }
    if (apex == 0) {
        return std::nullopt;
    }
    const QPointF tip = points[apex];
    const double la = QLineF(tip, a).length();
    const double lb = QLineF(tip, b).length();
    const double shorter = std::min(la, lb);
    const double longer = std::max(la, lb);
    if (shorter < V_MIN_LEG || longer > V_MAX_LEG || longer > V_MAX_RATIO * shorter) {
        return std::nullopt;
    }
    for (qsizetype i = 0; i < points.size(); ++i) {
        const bool first = i <= apex;
        if (distanceToSegment(points[i], first ? a : b, tip) > V_STRAIGHTNESS * (first ? la : lb) + 1.5) {
            return std::nullopt;
        }
    }
    const QPointF ua = unit(a - tip);
    const QPointF ub = unit(b - tip);
    const double angle = std::acos(std::clamp(QPointF::dotProduct(ua, ub), -1.0, 1.0));
    if (angle < V_MIN_ANGLE || angle > V_MAX_ANGLE) {
        return std::nullopt;
    }
    return std::pair(tip, unit(ua + ub));
}

/// The stroke as an arrow head drawn at the end of a branch or a cross link: the line gets the head
std::optional<MindMap::Result> arrowHead(const Stroke& stroke, const Layer& layer, const Analysis& analysis,
                                         const MindMap::Options& options) {
    const auto v = vShape(stroke.points);
    if (!v) {
        return std::nullopt;
    }
    const auto& [tip, opening] = *v;
    std::optional<size_t> best;
    bool bestAtStart = false;
    double bestDistance = options.attachTolerance + stroke.width;
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        if (!analysis.branch[i]) {
            continue;
        }
        Stroke line = std::get<Stroke>(layer.elements[i]);
        for (const bool atStart: {false, true}) {
            if (atStart) {
                reverseStroke(line);
            }
            const QPointF end = line.points.last();
            const double distance = QLineF(end, tip).length();
            if (distance > bestDistance || MindMap::hasArrowHead(line.points)) {
                continue;
            }
            // It opens back along the line
            const QPointF direction = unit(end - backAlong(line.points, ARROW_DIRECTION));
            if (QPointF::dotProduct(opening, -direction) < V_MIN_COS) {
                continue;
            }
            best = i;
            bestAtStart = atStart;
            bestDistance = distance;
        }
    }
    if (!best) {
        return std::nullopt;
    }
    Stroke line = std::get<Stroke>(layer.elements[*best]);
    if (bestAtStart) {
        reverseStroke(line);
    }
    addArrowHead(line.points, line.widths, line.width);
    line.updateBounds();

    MindMap::Result result;
    result.kind = MindMap::Result::Kind::ArrowHead;
    result.stroke = stroke;
    result.keep = false;
    result.to = *best;
    result.changes = {{*best, line}};
    return result;
}

/// The stroke as a scribble over elements, which deletes what it crosses
std::optional<MindMap::Result> scribble(const Stroke& stroke, const Layer& layer) {
    if (!MindMap::isScribble(stroke.points)) {
        return std::nullopt;
    }
    const QList<QPointF>& points = stroke.points;
    const QRectF bounds = boundsOf(points);
    std::vector<size_t> removed;
    for (size_t i = 0; i < layer.elements.size(); ++i) {
        const QRectF elementBounds = Renderer::elementBounds(layer.elements[i]);
        if (!elementBounds.intersects(bounds)) {
            continue;
        }
        bool crossed = false;
        if (const auto* other = std::get_if<Stroke>(&layer.elements[i])) {
            // A stroke it crosses
            for (qsizetype a = 0; a + 1 < points.size() && !crossed; ++a) {
                const QLineF segment(points[a], points[a + 1]);
                for (qsizetype b = 0; b + 1 < other->points.size() && !crossed; ++b) {
                    crossed = segment.intersects(QLineF(other->points[b], other->points[b + 1])) ==
                              QLineF::BoundedIntersection;
                }
            }
        } else {
            // A text or an image it covers in part
            const auto inside = std::count_if(points.begin(), points.end(),
                                              [&elementBounds](const QPointF& p) { return elementBounds.contains(p); });
            crossed = inside >= SCRIBBLE_MIN_COVER * static_cast<double>(points.size());
        }
        if (crossed) {
            removed.push_back(i);
        }
    }
    if (removed.empty()) {
        return std::nullopt;
    }
    MindMap::Result result;
    result.kind = MindMap::Result::Kind::Erase;
    result.stroke = stroke;
    result.keep = false;
    result.removed = std::move(removed);
    return result;
}

/// The stroke as a cross link, if it ends in a hook that goes back from its tip, drawn as its head
std::optional<MindMap::Result> crossLink(const Stroke& stroke, const Analysis& analysis,
                                         const MindMap::Options& options) {
    const QList<QPointF>& points = stroke.points;
    // The tip is where the stroke is farthest from its start
    qsizetype tip = 0;
    double farthest = 0;
    for (qsizetype i = 1; i < points.size(); ++i) {
        const double distance = QLineF(points.first(), points[i]).length();
        if (distance > farthest) {
            farthest = distance;
            tip = i;
        }
    }
    if (tip == 0 || tip + 1 >= points.size()) {
        return std::nullopt;
    }
    const QList<QPointF> shaft = points.mid(0, tip + 1);
    const QList<QPointF> hook = points.mid(tip);
    const double shaftLength = length(shaft);
    const double hookLength = length(hook);
    if (shaftLength < options.minBranchLength || farthest < BRANCH_MIN_STRAIGHTNESS * shaftLength ||
        hookLength < HOOK_MIN_LENGTH || hookLength > std::min(HOOK_MAX_LENGTH, HOOK_MAX_SHARE * shaftLength)) {
        return std::nullopt;
    }
    // The hook goes back
    QPointF hookEnd = points[tip];
    for (const QPointF& p: hook) {
        if (QLineF(points[tip], p).length() > QLineF(points[tip], hookEnd).length()) {
            hookEnd = p;
        }
    }
    const QPointF direction = unit(points[tip] - backAlong(shaft, ARROW_DIRECTION));
    if (QPointF::dotProduct(direction, unit(hookEnd - points[tip])) > HOOK_MAX_COS) {
        return std::nullopt;
    }

    const double tolerance = options.attachTolerance + stroke.width;
    const std::optional<Anchor> from = anchorAt(shaft.first(), analysis, tolerance, false);
    const std::optional<Anchor> to = anchorAt(shaft.last(), analysis, tolerance, false);
    if ((!from && !to) || (from && to && from->element == to->element)) {
        return std::nullopt;
    }
    QList<QPointF> path = branchPath(shaft, from, to, analysis, options.curved);
    QList<double> widths;
    addArrowHead(path, widths, widthOf(stroke));

    MindMap::Result result;
    result.kind = MindMap::Result::Kind::CrossLink;
    result.stroke = replaced(stroke, path);
    if (from) {
        result.from = from->element;
    }
    if (to) {
        result.to = to->element;
    }
    return result;
}

}  // namespace

bool MindMap::isLoop(const QList<QPointF>& points) {
    if (points.size() < LOOP_MIN_POINTS) {
        return false;
    }
    const QRectF bounds = boundsOf(points);
    const double diag = diagonal(bounds);
    if (diag <= 0) {
        return false;
    }

    // How far the stroke goes round the centre of its bounds
    const QPointF center = bounds.center();
    double turn = 0;
    double previous = std::atan2(points.first().y() - center.y(), points.first().x() - center.x());
    for (qsizetype i = 1; i < points.size(); ++i) {
        const double angle = std::atan2(points[i].y() - center.y(), points[i].x() - center.x());
        double delta = angle - previous;
        if (delta > M_PI) {
            delta -= 2 * M_PI;
        } else if (delta < -M_PI) {
            delta += 2 * M_PI;
        }
        turn += delta;
        previous = angle;
    }
    turn = std::abs(turn);
    if (turn < LOOP_MIN_TURN || turn > LOOP_MAX_TURN) {
        return false;
    }
    return turn >= 2 * M_PI || QLineF(points.first(), points.last()).length() <= LOOP_MAX_GAP * diag;
}

MindMap::Shape MindMap::shapeOf(const QList<QPointF>& points) {
    const QRectF bounds = boundsOf(points);
    const double diag = diagonal(bounds);
    if (points.size() < 3 || diag <= 0) {
        return Shape::Ellipse;
    }

    // An ellipse covers π/4 of its bounds and stays away from their corners, a rectangle covers nearly all
    double area = 0;
    for (qsizetype i = 0; i < points.size(); ++i) {
        const QPointF& p = points[i];
        const QPointF& q = points[(i + 1) % points.size()];
        area += p.x() * q.y() - q.x() * p.y();
    }
    const double fill = std::abs(area) / 2 / (bounds.width() * bounds.height());

    int corners = 0;
    for (const QPointF& corner: {bounds.topLeft(), bounds.topRight(), bounds.bottomLeft(), bounds.bottomRight()}) {
        double nearest = std::numeric_limits<double>::max();
        for (const QPointF& p: points) {
            nearest = std::min(nearest, QLineF(p, corner).length());
        }
        corners += nearest <= RECTANGLE_MAX_CORNER * diag ? 1 : 0;
    }
    return fill >= RECTANGLE_MIN_FILL && corners >= 3 ? Shape::Rectangle : Shape::Ellipse;
}

QList<QPointF> MindMap::outline(Shape shape, const QRectF& writing, double margin) {
    if (shape == Shape::Rectangle) {
        const QRectF r = writing.adjusted(-margin, -margin, margin, margin);
        return {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft(), r.topLeft()};
    }

    // The smallest ellipse through the corners of the writing has its proportions, √2 times as large
    const QPointF c = writing.center();
    const double a = writing.width() / M_SQRT2 + margin;
    const double b = writing.height() / M_SQRT2 + margin;
    const int count = std::max(24, static_cast<int>(M_PI * (a + b) / 3));
    QList<QPointF> points;
    for (int i = 0; i < count; ++i) {
        const double t = 2 * M_PI * i / count;
        points.append(QPointF(c.x() + a * std::cos(t), c.y() + b * std::sin(t)));
    }
    points.append(points.first());
    return points;
}

QList<QPointF> MindMap::smoothPath(const QPointF& start, const QList<QPointF>& drawn, const QPointF& end) {
    const double chord = QLineF(start, end).length();
    if (chord < 1e-6) {
        return {start, end};
    }

    double deviation = 0;
    for (const QPointF& p: drawn) {
        deviation = std::max(deviation, distanceToSegment(p, start, end));
    }
    // Nearly straight: a straight line
    QPointF c1 = start + (end - start) / 3;
    QPointF c2 = start + (end - start) * 2 / 3;
    if (deviation > std::max(STRAIGHT_TOLERANCE, STRAIGHT_TOLERANCE_SHARE * chord)) {
        // The control points of the cubic Bézier curve from start to end that is nearest to the points, which are
        // placed on it by their distance along the stroke (least squares)
        QList<QPointF> all{start};
        all.append(drawn);
        all.append(end);
        const double total = length(all);
        double a11 = 0, a12 = 0, a22 = 0;
        QPointF r1, r2;
        double travelled = 0;
        for (qsizetype i = 1; i + 1 < all.size(); ++i) {
            travelled += QLineF(all[i - 1], all[i]).length();
            const double t = travelled / total;
            const double s = 1 - t;
            const double b0 = s * s * s, b1 = 3 * t * s * s, b2 = 3 * t * t * s, b3 = t * t * t;
            const QPointF r = all[i] - start * b0 - end * b3;
            a11 += b1 * b1;
            a12 += b1 * b2;
            a22 += b2 * b2;
            r1 += r * b1;
            r2 += r * b2;
        }
        const double det = a11 * a22 - a12 * a12;
        if (std::abs(det) > 1e-9) {
            const QPointF p1 = (r1 * a22 - r2 * a12) / det;
            const QPointF p2 = (r2 * a11 - r1 * a12) / det;
            // A wild curve from few points is worse than a straight line
            const QPointF middle = (start + end) / 2;
            if (QLineF(p1, middle).length() < 1.5 * chord && QLineF(p2, middle).length() < 1.5 * chord) {
                c1 = p1;
                c2 = p2;
            }
        }
    }

    const double estimate = QLineF(start, c1).length() + QLineF(c1, c2).length() + QLineF(c2, end).length();
    const int count = std::max(8, static_cast<int>(estimate / PATH_STEP));
    QList<QPointF> points;
    for (int i = 0; i <= count; ++i) {
        const double t = static_cast<double>(i) / count;
        const double s = 1 - t;
        points.append(start * (s * s * s) + c1 * (3 * t * s * s) + c2 * (3 * t * t * s) + end * (t * t * t));
    }
    return points;
}

std::vector<MindMap::Node> MindMap::findNodes(const Layer& layer, const Options& options) {
    return analyze(layer, options).nodes;
}

MindMap::Result MindMap::recognize(const Stroke& stroke, const Layer& layer, const Options& options) {
    Result result;
    result.stroke = stroke;
    const QList<QPointF>& points = stroke.points;
    if (points.size() < 2) {
        return result;
    }
    const Analysis analysis = analyze(layer, options);

    if (isLoop(points)) {
        std::vector<size_t> contents = enclosed(QPolygonF(points), layer, std::nullopt);
        const bool group = std::any_of(contents.begin(), contents.end(),
                                       [&analysis](size_t i) { return analysis.structure[i]; });
        const QRectF bounds = boundsOf(points);
        if (group) {
            // Only smoothed, and widened where the smoothing cut into what it is around
            QList<QPointF> smooth = smoothLoop(points);
            const QPointF center = QPolygonF(smooth).boundingRect().center();
            for (int step = 0; step < GROUP_WIDEN_STEPS; ++step) {
                const QPolygonF polygon(smooth);
                auto in = [&polygon](const QPointF& p) { return polygon.containsPoint(p, Qt::WindingFill); };
                const bool around = std::all_of(contents.begin(), contents.end(), [&](size_t i) {
                    const QList<QPointF> inside = samples(layer.elements[i]);
                    return std::all_of(inside.begin(), inside.end(), in);
                });
                if (around) {
                    break;
                }
                for (QPointF& p: smooth) {
                    p = center + (p - center) * GROUP_WIDEN;
                }
            }
            result.kind = Result::Kind::Group;
            result.stroke = replaced(stroke, smooth);
            result.contents = std::move(contents);
            return result;
        }
        if (contents.empty() && diagonal(bounds) < options.emptyNodeMinSize) {
            return result;
        }
        // Around the writing, or where it was drawn, for writing put into it later
        const Shape shape = options.nodeShape == Options::NodeShape::Ellipse   ? Shape::Ellipse :
                            options.nodeShape == Options::NodeShape::Rectangle ? Shape::Rectangle :
                                                                                 shapeOf(points);
        const QRectF writing =
                contents.empty() ? innerRect(shape, bounds, options.margin) : writingBounds(layer, contents);
        result.kind = Result::Kind::Node;
        result.stroke = replaced(stroke, outline(shape, writing, options.margin));
        result.contents = std::move(contents);
        result.changes = joinBranchEnds(QPolygonF(result.stroke.points), layer, analysis, options);
        return result;
    }

    if (options.scribbleErases) {
        if (std::optional<Result> erase = scribble(stroke, layer)) {
            return *erase;
        }
    }
    if (std::optional<Result> head = arrowHead(stroke, layer, analysis, options)) {
        return *head;
    }
    if (std::optional<Result> link = crossLink(stroke, analysis, options)) {
        return *link;
    }

    // A line under writing, drawn from where it joins or towards it
    const QList<QPointF> reversed(points.rbegin(), points.rend());
    for (const QList<QPointF>* drawn: {&points, &reversed}) {
        if (std::optional<Result> underlined = underline(stroke, *drawn, layer, analysis, options)) {
            return *underlined;
        }
    }

    // Writing put into a node: mostly inside it. The node grows if it has no room for it
    if (insideNode(stroke, analysis)) {
        result.changes = grow(stroke, layer, analysis, options);
        return result;
    }

    // A branch is long and rather straight, writing is short or winds
    const double span = QLineF(points.first(), points.last()).length();
    if (span < options.minBranchLength || span < BRANCH_MIN_STRAIGHTNESS * length(points)) {
        return result;
    }
    const double tolerance = options.attachTolerance + stroke.width;
    std::optional<Anchor> from = anchorAt(points.first(), analysis, tolerance, false);
    std::optional<Anchor> to = anchorAt(points.last(), analysis, tolerance, false, from.has_value());
    if ((!from && !to) || (from && to && from->element == to->element)) {
        return result;
    }
    if (to && to->line) {
        // A line under writing that was drawn on its own: it is made as long as the writing, and joined at the end
        // the branch comes to
        const Node& node = analysis.nodes[*to->line];
        const Stroke& line = std::get<Stroke>(layer.elements[node.element]);
        const std::optional<Tail> tail = horizontalTail(line.points);
        if (tail && tail->first == 0) {
            const QRectF writing = writingBounds(layer, node.contents);
            const double y = writing.bottom() + options.underlineGap;
            QPointF nearEnd(writing.left() - options.underlineGap, y);
            QPointF farEnd(writing.right() + options.underlineGap, y);
            if (QLineF(points.last(), farEnd).length() < QLineF(points.last(), nearEnd).length()) {
                std::swap(nearEnd, farEnd);
            }
            result.changes.emplace_back(node.element, replaced(line, {nearEnd, farEnd}));
            to->point = nearEnd;
        }
    }
    // A branch goes from the node it comes from: drawn towards a node from empty space, it starts at that node
    const QList<QPointF>* drawn = &points;
    if (!from) {
        std::swap(from, to);
        drawn = &reversed;
    }
    const QList<QPointF> path = branchPath(*drawn, from, to, analysis, options.curved);
    QList<double> widths;
    if (options.taper) {
        widths = taper(path, widthOf(stroke), TAPER_START, to ? TAPER_NODE : TAPER_END);
    }
    result.kind = Result::Kind::Branch;
    result.stroke = replaced(stroke, path, widths);
    if (options.branchColors) {
        result.stroke.color = branchColor(*from, layer, analysis).value_or(stroke.color);
    }
    result.from = from->element;
    if (to) {
        result.to = to->element;
    }
    return result;
}

bool MindMap::hasArrowHead(const QList<QPointF>& points) {
    const qsizetype n = points.size();
    return n >= 6 && points[n - 1] == points[n - 3] && points[n - 1] == points[n - 5];
}

QList<QPointF> MindMap::smoothLoop(const QList<QPointF>& points) {
    if (points.size() < 3) {
        return points;
    }
    // Once round: what overlaps at the end is left out
    const QPointF center = boundsOf(points).center();
    QList<QPointF> once{points.first()};
    double turn = 0;
    double previous = std::atan2(points.first().y() - center.y(), points.first().x() - center.x());
    for (qsizetype i = 1; i < points.size() && std::abs(turn) < 2 * M_PI; ++i) {
        const double angle = std::atan2(points[i].y() - center.y(), points[i].x() - center.x());
        double delta = angle - previous;
        if (delta > M_PI) {
            delta -= 2 * M_PI;
        } else if (delta < -M_PI) {
            delta += 2 * M_PI;
        }
        turn += delta;
        previous = angle;
        once.append(points[i]);
    }
    once.append(once.first());

    // Points at equal distances along it, closed by a line over the gap between its ends
    const double total = length(once);
    if (total <= 0) {
        return points;
    }
    std::vector<std::complex<double>> samples;
    samples.reserve(GROUP_POINTS);
    qsizetype segment = 0;
    double travelled = 0;
    for (int k = 0; k < GROUP_POINTS; ++k) {
        const double at = total * k / GROUP_POINTS;
        while (segment + 2 < once.size() && travelled + QLineF(once[segment], once[segment + 1]).length() < at) {
            travelled += QLineF(once[segment], once[segment + 1]).length();
            ++segment;
        }
        const QLineF line(once[segment], once[segment + 1]);
        const QPointF p = line.length() > 0 ? line.pointAt((at - travelled) / line.length()) : line.p1();
        samples.emplace_back(p.x(), p.y());
    }

    // Only its slow waves: a Fourier series cut off after a few terms
    std::vector<std::complex<double>> terms;
    for (int k = -GROUP_HARMONICS; k <= GROUP_HARMONICS; ++k) {
        std::complex<double> sum;
        for (int j = 0; j < GROUP_POINTS; ++j) {
            sum += samples[static_cast<size_t>(j)] * std::polar(1.0, -2 * M_PI * k * j / GROUP_POINTS);
        }
        terms.push_back(sum / static_cast<double>(GROUP_POINTS));
    }
    QList<QPointF> smooth;
    for (int j = 0; j < GROUP_POINTS; ++j) {
        std::complex<double> p;
        for (int k = -GROUP_HARMONICS; k <= GROUP_HARMONICS; ++k) {
            p += terms[static_cast<size_t>(k + GROUP_HARMONICS)] * std::polar(1.0, 2 * M_PI * k * j / GROUP_POINTS);
        }
        smooth.append(QPointF(p.real(), p.imag()));
    }
    smooth.append(smooth.first());
    return smooth;
}

std::vector<size_t> MindMap::withContents(const Layer& layer, const std::vector<size_t>& elements,
                                          const Options& options) {
    const Analysis analysis = analyze(layer, options);
    std::vector<bool> chosen(layer.elements.size(), false);
    for (const size_t i: elements) {
        chosen[i] = true;
    }
    for (const auto& [group, contents]: analysis.groups) {
        if (chosen[group]) {
            for (const size_t i: contents) {
                chosen[i] = true;
            }
        }
    }
    // Nodes chosen by themselves or in a group
    for (const Node& node: analysis.nodes) {
        if (chosen[node.element]) {
            for (const size_t i: node.contents) {
                chosen[i] = true;
            }
        }
    }
    std::vector<size_t> result;
    for (size_t i = 0; i < chosen.size(); ++i) {
        if (chosen[i]) {
            result.push_back(i);
        }
    }
    return result;
}

std::vector<std::pair<size_t, Stroke>> MindMap::follow(const Layer& before, const Layer& after,
                                                       const std::vector<size_t>& moved, const Options& options) {
    const Analysis analysis = analyze(before, options);
    std::vector<bool> isMoved(before.elements.size(), false);
    for (const size_t i: moved) {
        isMoved[i] = true;
    }

    // Where the place a stroke joins a node goes: by how much the node moved, and its new outline
    auto stroke = [](const Layer& layer, size_t i) -> const Stroke& { return std::get<Stroke>(layer.elements[i]); };
    auto shift = [&](const Node& node) {
        if (!isMoved[node.element]) {
            return QPointF();
        }
        if (node.kind == Node::Kind::Underline) {
            return stroke(after, node.element).points.last() - stroke(before, node.element).points.last();
        }
        return QPolygonF(stroke(after, node.element).points).boundingRect().center() -
               node.outline.boundingRect().center();
    };
    auto joinedNode = [&](const QPointF& p, double tolerance, size_t self) -> const Node* {
        for (const Node& node: analysis.nodes) {
            if (node.element != self && joins(node, p, tolerance)) {
                return &node;
            }
        }
        return nullptr;
    };

    Changes changes;
    for (size_t i = 0; i < before.elements.size(); ++i) {
        const auto* old = std::get_if<Stroke>(&before.elements[i]);
        if (!old || old->points.size() < 2 || !(analysis.branch[i] || analysis.structure[i]) || isLoop(old->points)) {
            continue;
        }
        const double tolerance = END_TOLERANCE + old->width;
        const Node* startNode = joinedNode(old->points.first(), tolerance, i);
        // The line of an underlined node belongs to it: only what leads to it bends
        const auto own = std::find_if(analysis.nodes.begin(), analysis.nodes.end(),
                                      [i](const Node& node) { return node.element == i; });
        const bool underline = own != analysis.nodes.end();
        const Node* endNode = underline ? nullptr : joinedNode(old->points.last(), tolerance, i);
        if (!startNode && !endNode) {
            continue;
        }

        Stroke line = isMoved[i] ? stroke(after, i) : *old;
        const bool arrow = removeArrowHead(line);
        Stroke original = *old;
        removeArrowHead(original);
        auto target = [&](const Node* node, const QPointF& was, const QPointF& is) {
            return node ? was + shift(*node) : is;
        };
        const QPointF d0 = target(startNode, original.points.first(), line.points.first()) - line.points.first();
        const QPointF d1 = underline ? QPointF()
                                     : target(endNode, original.points.last(), line.points.last()) - line.points.last();
        if (std::hypot(d0.x(), d0.y()) < 1e-9 && std::hypot(d1.x(), d1.y()) < 1e-9) {
            continue;
        }

        // Bent smoothly: each point moves by a share of the moves of the ends, by how far along the line it is
        qsizetype last = line.points.size() - 1;
        if (underline) {
            if (const std::optional<Tail> tail = horizontalTail(line.points)) {
                last = tail->first;
            }
        }
        const QList<QPointF> unbent = line.points.mid(0, last + 1);
        const double total = length(unbent);
        double travelled = 0;
        for (qsizetype k = 0; k <= last; ++k) {
            if (k > 0) {
                travelled += QLineF(unbent[k - 1], unbent[k]).length();
            }
            const double t = total > 0 ? travelled / total : 0.0;
            line.points[k] += d0 * (1 - t) + d1 * t;
        }
        // Exactly on the outlines of the nodes
        auto outlineOf = [&](const Node& node) { return QPolygonF(stroke(after, node.element).points); };
        if (startNode && startNode->kind == Node::Kind::Outline) {
            line = endedAt(line, true, outlineOf(*startNode), std::nullopt);
        }
        if (endNode && endNode->kind == Node::Kind::Outline) {
            line = endedAt(line, false, outlineOf(*endNode), std::nullopt);
        }
        if (arrow) {
            addArrowHead(line.points, line.widths, line.width);
        }
        line.updateBounds();
        changes.emplace_back(i, line);
    }
    return changes;
}

bool MindMap::isScribble(const QList<QPointF>& points) {
    if (points.size() < SCRIBBLE_MIN_POINTS) {
        return false;
    }
    // Its main direction: the axis along which its points spread most
    double sx = 0, sy = 0;
    for (const QPointF& p: points) {
        sx += p.x();
        sy += p.y();
    }
    const double n = static_cast<double>(points.size());
    const QPointF mean(sx / n, sy / n);
    double xx = 0, xy = 0, yy = 0;
    for (const QPointF& p: points) {
        const QPointF d = p - mean;
        xx += d.x() * d.x();
        xy += d.x() * d.y();
        yy += d.y() * d.y();
    }
    const double angle = std::atan2(2 * xy, xx - yy) / 2;
    const QPointF axis(std::cos(angle), std::sin(angle));

    // How often it goes back along it, by more than a share of its extent
    double low = std::numeric_limits<double>::max();
    double high = std::numeric_limits<double>::lowest();
    for (const QPointF& p: points) {
        const double t = QPointF::dotProduct(p, axis);
        low = std::min(low, t);
        high = std::max(high, t);
    }
    const double extent = high - low;
    if (extent < SCRIBBLE_MIN_EXTENT) {
        return false;
    }
    const double hysteresis = std::max(SCRIBBLE_MIN_HYSTERESIS, SCRIBBLE_HYSTERESIS * extent);
    int turns = 0;
    int direction = 0;
    double extreme = QPointF::dotProduct(points.first(), axis);
    for (const QPointF& p: points) {
        const double t = QPointF::dotProduct(p, axis);
        if (direction >= 0 && t > extreme) {
            extreme = t;
            direction = 1;
        } else if (direction <= 0 && t < extreme) {
            extreme = t;
            direction = -1;
        } else if ((direction > 0 && t < extreme - hysteresis) || (direction < 0 && t > extreme + hysteresis)) {
            direction = -direction;
            extreme = t;
            ++turns;
        }
    }
    return turns >= SCRIBBLE_MIN_TURNS;
}

QList<QColor> MindMap::branchColors() {
    return {QColor(237, 83, 83), QColor(255, 161, 84), QColor(58, 145, 4), QColor(100, 186, 255),
            QColor(205, 158, 247)};
}
