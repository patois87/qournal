/*
 * Qournal
 *
 * The shapes of the drawing tools: line, rectangle, ellipse, arrows, coordinate system and spline.
 * Ported from the shape handlers of Xournal++ (src/core/control/tools).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QList>
#include <QPointF>

#include "Snapping.h"

namespace Shapes {

enum class Type { Line, Rectangle, Ellipse, Arrow, DoubleArrow, CoordinateSystem };

/// State of the modifier keys while a shape is drawn
struct Modifiers {
    bool alt = false;      ///< toggles snapping
    bool shift = false;    ///< square or circle
    bool control = false;  ///< draw from the center; flips the coordinate system
};

/**
 * The points of a shape that is dragged from start to current.
 * @param start where the pointer was pressed, already snapped
 * @param thickness width of the stroke; the head of an arrow depends on it
 */
QList<QPointF> create(Type type, const QPointF& start, const QPointF& current, const Snapper& snapper,
                      const Modifiers& modifiers, double thickness);

QList<QPointF> line(const QPointF& start, const QPointF& current, const Snapper& snapper, const Modifiers& modifiers);
QList<QPointF> rectangle(const QPointF& start, const QPointF& current, const Snapper& snapper,
                         const Modifiers& modifiers);
QList<QPointF> ellipse(const QPointF& start, const QPointF& current, const Snapper& snapper,
                       const Modifiers& modifiers);
QList<QPointF> arrow(const QPointF& start, const QPointF& current, const Snapper& snapper, const Modifiers& modifiers,
                     double thickness, bool doubleEnded);
QList<QPointF> coordinateSystem(const QPointF& start, const QPointF& current, const Snapper& snapper,
                                const Modifiers& modifiers);

/**
 * The points of a spline through the knots. The tangent of a knot points to the control point after it; the control
 * point before it lies opposite.
 */
QList<QPointF> spline(const QList<QPointF>& knots, const QList<QPointF>& tangents);

}  // namespace Shapes
