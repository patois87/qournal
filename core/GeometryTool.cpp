#include "GeometryTool.h"

#include <algorithm>
#include <cmath>

#include <QFont>
#include <QFontMetricsF>
#include <QPainterPath>

#include "Snapping.h"

namespace {

// All lengths are in centimetres
constexpr double LINE_WIDTH = .02;
constexpr double FONT_SIZE = .2;
constexpr double CIRCLE_RAD = .3;
constexpr double TICK_SMALL = .1;
constexpr double TICK_LARGE = .2;

constexpr double SETSQUARE_INITIAL_HEIGHT = 8.0;
constexpr double SETSQUARE_MIN_HEIGHT = 4.5;
constexpr double SETSQUARE_MAX_HEIGHT = 15.0;
constexpr double DISTANCE_SEMICIRCLE_FROM_LEGS = 1.15;
constexpr double COMPASS_INITIAL_HEIGHT = 3.0;
constexpr double COMPASS_MIN_HEIGHT = 0.5;
constexpr double COMPASS_MAX_HEIGHT = 10.0;

constexpr double MARK_SIZE = 2.;                                    // of the cross at the origin, in points
constexpr double SNAPPING_DISTANCE_TOLERANCE = 5.0;                 // points
constexpr double SNAPPING_ROTATION_TOLERANCE = 3.0 * M_PI / 180.0;  // radians
constexpr double GUIDE_WIDTH = 0.5;                                 // width of the zone at an edge that guides the pen

const QColor RED(255, 0, 0);
const QColor BLUE(0, 0, 255);
const QColor GREEN(0, 128, 0);
const QColor VIOLET(128, 0, 128);
const QColor TURQUOISE(0, 128, 128);
const QColor FILL(51, 51, 51, 26);  // transparent gray

double rad(double degrees) { return degrees * M_PI / 180.; }
double deg(double radians) { return radians * 180. / M_PI; }
double cathete(double hypotenuse, double other) { return std::sqrt(hypotenuse * hypotenuse - other * other); }
double setsquareRadius(double height) { return height / std::sqrt(2.) - DISTANCE_SEMICIRCLE_FROM_LEGS; }

QPen linePen(const QColor& color) { return QPen(color, LINE_WIDTH, Qt::SolidLine, Qt::FlatCap); }

/// Draws a text centered at a position and rotated around it
void showText(QPainter& p, const QPointF& pos, const QString& text, double angleDegrees) {
    // Laid out at a large size and scaled down: fonts have no sizes of fractions of a pixel
    constexpr double LAYOUT_SIZE = 100;
    QFont font(QStringLiteral("Arial"));
    font.setStyleHint(QFont::SansSerif);
    font.setPixelSize(static_cast<int>(LAYOUT_SIZE));
    const QRectF rect = QFontMetricsF(font).tightBoundingRect(text);

    p.save();
    p.translate(pos);
    p.rotate(angleDegrees);
    p.scale(FONT_SIZE / LAYOUT_SIZE, FONT_SIZE / LAYOUT_SIZE);
    p.setFont(font);
    p.drawText(QPointF(-rect.center().x(), -rect.center().y()), text);
    p.restore();
}

/// Position relative to the sides of the setsquare: x along the side, y the distance to it (negative inside)
enum class Leg { Hypotenuse, Left, Right };
QPointF relativeToSide(Leg leg, const QPointF& p, double height) {
    switch (leg) {
        case Leg::Left:
            return QPointF((p.y() + p.x()) / std::sqrt(2.), (p.y() - p.x() - height) / std::sqrt(2.));
        case Leg::Right:
            return QPointF((p.y() - p.x()) / std::sqrt(2.), (p.y() + p.x() - height) / std::sqrt(2.));
        default:
            return QPointF(p.x(), -p.y());
    }
}

}  // namespace

GeometryTool::GeometryTool(Type type, const QPointF& origin):
        m_type(type),
        m_height(type == Type::Setsquare ? SETSQUARE_INITIAL_HEIGHT : COMPASS_INITIAL_HEIGHT),
        m_origin(origin) {}

double GeometryTool::minHeight() const {
    return m_type == Type::Setsquare ? SETSQUARE_MIN_HEIGHT : COMPASS_MIN_HEIGHT;
}

double GeometryTool::maxHeight() const {
    return m_type == Type::Setsquare ? SETSQUARE_MAX_HEIGHT : COMPASS_MAX_HEIGHT;
}

QTransform GeometryTool::matrix() const {
    QTransform t;
    t.translate(m_origin.x(), m_origin.y());
    t.rotateRadians(m_rotation);
    t.scale(CM, CM);
    return t;
}

