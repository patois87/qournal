/*
 * Qournal
 *
 * Recognizes the parts of a mind map drawn by hand: a loop around writing becomes a node, an ellipse or a rectangle
 * fitted to the writing; a line under writing that comes from a node or a branch becomes an underlined node; a
 * stroke from a node to another one or into empty space becomes a branch, a smooth curve cut at the outlines. Only
 * ordinary strokes are made: which stroke is a node is derived from the geometry of the layer.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <optional>
#include <vector>

#include <QPolygonF>
#include <QRectF>

#include "Document.h"

namespace MindMap {

struct Options {
    enum class NodeShape { AsDrawn, Ellipse, Rectangle };

    NodeShape nodeShape = NodeShape::AsDrawn;  ///< of nodes drawn as loops
    double margin = 6;                         ///< between the writing and the outline of a node
    double underlineGap = 3;                   ///< between the writing and the line under it
    double attachTolerance = 10;  ///< how far from an outline the end of a branch may be, besides the width
    double minBranchLength = 20;  ///< shorter strokes are writing
    /// The diagonal of a loop around nothing that is a node; smaller ones are writing
    double emptyNodeMinSize = 40;
    bool taper = true;   ///< branches are thicker at the node they come from
    bool curved = true;  ///< branches follow the curve drawn; if not, they are straight
    /// Each main branch, from a node no branch leads to, gets a colour of its own, and what grows from it takes it
    bool branchColors = false;
    bool scribbleErases = true;  ///< scribbling over strokes, texts or images deletes them
};

/// The colours of main branches, in the order they are given: the strong colours of the palette of Xournal++
QList<QColor> branchColors();

enum class Shape { Ellipse, Rectangle };

/// Whether the points go round once and come back to where they started, as a loop drawn around something
bool isLoop(const QList<QPointF>& points);

/// The shape a loop was drawn in: a rectangle only if it is clearly angular
Shape shapeOf(const QList<QPointF>& points);

/// The outline of a node of the shape around the writing, closed (the first point is the last one)
QList<QPointF> outline(Shape shape, const QRectF& writing, double margin);

/**
 * A smooth curve from start to end that follows the points drawn between them: a cubic Bézier curve, or a straight
 * line if they are nearly straight. As points, close enough to each other to look smooth
 */
QList<QPointF> smoothPath(const QPointF& start, const QList<QPointF>& drawn, const QPointF& end);

/// Whether the points end in an arrow head as Xournal++ draws it: back from the tip and to it again, twice
bool hasArrowHead(const QList<QPointF>& points);

/// A node found on a layer
struct Node {
    enum class Kind {
        Outline,   ///< a loop around the writing
        Underline  ///< a line under the writing, coming from its parent
    };

    Kind kind = Kind::Outline;
    size_t element = 0;  ///< the stroke of the outline or of the line in the layer
    /// The outline, closed; for an underlined node the line under the writing, from where its parent joins it to
    /// where its branches start
    QPolygonF outline;
    std::vector<size_t> contents;  ///< the writing: the elements of the layer inside it or above the line
};

/**
 * The nodes of a layer: the loops that enclose writing but no other node, large loops around nothing, and the lines
 * under writing. Loops around nodes are groups, small loops around nothing are writing (an "o", a "0").
 */
std::vector<Node> findNodes(const Layer& layer, const Options& options = {});

struct Result {
    enum class Kind {
        Ink,
        Node,
        Underline,
        Branch,
        CrossLink,  ///< an arrow between nodes, drawn with its head
        ArrowHead,  ///< a head put on a branch or a cross link afterwards: the stroke itself goes
        Group,      ///< a loop around nodes, smoothed
        Erase       ///< a scribble over elements: they and the stroke itself go
    };

    Kind kind = Kind::Ink;
    Stroke stroke;     ///< what replaces the stroke; the stroke itself for ink
    bool keep = true;  ///< false if the stroke is removed
    /// For a node: the elements of the layer that are its writing; for a group all that is in it
    std::vector<size_t> contents;
    /// For a branch, a cross link or an underlined node: the stroke in the layer it starts at, a node or the end of a
    /// branch
    std::optional<size_t> from;
    /// For a branch or a cross link: the node in the layer its end joins; for an arrow head the line it is put on
    std::optional<size_t> to;
    /**
     * Other strokes of the layer that change with it, by their index: branches that join a node drawn at their
     * end, a node that grows around writing put into it and the strokes that join it, a line under writing that is
     * joined later, the line an arrow head is put on
     */
    std::vector<std::pair<size_t, Stroke>> changes;
    std::vector<size_t> removed;  ///< for a scribble: the elements of the layer it deletes, ascending
};

/**
 * What a stroke that has just been drawn is, and what replaces it
 * @param layer the layer the stroke is drawn on, without the stroke
 */
Result recognize(const Stroke& stroke, const Layer& layer, const Options& options = {});

/// A closed, smooth curve through a loop drawn by hand, which goes round once
QList<QPointF> smoothLoop(const QList<QPointF>& points);

/// Whether the points go back and forth several times, as in scribbling something out
bool isScribble(const QList<QPointF>& points);

/**
 * The elements together with what belongs to them: the writing of the nodes among them, and all that is inside the
 * groups among them. Ascending
 */
std::vector<size_t> withContents(const Layer& layer, const std::vector<size_t>& elements,
                                 const Options& options = {});

/**
 * How the strokes that join nodes change when nodes were moved, turned or scaled: their ends follow the nodes, and
 * they bend smoothly between their ends; ends at nodes that stayed stay
 * @param before the layer before the change
 * @param after the layer after it, which has the same elements
 * @param moved the elements that were changed
 * @return the strokes to replace in after, by their index
 */
std::vector<std::pair<size_t, Stroke>> follow(const Layer& before, const Layer& after,
                                              const std::vector<size_t>& moved, const Options& options = {});

}  // namespace MindMap
