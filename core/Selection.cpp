#include "Selection.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Renderer.h"

namespace {

bool isAxisAligned(const QTransform& t) {
    constexpr double EPSILON = 1e-9;
    return std::abs(t.m12()) < EPSILON && std::abs(t.m21()) < EPSILON && t.m11() > 0 && t.m22() > 0;
}

Matrix toMatrix(const QTransform& t) { return Matrix{t.m11(), t.m12(), t.m21(), t.m22(), t.dx(), t.dy()}; }

double distanceToSegment(const QPointF& p, const QPointF& a, const QPointF& b) {
    const QPointF ab = b - a;
    const double len2 = QPointF::dotProduct(ab, ab);
    if (len2 <= 0) {
        return std::hypot(p.x() - a.x(), p.y() - a.y());
    }
    const double t = std::clamp(QPointF::dotProduct(p - a, ab) / len2, 0.0, 1.0);
    const QPointF proj = a + t * ab;
    return std::hypot(p.x() - proj.x(), p.y() - proj.y());
}

bool insidePolygon(const QList<QPointF>& polygon, const QPointF& p) {
    bool inside = false;
    for (qsizetype i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const QPointF& a = polygon[i];
        const QPointF& b = polygon[j];
        if ((a.y() > p.y()) != (b.y() > p.y()) && p.x() < (b.x() - a.x()) * (p.y() - a.y()) / (b.y() - a.y()) + a.x()) {
            inside = !inside;
        }
    }
    return inside;
}

/// The transformation of an image from its natural size to the page
QTransform imageTransform(const ImageElement& image, const QSizeF& natural) {
    if (image.matrix) {
        return toTransform(*image.matrix);
    }
    QTransform t;
    t.translate(image.rect.left(), image.rect.top());
    t.scale(image.rect.width() / natural.width(), image.rect.height() / natural.height());
    return t;
}

QSizeF naturalSizeOf(const ImageElement& image) {
    if (image.naturalSize.isValid() && !image.naturalSize.isEmpty()) {
        return image.naturalSize;
    }
    return image.rect.isEmpty() ? QSizeF(1, 1) : image.rect.size();
}

}  // namespace

SelectionArea SelectionArea::rectangle(const QPointF& start, const QPointF& end) {
    SelectionArea area;
    area.m_rectangle = true;
    area.m_boundary = {start, QPointF(start.x(), end.y()), end, QPointF(end.x(), start.y())};
    area.updateBounds();
    return area;
}

SelectionArea SelectionArea::lasso(const QList<QPointF>& points) {
    SelectionArea area;
    area.m_rectangle = false;
    area.m_boundary = points;
    area.updateBounds();
    return area;
}

void SelectionArea::updateBounds() {
    if (m_boundary.isEmpty()) {
        m_min = m_max = QPointF();
        return;
    }
    m_min = m_max = m_boundary.first();
    for (const QPointF& p: m_boundary) {
        m_min = QPointF(std::min(m_min.x(), p.x()), std::min(m_min.y(), p.y()));
        m_max = QPointF(std::max(m_max.x(), p.x()), std::max(m_max.y(), p.y()));
    }
}

bool SelectionArea::isTap(double maxDistance) const {
    return m_max.x() - m_min.x() < maxDistance && m_max.y() - m_min.y() < maxDistance;
}

bool SelectionArea::contains(const QPointF& p) const {
    if (p.x() < m_min.x() || p.x() > m_max.x() || p.y() < m_min.y() || p.y() > m_max.y()) {
        return false;
    }
    if (m_rectangle) {
        return true;
    }
    if (m_boundary.size() <= 2) {
        return false;
    }

    // The winding number, counted with the crossings of a ray to the left
    int hits = 0;
    auto hitSign = [](const QPointF& last, const QPointF& current) {
        return current.y() > last.y() ? 1 : current.y() < last.y() ? -1 : current.x() < last.x() ? 1 : -1;
    };
    const QPointF* last = &m_boundary.last();
    for (const QPointF& current: m_boundary) {
        const QPointF& previous = *last;
        last = &current;
        if (p.y() < std::min(current.y(), previous.y()) || p.y() > std::max(current.y(), previous.y())) {
            continue;
        }
        if (p.x() < std::min(current.x(), previous.x())) {
            hits += hitSign(previous, current);
            continue;
        }
        if (p.x() > std::max(current.x(), previous.x())) {
            continue;
        }
        if (current.y() == previous.y()) {
            hits += hitSign(previous, current);
            continue;
        }
        const double projX =
                p.x() + (current.x() - previous.x()) * (current.y() - p.y()) / (current.y() - previous.y());
        if (projX <= current.x()) {
            hits += hitSign(previous, current);
        }
    }
    return hits != 0;
}