QPointF GeometryTool::toTool(const QPointF& pagePos) const { return matrix().inverted().map(pagePos); }

QRectF GeometryTool::bounds(bool withStroke) const {
    QList<QPointF> corners = withStroke ? m_points : QList<QPointF>();
    if (m_type == Type::Setsquare) {
        const QTransform m = matrix();
        corners << m.map(QPointF(m_height, 0)) << m.map(QPointF(-m_height, 0)) << m.map(QPointF(0, m_height));
    } else {
        const double h = m_height * CM;
        corners << m_origin - QPointF(h, h) << m_origin + QPointF(h, h);
    }
    QPointF min = corners.first();
    QPointF max = min;
    for (const QPointF& p: corners) {
        min = QPointF(std::min(min.x(), p.x()), std::min(min.y(), p.y()));
        max = QPointF(std::max(max.x(), p.x()), std::max(max.y(), p.y()));
    }
    // The outline and the last digits reach a little beyond the tool
    constexpr double PADDING = 3.0;
    return QRectF(min, max).adjusted(-PADDING, -PADDING, PADDING, PADDING);
}

bool GeometryTool::contains(const QPointF& pagePos, double border) const {
    const QPointF p = toTool(pagePos);
    if (m_type == Type::Compass) {
        return std::hypot(p.x(), p.y()) <= m_height + border;
    }
    return relativeToSide(Leg::Hypotenuse, p, m_height).y() < border &&
           relativeToSide(Leg::Left, p, m_height).y() < border && relativeToSide(Leg::Right, p, m_height).y() < border;
}

void GeometryTool::translate(const QPointF& offset) { m_origin += offset; }

void GeometryTool::rotate(double angle) { m_rotation += angle; }

void GeometryTool::rotate(double angle, const QPointF& center) {
    const QPointF offset = m_origin - center;
    m_origin = center + QPointF(offset.x() * std::cos(angle) - offset.y() * std::sin(angle),
                                offset.x() * std::sin(angle) + offset.y() * std::cos(angle));
    rotate(angle);
}

bool GeometryTool::scale(double factor) {
    const double height = m_height * factor;
    if (!(height >= minHeight() && height <= maxHeight())) {
        return false;
    }
    m_height = height;
    return true;
}

bool GeometryTool::scale(double factor, const QPointF& center) {
    if (!scale(factor)) {
        return false;
    }
    m_origin = center + (m_origin - center) * factor;
    return true;
}

void GeometryTool::translateWithSnapping(const QPointF& offset, const std::vector<QLineF>& lines) {
    const QPointF pos = m_origin + offset;
    double minDist = SNAPPING_DISTANCE_TOLERANCE;
    double diffAngle = NAN;
    for (const QLineF& line: lines) {
        const double dist = Snapping::distanceLine(pos, line.p1(), line.p2());
        const double angleLine = std::atan2(line.dy(), line.dx());
        const double diff = std::remainder(angleLine - m_rotation, M_PI_2);
        if (dist < minDist && std::abs(diff) <= SNAPPING_ROTATION_TOLERANCE) {
            minDist = dist;
            diffAngle = diff;
        }
    }
    if (!std::isnan(diffAngle)) {
        rotate(diffAngle, pos);
    }
    translate(offset);
}

bool GeometryTool::beginStroke(const QPointF& pagePos, bool filled) {
    if (!contains(pagePos)) {
        return false;
    }
    const QPointF p = toTool(pagePos);
    m_filled = filled;

    if (m_type == Type::Setsquare) {
        const QPointF hypotenuse = relativeToSide(Leg::Hypotenuse, p, m_height);
        if (hypotenuse.y() >= -GUIDE_WIDTH) {
            m_stroke = StrokeKind::Edge;
            m_min = m_max = hypotenuse.x();
        } else if (relativeToSide(Leg::Left, p, m_height).y() >= -GUIDE_WIDTH ||
                   relativeToSide(Leg::Right, p, m_height).y() >= -GUIDE_WIDTH) {
            m_stroke = StrokeKind::Radial;
            m_strokeAngle = std::atan2(hypotenuse.y(), hypotenuse.x());
        } else {
            return false;
        }
    } else {
        if (!contains(pagePos, -GUIDE_WIDTH)) {
            m_stroke = StrokeKind::Outline;
            m_lastAngle = std::atan2(p.y(), p.x());
            m_min = m_max = m_lastAngle;
        } else if (std::abs(p.y()) <= GUIDE_WIDTH && std::abs(p.x() - 0.5 * m_height) <= 0.5 * m_height) {
            m_stroke = StrokeKind::Radial;
            m_min = m_max = std::hypot(p.x(), p.y());
        } else {
            return false;
        }
    }
    updateStroke(pagePos);
    return true;
}

