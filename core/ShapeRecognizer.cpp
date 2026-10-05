#include "ShapeRecognizer.h"

#include <array>
#include <cmath>

namespace {

constexpr int MAX_POLYGON_SIDES = 4;
constexpr double LINE_POINT_DIST2_THRESHOLD = 15;  // largest squared distance of the last point to the line
constexpr double SEGMENT_MAX_DET = 0.045;          // largest score of a side of a polygon (ideal line: 0)
constexpr double LINE_MAX_DET = 0.015;             // largest score of a single line, stricter than for polygons
constexpr double CIRCLE_MIN_DET = 0.95;            // smallest score of a circle (ideal circle: 1)
constexpr double CIRCLE_MAX_SCORE = 0.10;          // largest deviation of a circle (ideal circle: 0)
constexpr double SLANT_TOLERANCE = 5 * M_PI / 180;             // slants up to 5° are ignored
constexpr double TRIANGLE_LINEAR_TOLERANCE = 0.3;              // gap at the vertices of triangles
constexpr double RECTANGLE_ANGLE_TOLERANCE = 15 * M_PI / 180;  // angles of rectangles
constexpr double RECTANGLE_LINEAR_TOLERANCE = 0.20;            // gap at the vertices of rectangles

/// Moments of a polyline, weighted by the length of its segments
class Inertia {
public:
    double centerX() const { return sx / mass; }
    double centerY() const { return sy / mass; }
    double xx() const { return mass <= 0.0 ? 0.0 : (sxx - sx * sx / mass) / mass; }
    double xy() const { return mass <= 0.0 ? 0.0 : (sxy - sx * sy / mass) / mass; }
    double yy() const { return mass <= 0.0 ? 0.0 : (syy - sy * sy / mass) / mass; }
    double getMass() const { return mass; }

    double rad() const {
        const double sum = xx() + yy();
        return sum <= 0.0 ? 0.0 : std::sqrt(sum);
    }

    /// 0 for a line, 1 for a circle
    double det() const {
        const double ixx = xx();
        const double iyy = yy();
        const double ixy = xy();
        if (mass <= 0.0 || ixx + iyy <= 0.0) {
            return 0.0;
        }
        return 4 * (ixx * iyy - ixy * ixy) / (ixx + iyy) / (ixx + iyy);
    }

    void increase(const QPointF& p1, const QPointF& p2, int coef) {
        const double dm = coef * std::hypot(p2.x() - p1.x(), p2.y() - p1.y());
        mass += dm;
        sx += dm * p1.x();
        sy += dm * p1.y();
        sxx += dm * p1.x() * p1.x();
        syy += dm * p1.y() * p1.y();
        sxy += dm * p1.x() * p1.y();
    }

    /// The points first to last (inclusive)
    void calc(const QList<QPointF>& points, int first, int last) {
        *this = Inertia();
        for (int i = first; i < last; ++i) {
            increase(points[i], points[i + 1], 1);
        }
    }

private:
    double mass = 0;
    double sx = 0;
    double sy = 0;
    double sxx = 0;
    double sxy = 0;
    double syy = 0;
};

/// A straight part of a stroke
struct RecoSegment {
    int startpt = 0;
    int endpt = 0;
    double xcenter = 0;
    double ycenter = 0;
    double angle = 0;
    double radius = 0;
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
    bool reversed = false;

    QPointF calcEdgeIsect(const RecoSegment& r2) const {
        double t = (r2.xcenter - xcenter) * std::sin(r2.angle) - (r2.ycenter - ycenter) * std::cos(r2.angle);
        t /= std::sin(r2.angle - angle);
        return QPointF(xcenter + t * std::cos(angle), ycenter + t * std::sin(angle));
    }