void SelectionArea::extendAtPageEdges(double pageWidth, double pageHeight) {
    if (pageWidth <= 0 || pageHeight <= 0) {
        return;
    }
    // Far outside of any page, but small enough to compute with
    constexpr double FAR = 1e9;
    auto extend = [](double value, double extent) {
        if (value <= EDGE_TOUCHING_THRESHOLD) {
            return -FAR;
        }
        if (value >= extent - EDGE_TOUCHING_THRESHOLD) {
            return FAR;
        }
        return value;
    };

    if (m_rectangle) {
        m_min = QPointF(m_min.x() <= EDGE_TOUCHING_THRESHOLD ? -FAR : m_min.x(),
                        m_min.y() <= EDGE_TOUCHING_THRESHOLD ? -FAR : m_min.y());
        m_max = QPointF(m_max.x() >= pageWidth - EDGE_TOUCHING_THRESHOLD ? FAR : m_max.x(),
                        m_max.y() >= pageHeight - EDGE_TOUCHING_THRESHOLD ? FAR : m_max.y());
        return;
    }
    if (m_boundary.size() <= 2) {
        return;
    }

    auto isOnEdge = [&](const QPointF& p) {
        return p.x() <= EDGE_TOUCHING_THRESHOLD || p.x() >= pageWidth - EDGE_TOUCHING_THRESHOLD ||
               p.y() <= EDGE_TOUCHING_THRESHOLD || p.y() >= pageHeight - EDGE_TOUCHING_THRESHOLD;
    };
    auto project = [&](const QPointF& p) { return QPointF(extend(p.x(), pageWidth), extend(p.y(), pageHeight)); };

    // Where the lasso runs along the border, it runs far outside instead
    QList<QPointF> extended;
    const qsizetype n = m_boundary.size();
    for (qsizetype i = 0; i < n; ++i) {
        const QPointF& current = m_boundary[i];
        if (!isOnEdge(current)) {
            extended.append(current);
            continue;
        }
        const bool prevOnEdge = isOnEdge(m_boundary[(i + n - 1) % n]);
        const bool nextOnEdge = isOnEdge(m_boundary[(i + 1) % n]);
        if (!prevOnEdge) {
            extended.append(current);
            extended.append(project(current));
        } else if (!nextOnEdge) {
            extended.append(project(current));
            extended.append(current);
        } else {
            extended.append(project(current));
        }
    }
    m_boundary = extended;
    updateBounds();
}

QList<QPointF> Selection::elementCorners(const Element& element) {
    if (const auto* image = std::get_if<ImageElement>(&element)) {
        if (image->matrix && image->naturalSize.isValid()) {
            const QTransform t = toTransform(*image->matrix);
            const QSizeF& size = image->naturalSize;
            return {t.map(QPointF(0, 0)), t.map(QPointF(size.width(), 0)), t.map(QPointF(size.width(), size.height())),
                    t.map(QPointF(0, size.height()))};
        }
    }
    // The bounds of rotated texts are larger than the text: good enough
    const QRectF rect = Renderer::elementBounds(element);
    return {rect.topLeft(), rect.topRight(), rect.bottomRight(), rect.bottomLeft()};
}

bool Selection::isInArea(const Element& element, const SelectionArea& area) {
    const auto* stroke = std::get_if<Stroke>(&element);
    const QList<QPointF> points = stroke ? stroke->points : elementCorners(element);
    if (points.isEmpty()) {
        return false;
    }
    return std::all_of(points.begin(), points.end(), [&](const QPointF& p) { return area.contains(p); });
}

