#include "Eraser.h"

#include <algorithm>
#include <cmath>

#include <QLineF>
#include <QRectF>

namespace {

using Eraser::PathParameter;

constexpr double CLOSED_STROKE_DISTANCE = 0.3;

/// Padding of the eraser square in units of the stroke width, depending on the cap of the stroke
double paddingCoefficient(Qt::PenCapStyle cap) {
    switch (cap) {
        case Qt::FlatCap:
            return 0.01;
        case Qt::SquareCap:
            return 0.5;
        default:
            return 0.4;
    }
}

struct Interval {
    double min;
    double max;
};

bool isInside(const QPointF& p, const QRectF& rect) {
    return p.x() >= rect.left() && p.x() <= rect.right() && p.y() >= rect.top() && p.y() <= rect.bottom();
}

/**
 * The parameters at which the line through p and q is inside the rectangle: it enters at
 * p + min * (q - p) and leaves at p + max * (q - p).
 */
std::optional<Interval> intersectLineWithRectangle(const QPointF& p, const QPointF& q, const QRectF& rect) {
    auto intersectLineWithStrip = [](double a1, double a2, double stripMin, double stripWidth) {
        const double norm = 1.0 / (a2 - a1);
        const double t1 = (stripMin - a1) * norm;
        const double t2 = t1 + stripWidth * norm;
        return Interval{std::min(t1, t2), std::max(t1, t2)};
    };

    if (p.x() == q.x()) {
        if (p.y() == q.y()) {
            return std::nullopt;  // a single dot
        }
        if (rect.left() < p.x() && p.x() < rect.right()) {
            return intersectLineWithStrip(p.y(), q.y(), rect.top(), rect.height());
        }
        return std::nullopt;
    }
    if (p.y() == q.y()) {
        if (rect.top() < p.y() && p.y() < rect.bottom()) {
            return intersectLineWithStrip(p.x(), q.x(), rect.left(), rect.width());
        }
        return std::nullopt;
    }

    const Interval vertical = intersectLineWithStrip(p.y(), q.y(), rect.top(), rect.height());
    const Interval horizontal = intersectLineWithStrip(p.x(), q.x(), rect.left(), rect.width());
    const Interval both{std::max(vertical.min, horizontal.min), std::min(vertical.max, horizontal.max)};
    if (both.min > both.max) {
        return std::nullopt;
    }
    return both;
}

/// Like intersectLineWithRectangle(), but only the parameters between p and q
std::vector<double> intersectLineSegmentWithRectangle(const QPointF& p, const QPointF& q, const QRectF& rect) {
    std::vector<double> result;
    if (const auto intersections = intersectLineWithRectangle(p, q, rect)) {
        if (intersections->min > 0.0 && intersections->min <= 1.0) {
            result.push_back(intersections->min);
        }
        if (intersections->max > 0.0 && intersections->max <= 1.0) {
            result.push_back(intersections->max);
        }
    }
    return result;
}

QPointF pointAt(const Stroke& stroke, const PathParameter& parameter) {
    const QPointF& p = stroke.points[parameter.index];
    const QPointF& q = stroke.points[parameter.index + 1];
    return p + parameter.t * (q - p);
}

void appendPoint(Stroke& s, const QPointF& point, bool hasPressure, double width) {
    // Cutting exactly at a point of the stroke would give a segment without length
    if (!s.points.isEmpty() && s.points.last() == point) {
        return;
    }
    if (hasPressure && !s.points.isEmpty()) {
        s.widths.append(width);
    }
    s.points.append(point);
}

Stroke styleOf(const Stroke& stroke) {
    Stroke s = stroke;
    s.points.clear();
    s.widths.clear();
    return s;
}

/// The end and the beginning of a closed stroke as one piece: from start over the first point to end
Stroke circularSection(const Stroke& stroke, const PathParameter& start, const PathParameter& end) {
    const bool hasPressure = stroke.hasPressure();
    const int last = static_cast<int>(stroke.points.size()) - 1;
    auto width = [&](int segment) { return hasPressure ? stroke.widths[segment] : 0.0; };

    Stroke s = styleOf(stroke);
    appendPoint(s, pointAt(stroke, start), hasPressure, 0);
    // The last point is the same as the first one: take it only once
    for (int i = start.index + 1; i < last; ++i) {
        appendPoint(s, stroke.points[i], hasPressure, width(i - 1));
    }
    appendPoint(s, stroke.points[0], hasPressure, width(last - 1));
    for (int i = 1; i <= end.index; ++i) {
        appendPoint(s, stroke.points[i], hasPressure, width(i - 1));
    }
    appendPoint(s, pointAt(stroke, end), hasPressure, width(end.index));
    s.updateBounds();
    return s;
}

}  // namespace

