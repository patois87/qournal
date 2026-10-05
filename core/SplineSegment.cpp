#include "SplineSegment.h"

#include <cmath>

namespace {

constexpr double FLATNESS_TOLERANCE = 1.0001;
constexpr double MIN_KNOT_DISTANCE = 0.3;
constexpr double MAX_WIDTH_VARIATION = 0.1;

double distance(const PathPoint& p, const PathPoint& q) { return std::hypot(p.x - q.x, p.y - q.y); }

PathPoint interpolate(const PathPoint& p, const PathPoint& q, double t) {
    return PathPoint(p.x * (1 - t) + q.x * t, p.y * (1 - t) + q.y * t);
}

bool isFlatEnough(const SplineSegment& s, bool usePressure) {
    const double l1 = distance(s.firstKnot, s.firstControlPoint);
    const double l2 = distance(s.firstControlPoint, s.secondControlPoint);
    const double l3 = distance(s.secondControlPoint, s.secondKnot);
    const double l = distance(s.firstKnot, s.secondKnot);
    return l < MIN_KNOT_DISTANCE ||
           (l1 + l2 + l3 < FLATNESS_TOLERANCE * l &&
            (!usePressure || std::abs(s.firstKnot.z - s.secondKnot.z) <= MAX_WIDTH_VARIATION));
}

void appendPoints(const SplineSegment& s, bool usePressure, std::vector<PathPoint>& result, int depth) {
    // The depth limit only matters for degenerate input (e.g. not-a-number coordinates)
    if (depth > 24 || isFlatEnough(s, usePressure)) {
        result.push_back(s.firstKnot);
        return;
    }
    // De Casteljau's algorithm
    const PathPoint b0 = interpolate(s.firstKnot, s.firstControlPoint, 0.5);
    const PathPoint b1 = interpolate(s.firstControlPoint, s.secondControlPoint, 0.5);
    const PathPoint b2 = interpolate(s.secondControlPoint, s.secondKnot, 0.5);
    const PathPoint c0 = interpolate(b0, b1, 0.5);
    const PathPoint c1 = interpolate(b1, b2, 0.5);
    PathPoint d0 = interpolate(c0, c1, 0.5);
    if (usePressure) {
        // The width does not change linearly with the length this way, which is good enough
        d0.z = 0.5 * s.firstKnot.z + 0.5 * s.secondKnot.z;
    }
    appendPoints(SplineSegment{s.firstKnot, b0, c0, d0}, usePressure, result, depth + 1);
    appendPoints(SplineSegment{d0, c1, b2, s.secondKnot}, usePressure, result, depth + 1);
}

}  // namespace

std::vector<PathPoint> SplineSegment::toPointSequence(bool usePressure) const {
    std::vector<PathPoint> result;
    appendPoints(*this, usePressure, result, 0);
    return result;
}