void GeometryTool::updateStroke(const QPointF& pagePos) {
    const QPointF p = toTool(pagePos);
    const QTransform m = matrix();
    m_points.clear();

    if (m_type == Type::Setsquare) {
        if (m_stroke == StrokeKind::Edge) {
            m_min = std::min(m_min, p.x());
            m_max = std::max(m_max, p.x());
            m_points = {m.map(QPointF(m_min, 0)), m.map(QPointF(m_max, 0))};
        } else if (m_stroke == StrokeKind::Radial) {
            const QPointF rel = relativeToSide(Leg::Hypotenuse, p, m_height);
            const double radius = std::hypot(rel.x(), rel.y());
            m_points.append(m_origin);
            if (radius >= setsquareRadius(m_height) || rel.y() > 0) {
                // Outside of the semicircle the direction follows the pen
                m_strokeAngle = std::atan2(rel.y(), rel.x());
                m_points.append(pagePos);
            } else {
                m_points.append(m.map(QPointF(radius * std::cos(m_strokeAngle), -radius * std::sin(m_strokeAngle))));
            }
        }
    } else {
        if (m_stroke == StrokeKind::Outline) {
            // The angle keeps counting beyond a full turn
            double angle = std::atan2(p.y(), p.x());
            angle = m_lastAngle + std::remainder(angle - m_lastAngle, 2 * M_PI);
            m_lastAngle = angle;
            m_min = std::min(m_min, angle);
            m_max = std::max(m_max, angle);

            const bool sector = m_filled && m_max < m_min + 2 * M_PI;
            if (sector) {
                m_points.append(m_origin);
            }
            for (int i = 0; i <= 100; ++i) {
                const double a = m_min + i / 100.0 * std::min(m_max - m_min, 2 * M_PI);
                m_points.append(m.map(QPointF(m_height * std::cos(a), m_height * std::sin(a))));
            }
            if (sector) {
                m_points.append(m_origin);
            }
        } else if (m_stroke == StrokeKind::Radial) {
            const double radius = std::hypot(p.x(), p.y());
            m_min = std::min(m_min, radius);
            m_max = std::max(m_max, radius);
            m_points = {m.map(QPointF(m_min, 0)), m.map(QPointF(m_max, 0))};
        }
    }
}

void GeometryTool::endStroke() {
    m_stroke = StrokeKind::None;
    m_points.clear();
}

QList<QPointF> GeometryTool::originMark() const {
    const double x = m_origin.x();
    const double y = m_origin.y();
    return {QPointF(x + MARK_SIZE, y + MARK_SIZE), QPointF(x - MARK_SIZE, y - MARK_SIZE), QPointF(x, y),
            QPointF(x + MARK_SIZE, y - MARK_SIZE), QPointF(x - MARK_SIZE, y + MARK_SIZE)};
}

void GeometryTool::paint(QPainter& p) const {
    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setTransform(matrix(), true);
    p.setBrush(Qt::NoBrush);
    if (m_type == Type::Setsquare) {
        paintSetsquare(p);
    } else {
        paintCompass(p);
    }
    p.restore();
}