bool Eraser::intersects(const Stroke& stroke, const QPointF& center, double halfSize) {
    if (stroke.points.isEmpty()) {
        return false;
    }
    const QRectF box(center.x() - halfSize, center.y() - halfSize, 2 * halfSize, 2 * halfSize);
    QPointF last = stroke.points.first();
    for (const QPointF& point: stroke.points) {
        if (isInside(point, box)) {
            return true;
        }
        const double length = QLineF(last, point).length();
        if (length >= halfSize) {
            // Distance of the center of the eraser to the line through the two points
            const double distance = std::abs((center.x() - last.x()) * (last.y() - point.y()) +
                                             (center.y() - last.y()) * (point.x() - last.x())) /
                                    length;
            if (distance <= halfSize) {
                // Near the line: is it near the segment as well? A circle around the middle of the segment, with
                // half its length plus the half diagonal of the eraser as radius, decides
                constexpr double PADDING = 0.1;
                const double toMiddle = QLineF(center, (last + point) / 2).length() - halfSize * std::sqrt(2.0);
                if (toMiddle <= length / 2 + PADDING) {
                    return true;
                }
            }
        }
        last = point;
    }
    return false;
}

std::vector<PathParameter> Eraser::intersectWithPaddedBox(const Stroke& stroke, const QPointF& center,
                                                          double halfSize, double halfSizeWithPadding) {
    const QRectF innerBox(center.x() - halfSize, center.y() - halfSize, 2 * halfSize, 2 * halfSize);
    const QRectF outerBox(center.x() - halfSizeWithPadding, center.y() - halfSizeWithPadding, 2 * halfSizeWithPadding,
                          2 * halfSizeWithPadding);
    const auto& points = stroke.points;
    const int pointCount = static_cast<int>(points.size());

    if (pointCount < 2) {
        if (pointCount == 1 && isInside(points.first(), innerBox)) {
            return {PathParameter{0, 0.0}, PathParameter{0, 0.0}};
        }
        return {};
    }

    // A part of the stroke in the padded box is only erased if the stroke also enters the eraser itself
    bool isInsideOuter = false;
    bool wentInsideInner = false;
    bool lastSegmentEndedOnBoundary = false;
    if (isInside(points[0], innerBox)) {
        isInsideOuter = true;
        wentInsideInner = true;
    } else if (isInside(points[0], outerBox)) {
        // If the stroke comes from the direction of the eraser, count it as having been inside
        const auto innerLine = intersectLineWithRectangle(points[0], points[1], innerBox);
        isInsideOuter = true;
        wentInsideInner = innerLine && innerLine->max <= 0.0;
    }

    std::vector<PathParameter> result;
    if (isInsideOuter) {
        result.push_back({0, 0.0});
    }

    for (int index = 0; index + 1 < pointCount; ++index) {
        const QPointF& first = points[index];
        const QPointF& second = points[index + 1];
        const auto outerIntersections = intersectLineSegmentWithRectangle(first, second, outerBox);
        if (outerIntersections.empty() && !isInside(first, outerBox) && !isInside(second, outerBox)) {
            continue;
        }

        // A part of the segment is in the padded box
        const auto innerIntersections = intersectLineSegmentWithRectangle(first, second, innerBox);
        auto itInner = innerIntersections.begin();
        auto skipInnerIntersectionsBelow = [&](double value) {
            bool skipped = false;
            while (itInner != innerIntersections.end() && *itInner < value) {
                skipped = true;
                ++itInner;
            }
            return skipped;
        };

        if (lastSegmentEndedOnBoundary) {
            lastSegmentEndedOnBoundary = false;
            QPointF p = second;
            if (!outerIntersections.empty()) {
                p = first + 0.5 * outerIntersections.front() * (second - first);
            }
            if (isInside(p, outerBox) != (result.size() % 2 != 0) && !result.empty()) {
                // The stroke bounced on the border of the box and never went in or out
                result.pop_back();
            }
        }

        for (double outerIntersection: outerIntersections) {
            wentInsideInner |= skipInnerIntersectionsBelow(outerIntersection);
            if (!isInsideOuter || wentInsideInner) {
                result.push_back({index, outerIntersection});
                if (outerIntersection == 1.0) {
                    lastSegmentEndedOnBoundary = true;
                }
            } else if (!result.empty()) {
                // Left the padded box without having touched the eraser: forget the entry
                result.pop_back();
            }
            wentInsideInner = false;
            isInsideOuter = !isInsideOuter;
        }
        if (itInner != innerIntersections.end()) {
            wentInsideInner = true;
        }
    }

    // Numerical imprecision can give inconsistent results, typically when a point of the stroke lies on the
    // boundary of the padded box. Such results are dropped
    if (result.size() % 2 != 0) {
        // Not necessarily inconsistent: the stroke may end in the padded box
        const QPointF& lastPoint = points[pointCount - 1];
        if (!isInside(lastPoint, outerBox)) {
            return {};
        }
        const auto innerLine = intersectLineWithRectangle(lastPoint, points[pointCount - 2], innerBox);
        if (wentInsideInner || (innerLine && innerLine->max < 0.0)) {
            result.push_back({pointCount - 2, 1.0});
        } else {
            result.pop_back();
        }
    }
    for (size_t i = 0; i + 1 < result.size(); i += 2) {
        // A point of the stroke between the two parameters has to be in the padded box
        const PathParameter& start = result[i];
        const PathParameter& end = result[i + 1];
        const QPointF testPoint = start.index == end.index ?
                                          pointAt(stroke, {start.index, 0.5 * (start.t + end.t)}) :
                                          points[start.index + 1];
        if (!isInside(testPoint, outerBox)) {
            return {};
        }
    }
    return result;
}

