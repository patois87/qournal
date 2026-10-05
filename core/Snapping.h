/*
 * Qournal
 *
 * Snapping of positions to the grid of the page and of directions to steps of 15°, ported from Xournal++
 * (Snapping, SnapToGridInputHandler)
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QPointF>

#include "Document.h"

struct SnapSettings {
    bool grid = true;
    bool rotation = true;
    double gridSize = 14.17;  ///< 5 mm
    double gridTolerance = 0.50;
    double rotationTolerance = 0.30;
};

namespace Snapping {

double snapVertically(double y, double gridSize, double tolerance, double yOffset);
double snapHorizontally(double x, double gridSize, double tolerance, double xOffset);
QPointF snapToGrid(const QPointF& pos, double columnSpacing, double rowSpacing, double tolerance, double xOffset,
                   double yOffset);
/// Snaps an angle to the next multiple of 15°
double snapAngle(double radian, double tolerance);
/// Snaps the direction from center to pos, keeping the distance
QPointF snapRotation(const QPointF& pos, const QPointF& center, double tolerance);
/// Projection of pos on the line through first and second
QPointF projToLine(const QPointF& pos, const QPointF& first, const QPointF& second);
/// Distance of pos to the segment between first and second
double distanceLine(const QPointF& pos, const QPointF& first, const QPointF& second);

}  // namespace Snapping

/// Snaps according to the settings and to the background of a page: the grid follows its ruling
class Snapper {
public:
    Snapper() = default;
    Snapper(const SnapSettings& settings, const Page& page);

    /// @param alt the Alt key toggles snapping
    QPointF snapToGrid(const QPointF& pos, bool alt = false) const;
    double snapVertically(double y, bool alt = false) const;
    double snapHorizontally(double x, bool alt = false) const;
    double snapAngle(double radian, bool alt = false) const;
    QPointF snapRotation(const QPointF& pos, const QPointF& center, bool alt = false) const;
    /// Snaps the rotation around center, then to the grid
    QPointF snap(const QPointF& pos, const QPointF& center, bool alt = false) const;

private:
    SnapSettings m_settings;
    double m_columnSpacing = 14.17;
    double m_rowSpacing = 14.17;
    double m_xOffset = 0;
    double m_yOffset = 0;
};
