/*
 * Qournal
 *
 * @license GNU GPLv2 or later
 */

#include "XoppCompat.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>
#include <optional>

#include "Renderer.h"

namespace {

/// Resolution of what is turned into an image, in pixels per point (300 dpi), and its largest side
constexpr double RASTER_SCALE = 300.0 / 72.0;
constexpr int RASTER_MAX_SIDE = 6000;

enum Change { LinkToText, TextToImage, ImageTurned, FormulaToImage, CHANGE_COUNT };

QString describe(Change change) {
    switch (change) {
        case LinkToText:
            return QCoreApplication::translate("XoppCompat", "Links become texts without their address.");
        case TextToImage:
            return QCoreApplication::translate("XoppCompat",
                                               "Turned texts become images and cannot be edited as texts any more.");
        case ImageTurned:
            return QCoreApplication::translate("XoppCompat", "Turned images are drawn turned into new images.");
        case FormulaToImage:
            return QCoreApplication::translate("XoppCompat",
                                               "Turned formulas become images and cannot be edited any more.");
        default:
            return {};
    }
}

/// The uniform scale of a matrix that only moves and scales, 0 for other matrices
double uniformScale(const Matrix& m) {
    const bool axisAligned = std::abs(m[1]) < 1e-9 && std::abs(m[2]) < 1e-9;
    return axisAligned && m[0] > 0 && std::abs(m[0] - m[3]) < 1e-9 ? m[0] : 0;
}

/// An image of how an element looks, in its bounds on the page
ImageElement rasterized(const Element& element) {
    const QRectF bounds = Renderer::elementBounds(element);
    const double scale = std::min(RASTER_SCALE, RASTER_MAX_SIDE / std::max({bounds.width(), bounds.height(), 1.0}));
    QImage image(std::max(1, static_cast<int>(std::ceil(bounds.width() * scale))),
                 std::max(1, static_cast<int>(std::ceil(bounds.height() * scale))),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.scale(scale, scale);
        p.translate(-bounds.topLeft());
        Page page;
        page.width = bounds.right();
        page.height = bounds.bottom();
        Layer layer;
        layer.elements.push_back(element);
        page.layers.push_back(std::move(layer));
        Renderer::renderLayers(p, page, bounds, true);
    }
    ImageElement result;
    result.image = image;
    QBuffer buffer(&result.data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    result.rect = bounds;
    return result;
}

/// What format 4 makes of an element, if it is not what it is now
std::optional<Change> changeOf(const Element& element) {
    if (const auto* link = std::get_if<LinkElement>(&element)) {
        return uniformScale(link->matrix) > 0 ? LinkToText : TextToImage;
    }
    if (const auto* text = std::get_if<TextElement>(&element); text && text->matrix) {
        return uniformScale(*text->matrix) > 0 ? std::nullopt : std::optional(TextToImage);
    }
    if (const auto* image = std::get_if<ImageElement>(&element); image && image->matrix) {
        const Matrix& m = *image->matrix;
        const bool axisAligned = std::abs(m[1]) < 1e-9 && std::abs(m[2]) < 1e-9 && m[0] > 0 && m[3] > 0;
        if (!image->naturalSize.isValid() || axisAligned) {
            return std::nullopt;
        }
        return image->tex ? FormulaToImage : ImageTurned;
    }
    return std::nullopt;
}

}  // namespace

bool XoppCompat::needsFormat5(const Document& doc) {
    for (const Page& page: doc.pages) {
        for (const Layer& layer: page.layers) {
            for (const Element& element: layer.elements) {
                if (std::holds_alternative<LinkElement>(element)) {
                    return true;
                }
                if (const auto* text = std::get_if<TextElement>(&element); text && text->matrix) {
                    return true;
                }
                if (const auto* image = std::get_if<ImageElement>(&element); image && image->matrix) {
                    return true;
                }
            }
        }
    }
    return false;
}

QStringList XoppCompat::format4Changes(const Document& doc) {
    bool made[CHANGE_COUNT] = {};
    for (const Page& page: doc.pages) {
        for (const Layer& layer: page.layers) {
            for (const Element& element: layer.elements) {
                if (const std::optional<Change> change = changeOf(element)) {
                    made[*change] = true;
                }
                // Also a turned link, which becomes an image, loses its address
                made[LinkToText] |= std::holds_alternative<LinkElement>(element);
            }
        }
    }
    QStringList changes;
    for (int i = 0; i < CHANGE_COUNT; ++i) {
        if (made[i]) {
            changes.append(describe(static_cast<Change>(i)));
        }
    }
    return changes;
}

Document XoppCompat::toFormat4(const Document& doc) {
    Document result = doc;
    for (Page& page: result.pages) {
        for (Layer& layer: page.layers) {
            for (Element& element: layer.elements) {
                const std::optional<Change> change = changeOf(element);
                if (change && *change != LinkToText) {
                    element = rasterized(element);
                } else if (const auto* link = std::get_if<LinkElement>(&element)) {
                    // Only moved or scaled: a text of the same look
                    const double scale = uniformScale(link->matrix);
                    TextElement text;
                    text.text = link->text;
                    text.font = link->font;
                    text.size = link->size * scale;
                    text.pos = QPointF(link->matrix[4], link->matrix[5]);
                    text.color = link->color;
                    text.align = link->align;
                    element = text;
                } else if (auto* text = std::get_if<TextElement>(&element); text && text->matrix) {
                    const double scale = uniformScale(*text->matrix);
                    text->pos = QPointF((*text->matrix)[4], (*text->matrix)[5]);
                    text->size *= scale;
                    if (text->wrap >= 0) {
                        text->wrap *= scale;
                    }
                    text->matrix.reset();
                } else if (auto* image = std::get_if<ImageElement>(&element); image && image->matrix) {
                    const Matrix& m = *image->matrix;
                    if (image->naturalSize.isValid()) {
                        image->rect = QRectF(m[4], m[5], m[0] * image->naturalSize.width(),
                                             m[3] * image->naturalSize.height());
                    }
                    image->matrix.reset();
                }
            }
        }
    }
    return result;
}
