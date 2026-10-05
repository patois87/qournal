/*
 * Qournal
 *
 * A cubic Bézier segment and its conversion into points, ported from Xournal++ (SplineSegment)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include <QPointF>

/// A point of a stroke that is being built: z is the width of the segment starting at it, negative for none
struct PathPoint {
    static constexpr double NO_PRESSURE = -1.0;

    PathPoint() = default;
    PathPoint(double x, double y, double z = NO_PRESSURE): x(x), y(y), z(z) {}
    PathPoint(const QPointF& p, double z = NO_PRESSURE): x(p.x()), y(p.y()), z(z) {}

    QPointF pos() const { return QPointF(x, y); }

    double x = 0;
    double y = 0;
    double z = NO_PRESSURE;
};

struct SplineSegment {
    PathPoint firstKnot;
    PathPoint firstControlPoint;
    PathPoint secondControlPoint;
    PathPoint secondKnot;

    /**
     * Points on the segment, close enough to each other to look smooth. The first knot is part of the result,
     * the second knot is not.
     * @param usePressure interpolate the widths of the knots as well
     */
    std::vector<PathPoint> toPointSequence(bool usePressure = false) const;
};
