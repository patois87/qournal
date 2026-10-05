/*
 * Qournal
 *
 * Geometry of the eraser, ported from Xournal++ (Stroke::intersectWithPaddedBox, ErasableStroke).
 * The eraser is a square. A stroke that enters it loses the part inside a slightly larger square, whose
 * padding depends on the width of the stroke, so that nothing of the erased part stays visible.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <vector>

#include <QPointF>

#include "Document.h"

namespace Eraser {

/// A position on a stroke: on the segment starting at the point index, at the fraction t of its length
struct PathParameter {
    int index = 0;
    double t = 0;

    bool operator==(const PathParameter& o) const { return index == o.index && t == o.t; }
    bool operator<(const PathParameter& o) const { return index < o.index || (index == o.index && t < o.t); }
    bool operator<=(const PathParameter& o) const { return !(o < *this); }
};

/// Whether the stroke touches the eraser square. Used by the eraser that deletes whole strokes
bool intersects(const Stroke& stroke, const QPointF& center, double halfSize);

/**
 * The parts of the stroke that the eraser removes: pairs of parameters (begin, end), in ascending order.
 * @param halfSizeWithPadding half the side of the larger square that is cut out
 */
std::vector<PathParameter> intersectWithPaddedBox(const Stroke& stroke, const QPointF& center, double halfSize,
                                                  double halfSizeWithPadding);

/// The stroke between two parameters, with the style of the original
Stroke section(const Stroke& stroke, const PathParameter& from, const PathParameter& to);

/// What remains of a stroke with at least two points when the given parts are removed, see intersectWithPaddedBox()
std::vector<Stroke> split(const Stroke& stroke, const std::vector<PathParameter>& erased);

/**
 * Erases the part of a stroke under the eraser square.
 * @return the remaining pieces (none if the stroke is erased completely), or nothing if the eraser does not touch
 *         the stroke. A closed stroke that is cut once stays in one piece.
 */
std::optional<std::vector<Stroke>> erase(const Stroke& stroke, const QPointF& center, double halfSize);

}  // namespace Eraser