    void calcSegmentGeometry(const QList<QPointF>& pt, int start, int end, const Inertia& s) {
        xcenter = s.centerX();
        ycenter = s.centerY();
        const double a = s.xx();
        const double b = s.xy();
        const double c = s.yy();
        // The largest angle of the quadratic form of the inertia solves tan(2t) = 2b / (a - c)
        angle = std::atan2(2 * b, a - c) / 2;
        radius = std::sqrt(3 * (a + c));

        double lmin = 0;
        double lmax = 0;
        for (int i = start; i <= end; ++i) {
            const double l = (pt[i].x() - xcenter) * std::cos(angle) + (pt[i].y() - ycenter) * std::sin(angle);
            lmin = std::min(lmin, l);
            lmax = std::max(lmax, l);
        }
        x1 = xcenter + lmin * std::cos(angle);
        y1 = ycenter + lmin * std::sin(angle);
        x2 = xcenter + lmax * std::cos(angle);
        y2 = ycenter + lmax * std::sin(angle);
    }
};

double dist2(const QPointF& p, const QPointF& q) {
    const double dx = p.x() - q.x();
    const double dy = p.y() - q.y();
    return dx * dx + dy * dy;
}

/**
 * Checks if the points start to finish (inclusive) are a polygonal line with at most nsides sides.
 * @return the number of sides, 0 if it is not one
 */
int findPolygonal(const QList<QPointF>& pt, int start, int finish, int nsides, int* breaks, Inertia* ss) {
    Inertia s;
    int i1 = 0, i2 = 0, n1 = 0, n2 = 0;

    if (finish == start || nsides <= 0) {
        return 0;
    }
    if (finish - start < 5) {
        nsides = 1;  // too small for a polygon
    }

    // Look for a linear piece that is big enough
    int k = 0;
    for (; k < nsides; ++k) {
        i1 = start + (k * (finish - start)) / nsides;
        i2 = start + ((k + 1) * (finish - start)) / nsides;
        s.calc(pt, i1, i2);
        if (s.det() < SEGMENT_MAX_DET) {
            break;
        }
    }
    if (k == nsides) {
        return 0;
    }

    // Grow the linear piece
    while (true) {
        double det1 = 1.0;
        double det2 = 1.0;
        Inertia s1;
        Inertia s2;
        if (i1 > start) {
            s1 = s;
            s1.increase(pt[i1 - 1], pt[i1], 1);
            det1 = s1.det();
        }
        if (i2 < finish) {
            s2 = s;
            s2.increase(pt[i2], pt[i2 + 1], 1);
            det2 = s2.det();
        }
        if (det1 < det2 && det1 < SEGMENT_MAX_DET) {
            i1--;
            s = s1;
        } else if (det2 < det1 && det2 < SEGMENT_MAX_DET) {
            i2++;
            s = s2;
        } else {
            break;
        }
    }

    if (i1 > start) {
        n1 = findPolygonal(pt, start, i1, (i2 == finish) ? (nsides - 1) : (nsides - 2), breaks, ss);
        if (n1 == 0) {
            return 0;
        }
    }
    breaks[n1] = i1;
    breaks[n1 + 1] = i2;
    ss[n1] = s;

    if (i2 < finish) {
        n2 = findPolygonal(pt, i2, finish, nsides - n1 - 1, breaks + n1 + 1, ss + n1 + 1);
        if (n2 == 0) {
            return 0;
        }
    }
    return n1 + n2 + 1;
}

/// Improves the polygon found by findPolygonal() by moving the breaks between its sides
void optimizePolygonal(const QList<QPointF>& pt, int nsides, int* breaks, Inertia* ss) {
    for (int i = 1; i < nsides; ++i) {
        double cost = ss[i - 1].det() * ss[i - 1].det() + ss[i].det() * ss[i].det();
        Inertia s1 = ss[i - 1];
        Inertia s2 = ss[i];
        bool improved = false;
        while (breaks[i] > breaks[i - 1] + 1) {
            // Try moving the break to the left
            s1.increase(pt[breaks[i] - 1], pt[breaks[i] - 2], -1);
            s2.increase(pt[breaks[i] - 1], pt[breaks[i] - 2], 1);
            const double newcost = s1.det() * s1.det() + s2.det() * s2.det();
            if (newcost >= cost) {
                break;
            }
            improved = true;
            cost = newcost;
            breaks[i]--;
            ss[i - 1] = s1;
            ss[i] = s2;
        }
        if (improved) {
            continue;
        }
        s1 = ss[i - 1];
        s2 = ss[i];
        while (breaks[i] < breaks[i + 1] - 1) {
            // Try moving the break to the right
            s1.increase(pt[breaks[i]], pt[breaks[i] + 1], 1);
            s2.increase(pt[breaks[i]], pt[breaks[i] + 1], -1);
            const double newcost = s1.det() * s1.det() + s2.det() * s2.det();
            if (newcost >= cost) {
                break;
            }
            cost = newcost;
            breaks[i]++;
            ss[i - 1] = s1;
            ss[i] = s2;
        }
    }
}

std::optional<QList<QPointF>> tryTriangle(RecoSegment* rs) {
    // Orient the sides so that each one points towards the next one
    for (int i = 0; i <= 2; ++i) {
        RecoSegment& r1 = rs[i];
        const RecoSegment& r2 = rs[(i + 1) % 3];
        const QPointF P(r1.x1, r1.y1);
        const QPointF Q(r1.x2, r1.y2);
        const QPointF R(r2.x1, r2.y1);
        const QPointF S(r2.x2, r2.y2);
        r1.reversed = std::min(dist2(P, R), dist2(P, S)) < std::min(dist2(Q, R), dist2(Q, S));
    }
    // The ends of neighbouring sides have to be close to each other
    for (int i = 0; i <= 2; ++i) {
        const RecoSegment& r1 = rs[i];
        const RecoSegment& r2 = rs[(i + 1) % 3];
        const double dist = std::hypot((r1.reversed ? r1.x1 : r1.x2) - (r2.reversed ? r2.x2 : r2.x1),
                                       (r1.reversed ? r1.y1 : r1.y2) - (r2.reversed ? r2.y2 : r2.y1));
        if (dist > TRIANGLE_LINEAR_TOLERANCE * (r1.radius + r2.radius)) {
            return std::nullopt;
        }
    }

    QList<QPointF> points;
    for (int i = 0; i <= 2; ++i) {
        points.append(rs[i].calcEdgeIsect(rs[(i + 1) % 3]));
    }
    points.append(points.first());
    return points;
}

std::optional<QList<QPointF>> tryRectangle(RecoSegment* rs) {
    // The sides have to be at right angles and the vertices have to match roughly
    double avgAngle = 0.;
    for (int i = 0; i <= 3; ++i) {
        RecoSegment& r1 = rs[i];
        const RecoSegment& r2 = rs[(i + 1) % 4];
        if (std::abs(std::abs(r1.angle - r2.angle) - M_PI / 2) > RECTANGLE_ANGLE_TOLERANCE) {
            return std::nullopt;
        }
        avgAngle += r1.angle;
        if (r2.angle > r1.angle) {
            avgAngle += (i + 1) * M_PI / 2;
        } else {
            avgAngle -= (i + 1) * M_PI / 2;
        }
        // Does r1 point away from r2 instead of towards it?
        r1.reversed = ((r1.x2 - r1.x1) * (r2.xcenter - r1.xcenter) + (r1.y2 - r1.y1) * (r2.ycenter - r1.ycenter)) < 0;
    }
    for (int i = 0; i <= 3; ++i) {
        const RecoSegment& r1 = rs[i];
        const RecoSegment& r2 = rs[(i + 1) % 4];
        const double dist = std::hypot((r1.reversed ? r1.x1 : r1.x2) - (r2.reversed ? r2.x2 : r2.x1),
                                       (r1.reversed ? r1.y1 : r1.y2) - (r2.reversed ? r2.y2 : r2.y1));
        if (dist > RECTANGLE_LINEAR_TOLERANCE * (r1.radius + r2.radius)) {
            return std::nullopt;
        }
    }

    // A rectangle of the right size and slope
    avgAngle = avgAngle / 4;
    if (std::abs(avgAngle) < SLANT_TOLERANCE) {
        avgAngle = 0.;
    }
    if (std::abs(avgAngle) > M_PI / 2 - SLANT_TOLERANCE) {
        avgAngle = M_PI / 2;
    }
    for (int i = 0; i <= 3; ++i) {
        rs[i].angle = avgAngle + i * M_PI / 2;
    }
    QList<QPointF> points;
    for (int i = 0; i <= 3; ++i) {
        points.append(rs[i].calcEdgeIsect(rs[(i + 1) % 4]));
    }
    points.append(points.first());
    return points;
}

std::optional<QList<QPointF>> tryLine(const QList<QPointF>& pt, RecoSegment& rs) {
    bool aligned = true;
    if (std::abs(rs.angle) < SLANT_TOLERANCE) {  // nearly horizontal
        rs.angle = 0.0;
        rs.y1 = rs.y2 = rs.ycenter;
    } else if (std::abs(rs.angle) > M_PI / 2 - SLANT_TOLERANCE) {  // nearly vertical
        rs.angle = (rs.angle > 0) ? (M_PI / 2) : (-M_PI / 2);
        rs.x1 = rs.x2 = rs.xcenter;
    } else {
        aligned = false;
    }

    const QPointF P(rs.x1, rs.y1);
    const QPointF Q(rs.x2, rs.y2);
    if (aligned) {
        return QList<QPointF>{P, Q};
    }
    // Keep the ends of the stroke if its last point is far from the fitted line
    const QPointF& last = pt.last();
    const double dx = Q.x() - P.x();
    const double dy = Q.y() - P.y();
    const double num = dy * last.x() - dx * last.y() + Q.x() * P.y() - Q.y() * P.x();
    if (num * num / (dy * dy + dx * dx) < LINE_POINT_DIST2_THRESHOLD) {
        return QList<QPointF>{P, Q};
    }
    return QList<QPointF>{pt.first(), pt.last()};
}

std::optional<QList<QPointF>> tryCircle(const QList<QPointF>& pt) {
    Inertia s;
    s.calc(pt, 0, static_cast<int>(pt.size()) - 1);
    if (s.det() <= CIRCLE_MIN_DET) {
        return std::nullopt;
    }

    // The mean deviation of the points from the circle
    const double r0 = s.rad();
    const double divisor = s.getMass() * r0;
    if (divisor == 0) {
        return std::nullopt;
    }
    const double x0 = s.centerX();
    const double y0 = s.centerY();
    double sum = 0.0;
    for (qsizetype i = 0; i + 1 < pt.size(); ++i) {
        const double dm = std::hypot(pt[i + 1].x() - pt[i].x(), pt[i + 1].y() - pt[i].y());
        sum += dm * std::abs(std::hypot(pt[i].x() - x0, pt[i].y() - y0) - r0);
    }
    if (sum / divisor >= CIRCLE_MAX_SCORE) {
        return std::nullopt;
    }

    const int npts = std::max(static_cast<int>(2 * r0), 24);
    QList<QPointF> points;
    for (int i = 0; i <= npts; ++i) {
        points.append(QPointF(x0 + r0 * std::cos((2 * M_PI * i) / npts), y0 + r0 * std::sin((2 * M_PI * i) / npts)));
    }
    return points;
}

std::optional<QList<QPointF>> recognizePoints(const QList<QPointF>& pt) {
    Inertia ss[MAX_POLYGON_SIDES];
    int brk[MAX_POLYGON_SIDES + 1] = {0};

    // First see if it is a polygon
    const int n = findPolygonal(pt, 0, static_cast<int>(pt.size()) - 1, MAX_POLYGON_SIDES, brk, ss);
    if (n > 0) {
        optimizePolygonal(pt, n, brk, ss);
        std::array<RecoSegment, MAX_POLYGON_SIDES> rs;
        for (int i = 0; i < n; ++i) {
            rs[static_cast<size_t>(i)].startpt = brk[i];
            rs[static_cast<size_t>(i)].endpt = brk[i + 1];
            rs[static_cast<size_t>(i)].calcSegmentGeometry(pt, brk[i], brk[i + 1], ss[i]);
        }

        std::optional<QList<QPointF>> result;
        if (n == 3 && (result = tryTriangle(rs.data()))) {
            return result;
        }
        if (n == 4 && (result = tryRectangle(rs.data()))) {
            return result;
        }
        if (n == 1 && ss[0].det() < LINE_MAX_DET) {
            return tryLine(pt, rs[0]);
        }
    }

    // Not a polygon: maybe a circle?
    return tryCircle(pt);
}

}  // namespace

std::optional<Stroke> ShapeRecognizer::recognize(const Stroke& stroke, double minSize) {
    const QList<QPointF>& pt = stroke.points;
    if (pt.size() < 3) {
        return std::nullopt;
    }
    // Large enough to make a shape out of it?
    double minX = pt.first().x(), maxX = minX, minY = pt.first().y(), maxY = minY;
    for (const QPointF& p: pt) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    if (std::hypot(maxX - minX, maxY - minY) < minSize) {
        return std::nullopt;
    }

    const std::optional<QList<QPointF>> points = recognizePoints(pt);
    if (!points) {
        return std::nullopt;
    }
    for (const QPointF& p: *points) {
        if (!std::isfinite(p.x()) || !std::isfinite(p.y())) {
            return std::nullopt;  // parallel sides have no intersection
        }
    }

    Stroke shape = stroke;
    shape.points = *points;
    if (stroke.hasPressure()) {
        // The average width of the stroke
        double sum = 0;
        for (double width: stroke.widths) {
            sum += width;
        }
        shape.width = sum / static_cast<double>(stroke.widths.size());
    }
    shape.widths.clear();
    shape.updateBounds();
    return shape;
}