void GeometryTool::paintSetsquare(QPainter& p) const {
    constexpr double MAX_HOR_POS_VMARKS = 2.5;
    constexpr int MIN_OFFSET_ANG_MARKS = 2;
    constexpr double RELATIVE_CIRCLE_POS = .75;
    constexpr double RELATIVE_MAX_HOR_POS_VMARKS = .6;
    constexpr double HMARK_POS = 2.;
    constexpr double MIN_DIST_FROM_HMARK = .2;
    constexpr int MIN_VMARK_SMALL = 3;
    constexpr int MIN_VMARK_LARGE = 5;
    constexpr int OFFSET_FROM_SEMICIRCLE = 2;
    constexpr double ZERO_MARK_TICK = .5;
    constexpr int SKIPPED_HMARKS = 8;

    const double height = m_height;
    const double radius = setsquareRadius(height);
    const double circlePos = height * RELATIVE_CIRCLE_POS;
    const double horPosVmarks = std::min(MAX_HOR_POS_VMARKS, radius * RELATIVE_MAX_HOR_POS_VMARKS);
    const bool nearHmark = std::abs(horPosVmarks - HMARK_POS - TICK_SMALL / 2.) < MIN_DIST_FROM_HMARK - TICK_SMALL / 2.;
    const int minVmark = nearHmark ? MIN_VMARK_LARGE : MIN_VMARK_SMALL;
    const int maxVmark = static_cast<int>(std::floor(cathete(radius, horPosVmarks) * 10.0)) - OFFSET_FROM_SEMICIRCLE;
    // The offset of the angular marks is based on experimentation: larger for small heights, and when the
    // semicircle comes close to the big marks of the hypotenuse
    int offset = std::max(MIN_OFFSET_ANG_MARKS, static_cast<int>(256.0 / std::pow(height, 2)));
    if (std::abs(radius - std::round(radius)) < .2) {
        offset = std::max(offset, static_cast<int>(24.0 / radius));
    }
    const int maxHmark = static_cast<int>(std::floor(height * 10.0)) - SKIPPED_HMARKS;
    const int fullCentimetres = maxVmark / 10;

    // The areas where numbers are written stay free of lines
    const QRectF whole(-height, 0, 2 * height, height);
    QPainterPath outsideVerticalStripes;
    outsideVerticalStripes.setFillRule(Qt::OddEvenFill);
    const double stripeHeight = maxVmark / 10. + .5;
    outsideVerticalStripes.addRect(QRectF(-horPosVmarks - .25, 0, .75, stripeHeight));
    outsideVerticalStripes.addRect(QRectF(horPosVmarks - .5, 0, .75, stripeHeight));
    outsideVerticalStripes.addRect(QRectF(-.25, 0, .5, stripeHeight));
    outsideVerticalStripes.addRect(whole);
    QPainterPath outsideHorizontalStripes;
    outsideHorizontalStripes.setFillRule(Qt::OddEvenFill);
    for (double i = .5; i <= maxVmark / 10.0; i += .5) {
        const double x = cathete(radius - .25, i);
        outsideHorizontalStripes.addRect(QRectF(-x, i - .15, 2. * x, .3));
    }
    outsideHorizontalStripes.addRect(whole);

    // Outline
    QPainterPath triangle;
    triangle.moveTo(height, 0);
    triangle.lineTo(-height, 0);
    triangle.lineTo(0, height);
    triangle.closeSubpath();
    p.fillPath(triangle, FILL);
    p.setPen(linePen(RED));
    p.drawPath(triangle);

    // The scale along the hypotenuse, and the marks of its middle
    p.setPen(linePen(BLUE));
    for (int i = 1; i <= fullCentimetres; ++i) {
        p.drawLine(QPointF(0, i - ZERO_MARK_TICK / 2.), QPointF(0, i + ZERO_MARK_TICK / 2.));
    }
    for (int i = -maxHmark; i <= maxHmark; ++i) {
        const double tick = (i % 5 == 0) ? TICK_LARGE : TICK_SMALL;
        p.drawLine(QPointF(i / 10., 0), QPointF(i / 10., tick));
        if (i % 10 == 0) {
            showText(p, QPointF(i / 10., tick + FONT_SIZE / 2.), QString::number(std::abs(i / 10)), 0);
        }
    }

    // Parallels to the hypotenuse within the semicircle, and the scales perpendicular to it
    p.setPen(linePen(GREEN));
    p.save();
    p.setClipPath(outsideVerticalStripes, Qt::IntersectClip);
    for (double i = .5; i <= fullCentimetres; i += .5) {
        const double x = cathete(radius - .25, i);
        p.drawLine(QPointF(-x, i), QPointF(x, i));
    }
    p.restore();
    for (int i = minVmark; i <= maxVmark; ++i) {
        const double y = i / 10.;
        const double tick = (i % 5 == 0) ? TICK_LARGE : TICK_SMALL;
        for (const double sign: {-1., 1.}) {
            p.drawLine(QPointF(sign * horPosVmarks, y), QPointF(sign * (horPosVmarks - tick), y));
            if (i % 10 == 0) {
                showText(p, QPointF(sign * (horPosVmarks - tick - TICK_SMALL), y), QString::number(i / 10), 0);
            }
        }
    }

    // Angles
    p.setPen(linePen(VIOLET));
    p.save();
    p.setClipPath(outsideHorizontalStripes, Qt::IntersectClip);
    p.setClipPath(outsideVerticalStripes, Qt::IntersectClip);
    for (int i: {45, 135}) {
        const QPointF direction(std::cos(rad(i)), std::sin(rad(i)));
        p.drawLine(direction, (radius - .3) * direction);
        p.drawLine((radius + .3) * direction, (height / std::sqrt(2.) - .3) * direction);
    }
    p.restore();
    for (int i = 1; i < 180; ++i) {
        // The distance from the middle of the hypotenuse to the leg at this angle, by the law of sines
        const double radCath = height * std::sin(rad(45)) / std::sin(rad(i > 90 ? i - 45 : 135 - i));
        const QPointF direction(std::cos(rad(i)), std::sin(rad(i)));
        const double tick = (i % 5 == 0) ? TICK_LARGE : TICK_SMALL;
        if (i % 10 == 0 && i > offset && i < 180 - offset) {
            // A long tick from the leg, with the angle counted from both sides
            const double radTickEnd = (i == 90) ? (circlePos + .5) : (radius + .8);
            p.drawLine(radCath * direction, radTickEnd * direction);
            showText(p, (radius + 0.3) * direction, QString::number(i), i + 270);
            if (i != 90) {
                showText(p, (radius + 0.6) * direction, QString::number(180 - i), i + 270);
            }
        } else {
            p.drawLine(radCath * direction, (radCath - tick) * direction);
        }
        if (i > offset && i < 180 - offset) {
            p.drawLine(radius * direction, (radius + tick) * direction);
        }
    }

    // The angle between the hypotenuse and the horizontal, written upright in a small circle
    p.setPen(linePen(TURQUOISE));
    const double angle = std::abs(std::remainder(deg(m_rotation), 180.));
    showText(p, QPointF(0, circlePos), QString::number(angle, 'f', 1), -deg(m_rotation));
    p.drawEllipse(QPointF(0, circlePos), CIRCLE_RAD, CIRCLE_RAD);
}

