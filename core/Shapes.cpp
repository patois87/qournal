#include "Shapes.h"

#include <algorithm>
#include <cmath>

#include "SplineSegment.h"

QList<QPointF> Shapes::create(Type type, const QPointF& start, const QPointF& current, const Snapper& snapper,
                              const Modifiers& modifiers, double thickness) {
    switch (type) {
        case Type::Line:
            return line(start, current, snapper, modifiers);
        case Type::Rectangle:
            return rectangle(start, current, snapper, modifiers);
        case Type::Ellipse:
            return ellipse(start, current, snapper, modifiers);
        case Type::Arrow:
            return arrow(start, current, snapper, modifiers, thickness, false);
        case Type::DoubleArrow:
            return arrow(start, current, snapper, modifiers, thickness, true);
        case Type::CoordinateSystem:
            return coordinateSystem(start, current, snapper, modifiers);
    }
    return {};
}

QList<QPointF> Shapes::line(const QPointF& start, const QPointF& current, const Snapper& snapper,
                            const Modifiers& modifiers) {
    return {start, snapper.snap(current, start, modifiers.alt)};
}

QList<QPointF> Shapes::rectangle(const QPointF& start, const QPointF& current, const Snapper& snapper,
                                 const Modifiers& modifiers) {
    const QPointF c = snapper.snapToGrid(current, modifiers.alt);
    double width = c.x() - start.x();
    double height = c.y() - start.y();

    if (modifiers.shift) {
        // A square
        const int signW = width > 0 ? 1 : -1;
        const int signH = height > 0 ? 1 : -1;
        width = std::max(width * signW, height * signH) * signW;
        height = (width * signW) * signH;
    }

    // With Control the starting point is the center
    const QPointF p1 = modifiers.control ? QPointF(start.x() - width, start.y() - height) : start;
    const QPointF p2(start.x() + width, start.y() + height);
    return {p1, QPointF(p1.x(), p2.y()), p2, QPointF(p2.x(), p1.y()), p1};
}

QList<QPointF> Shapes::ellipse(const QPointF& start, const QPointF& current, const Snapper& snapper,
                               const Modifiers& modifiers) {
    const QPointF c = snapper.snapToGrid(current, modifiers.alt);
    double width = c.x() - start.x();
    double height = c.y() - start.y();

    if (modifiers.shift) {
        // A circle
        width = modifiers.control ? std::hypot(width, height) :
                                    std::copysign(std::max(std::abs(width), std::abs(height)), width);
        height = std::copysign(width, height);
    }

    double radiusX, radiusY;
    QPointF center;
    if (!modifiers.control) {
        radiusX = 0.5 * width;
        radiusY = 0.5 * height;
        center = QPointF(start.x() + radiusX, start.y() + radiusY);
    } else {
        radiusX = width;
        radiusY = height;
        center = start;
    }

    // The number of points depends on the size (heuristic)
    const int pointsPerQuadrant = static_cast<int>(std::ceil(5 + 0.3 * (std::abs(radiusX) + std::abs(radiusY))));
    const double stepAngle = M_PI_2 / pointsPerQuadrant;

    QList<QPointF> shape;
    shape.reserve(4 * pointsPerQuadrant + 1);
    shape.append(QPointF(center.x() + radiusX, center.y()));
    for (int j = 1; j < pointsPerQuadrant; ++j) {
        // Between equal steps of the angle at the center and equal steps along the ellipse
        const double targetAngle = stepAngle * j;
        const double centerAngle = 0.25 * std::atan2(std::abs(radiusY) * std::sin(targetAngle),
                                                     std::abs(radiusX) * std::cos(targetAngle)) +
                                   0.75 * targetAngle;
        shape.append(QPointF(center.x() + radiusX * std::cos(centerAngle),
                             center.y() + radiusY * std::sin(centerAngle)));
    }
    shape.append(QPointF(center.x(), center.y() + radiusY));

    // The other quadrants by symmetry; the last point closes the ellipse
    for (qsizetype i = shape.size() - 2; i >= 0; --i) {
        shape.append(QPointF(2 * center.x() - shape[i].x(), shape[i].y()));
    }
    for (qsizetype i = shape.size() - 2; i >= 0; --i) {
        shape.append(QPointF(shape[i].x(), 2 * center.y() - shape[i].y()));
    }
    return shape;
}

