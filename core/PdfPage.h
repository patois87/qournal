/*
 * Qournal
 *
 * Draws the pages of a PDF file with QPainter, as vectors: for printing and for the SVG export, where Qt PDF only
 * gives images. Xournal++ does this with Poppler and Cairo.
 *
 * Understood are paths, colours (gray, RGB, CMYK, Separation, DeviceN and what is based on them), axial and radial
 * gradients, tiling patterns, clipping, transparency, images (JPEG and uncompressed or Flate, with soft masks), form
 * objects, annotations with appearances, and text in embedded TrueType, Type 1 and CFF fonts (also CID-keyed ones),
 * in Type 3 fonts or in the standard fonts. What a soft mask of the graphics state covers is drawn as an image, as
 * QPainter has no such masks. A page with anything else (mesh gradients, blend modes, JBIG2, CCITT and JPEG 2000
 * images, ...) is not drawn: the caller draws it as an image then.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QHash>
#include <QPainter>
#include <QSet>
#include <QSizeF>
#include <QString>
#include <memory>

#include "PdfFile.h"

namespace Pdf {

class PageDrawer {
public:
    explicit PageDrawer(const Reader& reader);
    ~PageDrawer();

    /**
     * Draws a page into the rectangle (0, 0, size) of the painter, as a viewer shows it: its crop box, turned by
     * its rotation.
     * @return false, having drawn nothing, if the page has something that cannot be drawn here; why() tells what
     */
    bool draw(QPainter& painter, const PageInfo& page, const QSizeF& size);

    QString why() const { return m_why; }

    struct Font;
    struct Image;

private:
    const Reader& m_reader;
    QString m_why;
    /// Fonts and images by the number of their object: the same ones are on many pages
    QHash<int, std::shared_ptr<Font>> m_fonts;
    QHash<int, std::shared_ptr<Image>> m_images;
    /// The groups of optional content that are hidden: their content is not drawn, as in viewers
    QSet<int> m_hiddenGroups;

public:
    /// Whether an entry /OC (a group, or a membership dictionary) hides what it belongs to
    bool hides(const Value& optionalContent) const;

    friend class Interpreter;
};

}  // namespace Pdf
