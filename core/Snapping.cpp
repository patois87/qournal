#include "Snapping.h"

#include <cmath>

#include "BackgroundConfig.h"

namespace {

constexpr double DEFAULT_RASTER_SIZE = 14.17;  // 5 mm
constexpr double RULED_HEADER_SIZE = 80.0;
constexpr double ANGLE_STEP = M_PI_4 / 3.0;  // 15°

double roundToMultiple(double value, double multiple) { return value - std::remainder(value, multiple); }

double distance(const QPointF& a, const QPointF& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()); }

}  // namespace

double Snapping::snapVertically(double y, double gridSize, double tolerance, double yOffset) {
    const double snapped = roundToMultiple(y - yOffset, gridSize) + yOffset;
    return std::abs(snapped - y) < tolerance * gridSize / 2.0 ? snapped : y;
}

double Snapping::snapHorizontally(double x, double gridSize, double tolerance, double xOffset) {
    const double snapped = roundToMultiple(x - xOffset, gridSize) + xOffset;
    return std::abs(snapped - x) < tolerance * gridSize / 2.0 ? snapped : x;
}

QPointF Snapping::snapToGrid(const QPointF& pos, double columnSpacing, double rowSpacing, double tolerance,
                             double xOffset, double yOffset) {
    const QPointF nearestVertex(roundToMultiple(pos.x() - xOffset, columnSpacing) + xOffset,
                                roundToMultiple(pos.y() - yOffset, rowSpacing) + yOffset);
    // Only within a fraction of the half diagonal of a cell
    const double threshold = 0.5 * std::hypot(columnSpacing, rowSpacing) * tolerance;
    return distance(pos, nearestVertex) < threshold ? nearestVertex : pos;
}

double Snapping::snapAngle(double radian, double tolerance) {
    const double snapped = roundToMultiple(radian, ANGLE_STEP);
    return std::abs(snapped - radian) < (ANGLE_STEP / 2.0) * tolerance ? snapped : radian;
}

QPointF Snapping::snapRotation(const QPointF& pos, const QPointF& center, double tolerance) {
    const double dist = distance(pos, center);
    const double angle = snapAngle(std::atan2(pos.y() - center.y(), pos.x() - center.x()), tolerance);
    return QPointF(center.x() + dist * std::cos(angle), center.y() + dist * std::sin(angle));
}

QPointF Snapping::projToLine(const QPointF& pos, const QPointF& first, const QPointF& second) {
    const double dx = first.x() - second.x();
    const double dy = second.y() - first.y();
    const double scalar = ((first.x() - pos.x()) * dy + (first.y() - pos.y()) * dx) / (dy * dy + dx * dx);
    return QPointF(pos.x() + scalar * dy, pos.y() + scalar * dx);
}

double Snapping::distanceLine(const QPointF& pos, const QPointF& first, const QPointF& second) {
    const QPointF proj = projToLine(pos, first, second);
    if (std::min(first.x(), second.x()) <= proj.x() && proj.x() <= std::max(first.x(), second.x()) &&
        std::min(first.y(), second.y()) <= proj.y() && proj.y() <= std::max(first.y(), second.y())) {
        return distance(pos, proj);
    }
    return std::min(distance(pos, first), distance(pos, second));
}

Snapper::Snapper(const SnapSettings& settings, const Page& page):
        m_settings(settings), m_columnSpacing(settings.gridSize), m_rowSpacing(settings.gridSize) {
    const Background& bg = page.background;
    if (bg.type != Background::Type::Solid) {
        return;
    }
    if (bg.style == u"isodotted" || bg.style == u"isograph") {
        // The grid is the one of the triangles, centered on the page like the ruling
        const double triangleSize = BackgroundConfig(bg.config).number(QStringLiteral("r1"), DEFAULT_RASTER_SIZE);
        if (triangleSize > 0) {
            m_columnSpacing = std::sqrt(3.0) / 2.0 * triangleSize;
            m_rowSpacing = triangleSize / 2.0;
            const int cols = static_cast<int>(std::floor((page.width - 2.0 * triangleSize) / m_columnSpacing));
            const int rows = static_cast<int>(std::floor((page.height - 2.0 * triangleSize) / m_rowSpacing));
            m_xOffset = (page.width - cols * m_columnSpacing) / 2.0;
            m_yOffset = (page.height - rows * m_rowSpacing) / 2.0;
        }
    } else if (bg.style == u"ruled" || bg.style == u"lined") {
        m_yOffset = RULED_HEADER_SIZE;
    }
}

QPointF Snapper::snapToGrid(const QPointF& pos, bool alt) const {
    if (alt == m_settings.grid) {
        return pos;
    }
    return Snapping::snapToGrid(pos, m_columnSpacing, m_rowSpacing, m_settings.gridTolerance, m_xOffset, m_yOffset);
}

double Snapper::snapVertically(double y, bool alt) const {
    if (alt == m_settings.grid) {
        return y;
    }
    return Snapping::snapVertically(y, m_settings.gridSize, m_settings.gridTolerance, m_yOffset);
}

double Snapper::snapHorizontally(double x, bool alt) const {
    if (alt == m_settings.grid) {
        return x;
    }
    return Snapping::snapHorizontally(x, m_settings.gridSize, m_settings.gridTolerance, m_xOffset);
}

double Snapper::snapAngle(double radian, bool alt) const {
    if (alt == m_settings.rotation) {
        return radian;
    }
    return Snapping::snapAngle(radian, m_settings.rotationTolerance);
}

QPointF Snapper::snapRotation(const QPointF& pos, const QPointF& center, bool alt) const {
    if (alt == m_settings.rotation) {
        return pos;
    }
    return Snapping::snapRotation(pos, center, m_settings.rotationTolerance);
}

QPointF Snapper::snap(const QPointF& pos, const QPointF& center, bool alt) const {
    return snapToGrid(snapRotation(pos, center, alt), alt);
}