double Selection::distanceTo(const Element& element, const QPointF& pos) {
    if (const auto* stroke = std::get_if<Stroke>(&element)) {
        const auto& points = stroke->points;
        if (points.isEmpty()) {
            return std::numeric_limits<double>::max();
        }
        if (stroke->fill >= 0 && points.size() > 2 && insidePolygon(points, pos)) {
            return 0;
        }
        double distance = std::numeric_limits<double>::max();
        const bool pressure = stroke->hasPressure();
        for (qsizetype i = 0; i + 1 < points.size(); ++i) {
            const double width = pressure ? stroke->widths[i] : stroke->width;
            distance = std::min(distance, std::max(distanceToSegment(pos, points[i], points[i + 1]) - width / 2, 0.0));
        }
        if (points.size() == 1) {
            distance = std::max(std::hypot(pos.x() - points[0].x(), pos.y() - points[0].y()) - stroke->width / 2, 0.0);
        }
        return distance;
    }

    const QList<QPointF> corners = elementCorners(element);
    if (insidePolygon(corners, pos)) {
        return 0;
    }
    double distance = std::numeric_limits<double>::max();
    for (qsizetype i = 0; i < corners.size(); ++i) {
        distance = std::min(distance, distanceToSegment(pos, corners[i], corners[(i + 1) % corners.size()]));
    }
    return distance;
}

std::optional<size_t> Selection::elementAt(const Layer& layer, const QPointF& pos) {
    std::optional<size_t> match;
    double minDistance = ACTION_RADIUS;
    // From the front-most element on
    for (size_t i = layer.elements.size(); i-- > 0;) {
        const Element& element = layer.elements[i];
        // A rough check first: the distance to a stroke is expensive to compute
        const QRectF bounds = Renderer::elementBounds(element);
        if (!bounds.adjusted(-minDistance, -minDistance, minDistance, minDistance).contains(pos)) {
            continue;
        }
        const double distance = distanceTo(element, pos);
        if (distance == 0.0) {
            return i;
        }
        if (distance < minDistance) {
            // Keep going: another one may be closer
            match = i;
            minDistance = distance;
        }
    }
    return match;
}

void Selection::transform(Element& element, const QTransform& transformation, double widthFactor) {
    if (auto* stroke = std::get_if<Stroke>(&element)) {
        for (QPointF& p: stroke->points) {
            p = transformation.map(p);
        }
        stroke->width *= widthFactor;
        for (double& width: stroke->widths) {
            width *= widthFactor;
        }
        stroke->updateBounds();
        return;
    }

    if (auto* text = std::get_if<TextElement>(&element)) {
        const QTransform before =
                text->matrix ? toTransform(*text->matrix) : QTransform::fromTranslate(text->pos.x(), text->pos.y());
        const QTransform after = before * transformation;
        text->pos = QPointF(after.dx(), after.dy());
        const bool uniform = isAxisAligned(after) && std::abs(after.m11() - after.m22()) < 1e-9;
        if (uniform) {
            // Moved or scaled evenly: a text of another size, which older versions of the file format can store
            text->size *= after.m11();
            if (text->wrap >= 0) {
                text->wrap *= after.m11();
            }
            text->matrix.reset();
        } else {
            text->matrix = toMatrix(after);
        }
        return;
    }

    if (auto* image = std::get_if<ImageElement>(&element)) {
        const QSizeF natural = naturalSizeOf(*image);
        const QTransform after = imageTransform(*image, natural) * transformation;
        image->rect = after.mapRect(QRectF(QPointF(0, 0), natural));
        if (isAxisAligned(after) && !image->matrix) {
            // Still an upright rectangle: stays readable for older versions of the file format
            return;
        }
        image->naturalSize = natural;
        image->matrix = toMatrix(after);
        return;
    }

    auto& link = std::get<LinkElement>(element);
    link.matrix = toMatrix(toTransform(link.matrix) * transformation);
}

std::vector<size_t> Selection::arrange(const std::vector<size_t>& indices, size_t count, OrderChange change) {
    const size_t n = indices.size();
    std::vector<size_t> result(n);
    if (n == 0) {
        return result;
    }
    size_t first = 0;
    switch (change) {
        case OrderChange::BringToFront:
            first = count - n;
            break;
        case OrderChange::BringForward:
            // Together, one place above the element that is in front of the highest of them
            first = std::min(indices.back() + 2, count) - n;
            break;
        case OrderChange::SendBackward:
            first = indices.front() > 0 ? indices.front() - 1 : 0;
            break;
        case OrderChange::SendToBack:
            first = 0;
            break;
    }
    for (size_t i = 0; i < n; ++i) {
        result[i] = first + i;
    }
    return result;
}
