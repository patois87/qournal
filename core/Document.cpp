#include "Document.h"

#include <algorithm>

void Stroke::setStyle(const QString& newStyle) {
    style = newStyle;
    dashes.clear();
    if (style == u"dash") {
        dashes = {6, 3};
    } else if (style == u"dashdot") {
        dashes = {6, 3, 0.5, 3};
    } else if (style == u"dot") {
        dashes = {0.5, 3};
    } else if (style.startsWith(u"cust: ")) {
        const auto parts = QStringView(style).mid(6).split(u' ', Qt::SkipEmptyParts);
        for (const QStringView part: parts) {
            dashes.append(part.toDouble());
        }
        // Qt needs an even number of entries
        if (dashes.size() % 2 != 0) {
            dashes.append(dashes);
        }
    } else {
        style.clear();  // "plain" or unknown
    }
}

void Stroke::updateBounds() {
    if (points.isEmpty()) {
        bounds = QRectF();
        return;
    }
    double minX = points.first().x(), maxX = minX;
    double minY = points.first().y(), maxY = minY;
    for (const QPointF& p: points) {
        minX = std::min(minX, p.x());
        maxX = std::max(maxX, p.x());
        minY = std::min(minY, p.y());
        maxY = std::max(maxY, p.y());
    }
    double maxWidth = width;
    for (double w: widths) {
        maxWidth = std::max(maxWidth, w);
    }
    const double pad = maxWidth / 2 + 1;
    bounds = QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-pad, -pad, pad, pad);
}

void translateElement(Element& element, const QPointF& delta) {
    auto translateMatrix = [&delta](Matrix& matrix) {
        matrix[4] += delta.x();
        matrix[5] += delta.y();
    };
    if (auto* stroke = std::get_if<Stroke>(&element)) {
        for (QPointF& point: stroke->points) {
            point += delta;
        }
        stroke->bounds.translate(delta);
    } else if (auto* text = std::get_if<TextElement>(&element)) {
        text->pos += delta;
        if (text->matrix) {
            translateMatrix(*text->matrix);
        }
    } else if (auto* image = std::get_if<ImageElement>(&element)) {
        image->rect.translate(delta);
        if (image->matrix) {
            translateMatrix(*image->matrix);
        }
    } else if (auto* link = std::get_if<LinkElement>(&element)) {
        translateMatrix(link->matrix);
    }
}

Document Document::createEmpty() {
    Document doc;
    Page page;
    page.layers.emplace_back();
    doc.pages.push_back(std::move(page));
    return doc;
}
