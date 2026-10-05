/*
 * Qournal
 *
 * QPainter based rendering of the document model
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QImage>
#include <QPainter>
#include <QRectF>

#include "Document.h"

namespace Renderer {

void renderStroke(QPainter& p, const Stroke& stroke);

/**
 * While one of these exists, renderStroke() in its thread draws the fills of strokes as a regular pattern of
 * dots of the full colour instead of a half transparent colour: for electronic paper, which shows tones between
 * its inks with noise, so that a filled shape looks uneven there. Only for the screen, not for exports
 */
class PatternFills {
public:
    explicit PatternFills(bool enabled);
    ~PatternFills();
    PatternFills(const PatternFills&) = delete;
    PatternFills& operator=(const PatternFills&) = delete;

private:
    bool m_before;
};

/// The area an element covers on its page. Texts are measured with the fonts of this system
QRectF elementBounds(const Element& element);

/**
 * While one of these exists, renderBackground() in its thread draws the lines of a ruling in black (white on dark
 * paper) and at least one pixel of the device wide: for electronic paper, where the thin, light lines had gaps
 * that changed with the zoom. Only for the screen, not for exports
 */
class CrispRuling {
public:
    explicit CrispRuling(bool enabled);
    ~CrispRuling();
    CrispRuling(const CrispRuling&) = delete;
    CrispRuling& operator=(const CrispRuling&) = delete;

private:
    bool m_before;
};

/**
 * Paints the background of a page: the paper with its ruling, the background image or the PDF page.
 * @param pdfBackground rendered PDF page for PDF backgrounds, or nullptr to paint a placeholder
 */
void renderBackground(QPainter& p, const Page& page, const QImage* pdfBackground);

/**
 * Paints the elements of all layers, without the background.
 * @param exposed only elements intersecting this rectangle are painted
 */
/// @param vectorFormulas LaTeX formulas from their PDF, as vectors (for exports and printing); else as images
void renderLayers(QPainter& p, const Page& page, const QRectF& exposed, bool vectorFormulas = false);

/**
 * Paints a page in page coordinates (origin at the top left corner, unit: points).
 * @param pdfBackground rendered PDF page for PDF backgrounds, or nullptr to paint a placeholder
 * @param exposed only elements intersecting this rectangle are painted
 */
void renderPage(QPainter& p, const Page& page, const QImage* pdfBackground, const QRectF& exposed);

}  // namespace Renderer
