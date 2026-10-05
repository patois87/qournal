/*
 * Qournal
 *
 * What a selection needs from the model: which elements an area selects, which element is at a position, and
 * how elements are transformed. Ported from Xournal++ (Selector, Element::isInSelection, Element::scale/rotate).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <vector>

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QTransform>

#include "Document.h"

/// The area drawn with a selection tool: a rectangle or the region inside a lasso
class SelectionArea {
public:
    /// An area that reaches a border of the page is extended beyond it, to catch what sticks out of the page
    static constexpr double EDGE_TOUCHING_THRESHOLD = 1.0;

    static SelectionArea rectangle(const QPointF& start, const QPointF& end);
    static SelectionArea lasso(const QList<QPointF>& points);

    bool contains(const QPointF& p) const;
    void extendAtPageEdges(double pageWidth, double pageHeight);

    const QList<QPointF>& boundary() const { return m_boundary; }
    QRectF bounds() const { return QRectF(m_min, m_max); }
    /// Whether the area is so small that the user only tapped. @param maxDistance in page units
    bool isTap(double maxDistance) const;

private:
    void updateBounds();

    bool m_rectangle = true;
    QList<QPointF> m_boundary;
    QPointF m_min;
    QPointF m_max;
};

namespace Selection {

/// Elements closer than this to a position are found by elementAt(), in page units
constexpr double ACTION_RADIUS = 5.0;

/// The four corners of the area a text, image or link covers
QList<QPointF> elementCorners(const Element& element);

/// A stroke is selected if all its points are in the area; other elements if their four corners are
bool isInArea(const Element& element, const SelectionArea& area);

/// Distance of a position to an element: 0 on it
double distanceTo(const Element& element, const QPointF& pos);

/// @return the index of the front-most element of the layer at the position, the closest one within ACTION_RADIUS
std::optional<size_t> elementAt(const Layer& layer, const QPointF& pos);

/**
 * Applies a transformation of the page to an element.
 * @param widthFactor factor for the width of strokes, e.g. the geometric mean of the scale factors
 */
void transform(Element& element, const QTransform& transformation, double widthFactor = 1.0);

/// Order of elements of a layer, see arrange()
enum class OrderChange { BringToFront, BringForward, SendBackward, SendToBack };

/**
 * The new places of selected elements in their layer.
 * @param indices the places of the selected elements, ascending
 * @param count number of elements of the layer
 * @return for each selected element its new place, ascending; the other elements keep their order around them
 */
std::vector<size_t> arrange(const std::vector<size_t>& indices, size_t count, OrderChange change);

}  // namespace Selection
