/*
 * Qournal
 *
 * Setsquare and compass: tools that lie on the page and guide the pen. Ported from Xournal++
 * (model/Setsquare, model/Compass, their controllers and views).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <vector>

#include <QLineF>
#include <QList>
#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <QTransform>

class GeometryTool {
public:
    enum class Type { Setsquare, Compass };

    static constexpr double HALF_CM = 14.17;
    static constexpr double CM = 2. * HALF_CM;

    GeometryTool() = default;
    GeometryTool(Type type, const QPointF& origin);

    Type type() const { return m_type; }
    /// Setsquare: the height of the triangle; compass: the radius. In centimetres
    double height() const { return m_height; }
    double rotation() const { return m_rotation; }
    /// Setsquare: the middle of the hypotenuse; compass: the center. In page coordinates
    QPointF origin() const { return m_origin; }
    double minHeight() const;
    double maxHeight() const;

    /// From the coordinates of the tool (centimetres) to the page
    QTransform matrix() const;
    /// The area of the page the tool covers, by default with the stroke that is drawn with it
    QRectF bounds(bool withStroke = true) const;
    /// @param border in centimetres: positive values enlarge the tool
    bool contains(const QPointF& pagePos, double border = 0) const;

    void translate(const QPointF& offset);
    void rotate(double angle);
    void rotate(double angle, const QPointF& center);
    /// @return false if the tool would get too small or too large
    bool scale(double factor);
    bool scale(double factor, const QPointF& center);
    /**
     * Moves the tool; near a line that is almost parallel or perpendicular to the tool, it turns to match it.
     * @param lines candidates, e.g. the strokes of the page that are straight lines
     */
    void translateWithSnapping(const QPointF& offset, const std::vector<QLineF>& lines);

    /**
     * Starts a stroke if the pen is on a part of the tool that guides it. Setsquare: along the hypotenuse, or from
     * its middle outwards when starting at one of the legs. Compass: an arc along the outline, or the radius.
     * @param filled whether the stroke will be filled: an arc then gets its center as corner
     */
    bool beginStroke(const QPointF& pagePos, bool filled);
    void updateStroke(const QPointF& pagePos);
    bool hasStroke() const { return m_stroke != StrokeKind::None; }
    /// The points of the stroke so far, in page coordinates
    const QList<QPointF>& strokePoints() const { return m_points; }
    void endStroke();

    /// A small cross at the origin
    QList<QPointF> originMark() const;

    /// Paints the tool in page coordinates
    void paint(QPainter& p) const;

private:
    enum class StrokeKind { None, Edge, Radial, Outline };

    QPointF toTool(const QPointF& pagePos) const;
    void paintSetsquare(QPainter& p) const;
    void paintCompass(QPainter& p) const;

    Type m_type = Type::Setsquare;
    double m_height = 8.0;
    double m_rotation = 0;
    QPointF m_origin;

    StrokeKind m_stroke = StrokeKind::None;
    QList<QPointF> m_points;
    bool m_filled = false;
    double m_min = 0;  ///< range of the stroke along the edge, the radius or the outline
    double m_max = 0;
    double m_strokeAngle = 0;
    double m_lastAngle = 0;
};
