#include "StrokeBuilder.h"

#include <algorithm>
#include <cmath>

#include <QLineF>

void StrokeBuilder::begin(const Stroke& style, const QPointF& point, double pressure) {
    m_stroke = style;
    m_stroke.points.clear();
    m_stroke.widths.clear();
    m_z.clear();
    m_dirty = QRectF();
    m_active = true;

    m_hasPressure = pressure != NO_PRESSURE;
    drawSegmentTo(PathPoint(point, m_hasPressure ? pressure * m_stroke.width : NO_PRESSURE));
}

void StrokeBuilder::cancel() {
    m_active = false;
    m_stroke.points.clear();
    m_stroke.widths.clear();
    m_z.clear();
}

void StrokeBuilder::addDirty(const QPointF& a, const QPointF& b, double width) {
    const double pad = std::max(width, m_stroke.width) / 2 + 1;
    m_dirty |= QRectF(a, b).normalized().adjusted(-pad, -pad, pad, pad);
}

QRectF StrokeBuilder::takeDirtyRect() {
    const QRectF rect = m_dirty;
    m_dirty = QRectF();
    return rect;
}

void StrokeBuilder::paintTo(const QPointF& pos, double pressure) {
    if (!m_active) {
        return;
    }
    PathPoint point(pos, pressure);
    if (m_hasPressure && point.z > 0.0) {
        point.z *= m_stroke.width;
    }

    PathPoint endPoint = this->point(pointCount() - 1);
    const double distance = QLineF(pos, endPoint.pos()).length();
    if (distance < MOTION_THRESHOLD) {
        if (pointCount() == 1 && m_hasPressure && endPoint.z < point.z) {
            // Pressing harder on the spot thickens the first point
            setLastPressure(point.z);
        }
        return;
    }
    if (m_hasPressure) {
        const double widthDelta = point.z - endPoint.z;
        if (std::abs(widthDelta) > MAX_WIDTH_VARIATION) {
            // Change the width in several small steps
            const double steps = std::min(std::ceil(std::abs(widthDelta) / MAX_WIDTH_VARIATION),
                                          std::floor(distance / MOTION_THRESHOLD));
            const double stepLength = 1.0 / steps;
            const PathPoint increment((point.x - endPoint.x) * stepLength, (point.y - endPoint.y) * stepLength,
                                      widthDelta * stepLength);
            endPoint.z += increment.z;
            for (int i = 1; i < static_cast<int>(steps); ++i) {  // the last step is done below
                endPoint.x += increment.x;
                endPoint.y += increment.y;
                endPoint.z += increment.z;
                drawSegmentTo(endPoint);
            }
        }
    }
    drawSegmentTo(point);
}

void StrokeBuilder::drawSegmentTo(const PathPoint& point) {
    if (!m_stroke.points.isEmpty()) {
        const double width = m_hasPressure ? m_z.last() : m_stroke.width;
        if (m_hasPressure) {
            m_stroke.widths.append(m_z.last());
        }
        addDirty(m_stroke.points.last(), point.pos(), width);
    } else {
        addDirty(point.pos(), point.pos(), m_hasPressure ? point.z : m_stroke.width);
    }
    m_stroke.points.append(point.pos());
    m_z.append(m_hasPressure ? point.z : NO_PRESSURE);
}

void StrokeBuilder::setLastPressure(double z) {
    if (!m_z.isEmpty()) {
        m_z.last() = z;
        addDirty(m_stroke.points.last(), m_stroke.points.last(), z);
    }
}

void StrokeBuilder::setSecondToLastPressure(double z) {
    const qsizetype count = m_z.size();
    if (count >= 2) {
        m_z[count - 2] = z;
        if (m_hasPressure) {
            m_stroke.widths[count - 2] = z;
        }
        addDirty(m_stroke.points[count - 2], m_stroke.points[count - 1], z);
    }
}

Stroke StrokeBuilder::finish(double pressure) {
    if (m_stroke.points.size() == 1) {
        // A tap: a dot is stored as a segment without length, as Xournal++ does
        if (m_hasPressure) {
            m_z.last() = std::max(m_z.last(), pressure * m_stroke.width);
            m_stroke.widths.append(m_z.last());
        }
        m_stroke.points.append(m_stroke.points.first());
    }
    m_stroke.updateBounds();
    m_active = false;
    return m_stroke;
}