void GeometryTool::paintCompass(QPainter& p) const {
    constexpr double RELATIVE_CIRCLE_POS = .8;
    constexpr double RELATIVE_ANGULAR_CAPTION_POS = .3;
    constexpr double OFFSET_CIRCLE_POS = .4;

    const double height = m_height;
    const double circlePos = height * RELATIVE_CIRCLE_POS - OFFSET_CIRCLE_POS;
    const double angularCaptionPos = height * RELATIVE_ANGULAR_CAPTION_POS;
    const int maxHmark = static_cast<int>(std::round(height * 10.0));
    const bool drawRotationDisplay = height >= 2.;
    const bool drawRadialCaption = height >= 1.5;
    // Fewer marks on small tools
    const int angularOffset = (height >= 2.) ? 1 : (height >= 1.2) ? 2 : (height >= 0.8) ? 5 : 10;
    const int angularCaptionOffset = (height >= 3.) ? 30 : (height >= 1.5) ? 45 : (height >= 1.) ? 90 : 360;

    // Outline, with the radius that marks the angle 0
    p.setPen(linePen(RED));
    p.setBrush(FILL);
    p.drawEllipse(QPointF(0, 0), height, height);
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(0, 0), QPointF(height, 0));

    // The scale along the radius
    p.setPen(linePen(BLUE));
    for (int i = 0; i <= maxHmark; ++i) {
        const double tick = (i % 5 == 0) ? TICK_LARGE : TICK_SMALL;
        p.drawLine(QPointF(i / 10.0, 0), QPointF(i / 10.0, tick));
        if (i % 10 == 0 && drawRadialCaption) {
            showText(p, QPointF(i / 10.0, tick + FONT_SIZE / 2.), QString::number(i / 10), 0);
        }
    }

    // Angles
    p.setPen(linePen(VIOLET));
    for (int i = angularOffset; i < 360; i += angularOffset) {
        const QPointF direction(std::cos(rad(i)), std::sin(rad(i)));
        const double tick = (i % 5 == 0) ? TICK_LARGE : TICK_SMALL;
        if (i % angularCaptionOffset == 0) {
            const double radTickEnd = (i == 270) ? (circlePos + 1.5 * CIRCLE_RAD) : (angularCaptionPos + 0.3);
            p.drawLine(height * direction, radTickEnd * direction);
            showText(p, angularCaptionPos * direction, QString::number(360 - i), i + 90);
        } else {
            p.drawLine(height * direction, (height - tick) * direction);
        }
    }

    // The rotation, written upright in a small circle
    if (drawRotationDisplay) {
        p.setPen(linePen(TURQUOISE));
        const double angle = std::abs(std::remainder(deg(m_rotation), 180.));
        showText(p, QPointF(0, -circlePos), QString::number(angle, 'f', 1), -deg(m_rotation));
        p.drawEllipse(QPointF(0, -circlePos), CIRCLE_RAD, CIRCLE_RAD);
    }
}
