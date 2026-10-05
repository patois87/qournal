/*
 * Qournal
 *
 * Builds a stroke from the points of a pen, ported from Xournal++ (StrokeHandler): points that are too close
 * to each other are dropped, and segments are split where the width changes too fast.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>

#include "Document.h"
#include "SplineSegment.h"

class StrokeBuilder {
public:
    static constexpr double NO_PRESSURE = PathPoint::NO_PRESSURE;
    /// Smaller movements do not add a point (page units)
    static constexpr double MOTION_THRESHOLD = 0.3;
    /// Largest change of the width from one segment to the next
    static constexpr double MAX_WIDTH_VARIATION = 0.3;

    /**
     * Starts a stroke.
     * @param style tool, colour, width, ... of the stroke; its points are ignored
     * @param pressure 0 to 1, or NO_PRESSURE for a stroke of constant width
     */
    void begin(const Stroke& style, const QPointF& point, double pressure);
    bool active() const { return m_active; }
    void cancel();

    /// Continues the stroke to a point given with its pressure (0 to 1)
    void paintTo(const QPointF& point, double pressure);
    /// Appends a point as it is; z is the width of the segment that will start at it
    void drawSegmentTo(const PathPoint& point);

    /// Ends the stroke. A stroke with a single point becomes a dot
    Stroke finish(double pressure);

    /// The stroke so far, as it is to be shown
    const Stroke& stroke() const { return m_stroke; }
    bool hasPressure() const { return m_hasPressure; }

    int pointCount() const { return static_cast<int>(m_stroke.points.size()); }
    PathPoint point(int index) const { return PathPoint(m_stroke.points[index], m_z[index]); }
    void setLastPressure(double z);
    void setSecondToLastPressure(double z);

    /// The part of the page that changed since the last call
    QRectF takeDirtyRect();

private:
    void addDirty(const QPointF& a, const QPointF& b, double width);

    Stroke m_stroke;
    QList<double> m_z;  ///< for each point the width of the segment starting at it
    bool m_active = false;
    bool m_hasPressure = false;
    QRectF m_dirty;
};
