/*
 * Qournal
 *
 * Recognizes lines, triangles, rectangles and circles in hand drawn strokes, ported from Xournal++
 * (src/core/control/shaperecognizer), which took it from Xournal.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>

#include "Document.h"

namespace ShapeRecognizer {

/// Strokes with a smaller diagonal are not turned into shapes
constexpr double DEFAULT_MIN_SIZE = 40.0;

/**
 * @return the shape the stroke looks like, with the style of the stroke and a constant width, or nothing if it
 *         does not look like a shape
 */
std::optional<Stroke> recognize(const Stroke& stroke, double minSize = DEFAULT_MIN_SIZE);

}  // namespace ShapeRecognizer