Stroke Eraser::section(const Stroke& stroke, const PathParameter& from, const PathParameter& to) {
    const bool hasPressure = stroke.hasPressure();
    auto width = [&](int segment) { return hasPressure ? stroke.widths[segment] : 0.0; };

    Stroke s = styleOf(stroke);
    appendPoint(s, pointAt(stroke, from), hasPressure, 0);
    for (int i = from.index + 1; i <= to.index; ++i) {
        appendPoint(s, stroke.points[i], hasPressure, width(i - 1));
    }
    appendPoint(s, pointAt(stroke, to), hasPressure, width(to.index));
    s.updateBounds();
    return s;
}

std::optional<std::vector<Stroke>> Eraser::erase(const Stroke& stroke, const QPointF& center, double halfSize) {
    const double padded = halfSize + paddingCoefficient(stroke.cap) * stroke.width;
    if (!stroke.bounds.adjusted(-padded, -padded, padded, padded).contains(center)) {
        return std::nullopt;
    }
    const std::vector<PathParameter> erased = intersectWithPaddedBox(stroke, center, halfSize, padded);
    if (erased.empty()) {
        return std::nullopt;
    }
    const int pointCount = static_cast<int>(stroke.points.size());
    if (pointCount < 2) {
        return std::vector<Stroke>();
    }

    return split(stroke, erased);
}

std::vector<Stroke> Eraser::split(const Stroke& stroke, const std::vector<PathParameter>& erased) {
    const int pointCount = static_cast<int>(stroke.points.size());

    // What remains: the complement of the erased parts
    const PathParameter begin{0, 0.0};
    const PathParameter end{pointCount - 2, 1.0};
    std::vector<std::pair<PathParameter, PathParameter>> remaining;
    PathParameter position = begin;
    for (size_t i = 0; i + 1 < erased.size(); i += 2) {
        if (position < erased[i]) {
            remaining.emplace_back(position, erased[i]);
        }
        if (position < erased[i + 1]) {
            position = erased[i + 1];
        }
    }
    if (position < end) {
        remaining.emplace_back(position, end);
    }

    std::vector<Stroke> pieces;
    auto first = remaining.begin();
    auto last = remaining.end();
    const bool closed = pointCount >= 3 &&
                        QLineF(stroke.points.first(), stroke.points.last()).length() < CLOSED_STROKE_DISTANCE;
    if (closed && remaining.size() >= 2 && remaining.front().first == begin && remaining.back().second == end) {
        // The pieces at the end and at the beginning belong together
        pieces.push_back(circularSection(stroke, remaining.back().first, remaining.front().second));
        ++first;
        --last;
    }
    for (auto it = first; it != last; ++it) {
        pieces.push_back(section(stroke, it->first, it->second));
    }
    // A piece cannot be shorter than two points
    pieces.erase(std::remove_if(pieces.begin(), pieces.end(), [](const Stroke& s) { return s.points.size() < 2; }),
                 pieces.end());
    return pieces;
}