QList<QPointF> Shapes::arrow(const QPointF& start, const QPointF& current, const Snapper& snapper,
                             const Modifiers& modifiers, double thickness, bool doubleEnded) {
    const QPointF c = snapper.snap(current, start, modifiers.alt);
    const double lineLength = std::hypot(c.x() - start.x(), c.y() - start.y());
    const double slimness = lineLength / thickness;

    // arrowDist is the length of the legs of the head, delta the angle between a leg and the line.
    // The size of the head follows the thickness, limited by the length of the line
    constexpr double THICK1 = 7, THICK3 = 1.6;
    constexpr double LENGTH2 = 0.4;
    const double LENGTH4 = doubleEnded ? 0.5 : 0.8;
    double delta = M_PI / 6.0;
    double arrowDist = thickness * THICK1;
    if (slimness >= THICK1 / LENGTH2) {
        // The head is not too long compared to the line
    } else if (slimness >= THICK3 / LENGTH2) {
        // The head is not too short compared to the thickness
        arrowDist = lineLength * LENGTH2;
    } else if (slimness >= THICK3 / LENGTH4) {
        // The head is not too thick compared to the line: widen the angle to keep it visible
        arrowDist = thickness * THICK3;
        delta = (1 + (slimness - THICK3 / LENGTH2) / (THICK3 / LENGTH4 - THICK3 / LENGTH2)) * M_PI / 6.0;
        arrowDist *= std::sin(M_PI / 6.0) / std::sin(delta);
    } else {
        // Shrink gracefully
        arrowDist = lineLength * LENGTH4;
        delta = M_PI / 3.0;
        arrowDist *= std::sin(M_PI / 6.0) / std::sin(M_PI / 3.0);
    }

    const double angle = std::atan2(c.y() - start.y(), c.x() - start.x());
    auto leg = [&](const QPointF& tip, double direction, double legAngle) {
        return QPointF(tip.x() + direction * arrowDist * std::cos(legAngle),
                       tip.y() + direction * arrowDist * std::sin(legAngle));
    };

    QList<QPointF> shape;
    shape.append(start);
    if (doubleEnded) {
        shape.append(leg(start, 1, angle + delta));
        shape.append(start);
        shape.append(leg(start, 1, angle - delta));
        shape.append(start);
    }
    shape.append(c);
    shape.append(leg(c, -1, angle + delta));
    shape.append(c);
    shape.append(leg(c, -1, angle - delta));
    shape.append(c);
    return shape;
}

QList<QPointF> Shapes::coordinateSystem(const QPointF& start, const QPointF& current, const Snapper& snapper,
                                        const Modifiers& modifiers) {
    const QPointF c = snapper.snapToGrid(current, modifiers.alt);
    double width = c.x() - start.x();
    double height = c.y() - start.y();

    if (modifiers.shift) {
        // Axes of the same length
        const int signW = width > 0 ? 1 : -1;
        const int signH = height > 0 ? 1 : -1;
        width = std::max(width * signW, height * signH) * signW;
        height = (width * signW) * signH;
    }

    if (!modifiers.control) {
        // The origin is where the pointer goes vertically
        return {start, QPointF(start.x(), start.y() + height), QPointF(start.x() + width, start.y() + height)};
    }
    // The origin is the starting point
    return {QPointF(start.x(), start.y() + height), start, QPointF(start.x() + width, start.y())};
}

QList<QPointF> Shapes::spline(const QList<QPointF>& knots, const QList<QPointF>& tangents) {
    QList<QPointF> result;
    if (knots.isEmpty() || knots.size() != tangents.size()) {
        return result;
    }
    for (qsizetype i = 0; i + 1 < knots.size(); ++i) {
        const SplineSegment segment{PathPoint(knots[i]), PathPoint(knots[i] + tangents[i]),
                                    PathPoint(knots[i + 1] - tangents[i + 1]), PathPoint(knots[i + 1])};
        for (const PathPoint& p: segment.toPointSequence()) {
            result.append(p.pos());
        }
    }
    result.append(knots.last());
    return result;
}
