#include "Renderer.h"

#include <QCoreApplication>
#include <QFont>
#include <QFontMetricsF>
#include <QPen>
#include <cmath>

#include "BackgroundConfig.h"
#include "PdfFile.h"
#include "PdfPage.h"
#include "TextBlock.h"

using namespace Qt::StringLiterals;

namespace {

constexpr int HIGHLIGHTER_ALPHA = 120;

// Rulings, ported from the background views of Xournal++ (src/core/view/background)
constexpr double RULING_HEADER = 80.0;
constexpr double RULING_FOOTER = 60.0;
constexpr double STAVES_MARGIN = 50.0;
constexpr double STAVES_SPACING = 40.0;      // between two staves
constexpr double STAVES_LINE_SPACING = 5.0;  // between two lines of a staff
constexpr double DEFAULT_RASTER = 14.17;     // 5 mm

const QColor COLOR_SILVER(0xbd, 0xbd, 0xbd);
const QColor COLOR_DARK_SLATE_GRAY(0x43, 0x43, 0x43);
const QColor COLOR_DODGER_BLUE(0x40, 0xa0, 0xff);
const QColor COLOR_DEEP_PINK(0xff, 0x00, 0x80);
const QColor COLOR_MIDNIGHT_BLUE(0x22, 0x00, 0x80);

bool isLight(const QColor& c) { return c.red() + c.green() + c.blue() > 0x180; }

/// Line colour: "f1"/"f2" on light paper, "af1"/"af2" on dark paper
QColor foreground(const BackgroundConfig& config, const QColor& paper, int number, const QColor& fallback,
                  const QColor& altFallback) {
    const QString key = QStringLiteral("f%1").arg(number);
    return isLight(paper) ? config.color(key, fallback) : config.color(u'a' + key, altFallback);
}

thread_local bool crispRuling = false;

/**
 * The pen of a ruling. For electronic paper (Renderer::CrispRuling) it is black, or white on dark paper, and as
 * wide as a whole number of pixels of the device, one at least: drawn without smoothed edges, a line thinner
 * than a pixel has gaps that change with the zoom, and the screen shows a light tone with noise
 */
QPen rulingPen(const QPainter& p, const QColor& paper, QPen pen) {
    if (!crispRuling) {
        return pen;
    }
    const double scale = std::sqrt(std::abs(p.deviceTransform().determinant()));
    if (scale <= 0) {
        return pen;
    }
    pen.setColor(isLight(paper) ? Qt::black : Qt::white);
    pen.setWidthF(std::max(1.0, std::floor(pen.widthF() * scale)) / scale);
    return pen;
}

struct IndexBounds {
    int min;
    int max;
};

/// Indices i for which i * step lies in [min, max] and keeps the margin to both ends of length
IndexBounds indexBounds(double min, double max, double step, double margin, double length) {
    return {static_cast<int>(std::ceil(std::max(min, margin) / step)),
            static_cast<int>(std::floor(std::min(max, length - margin) / step))};
}

void renderRuled(QPainter& p, const Page& page, const BackgroundConfig& config, bool marginLine) {
    const QColor& paper = page.background.color;
    const double lineWidth = config.number(u"lw"_s, 0.5);
    const double spacing = config.number(u"r1"_s, 24.0);
    if (spacing <= 0) {
        return;
    }

    const IndexBounds rows =
            indexBounds(-RULING_HEADER - 0.5 * lineWidth, page.height - RULING_HEADER + 0.5 * lineWidth, spacing, 0.0,
                        page.height - RULING_HEADER - RULING_FOOTER);
    p.setPen(rulingPen(p, page.background.color,
                       QPen(foreground(config, paper, 1, COLOR_DODGER_BLUE, COLOR_DARK_SLATE_GRAY), lineWidth,
                            Qt::SolidLine, Qt::FlatCap)));
    for (int i = rows.min; i <= rows.max; ++i) {
        const double y = RULING_HEADER + i * spacing;
        p.drawLine(QPointF(0, y), QPointF(page.width, y));
    }

    if (marginLine) {
        double margin = config.number(u"m1"_s, 72.0);
        if (margin < 0) {
            // A negative value puts the margin line on the right hand side
            margin += page.width;
        }
        p.setPen(rulingPen(p, page.background.color,
                           QPen(foreground(config, paper, 2, COLOR_DEEP_PINK, COLOR_MIDNIGHT_BLUE), lineWidth,
                                Qt::SolidLine, Qt::FlatCap)));
        p.drawLine(QPointF(margin, 0), QPointF(margin, page.height));
    }
}

void renderGraph(QPainter& p, const Page& page, const BackgroundConfig& config) {
    const double lineWidth = config.number(u"lw"_s, 0.5);
    const double squareSize = config.number(u"r1"_s, DEFAULT_RASTER);
    const double margin = config.number(u"m1"_s, 0.0);
    const int boldInterval = static_cast<int>(config.number(u"bli"_s, 0));  // 0: no bold lines
    const double boldLineWidth = config.number(u"blw"_s, 1.5);
    const bool roundUpMargin = margin > 0.0 && config.number(u"rm"_s, 0) != 0;
    if (squareSize <= 0) {
        return;
    }

    double minX = 0, maxX = page.width, minY = 0, maxY = page.height;
    if (margin > 0.0) {
        minX = margin;
        maxX = page.width - margin;
        minY = margin;
        maxY = page.height - margin;
    }
    const double halfLineWidth = 0.5 * (boldInterval > 0 ? boldLineWidth : lineWidth);
    const double clearance = std::max(margin, halfLineWidth);
    const IndexBounds cols = indexBounds(minX - halfLineWidth, maxX + halfLineWidth, squareSize, clearance, page.width);
    const IndexBounds rows =
            indexBounds(minY - halfLineWidth, maxY + halfLineWidth, squareSize, clearance, page.height);
    if (roundUpMargin) {
        // The lines end at the outermost lines instead of at the margin
        minX = std::max(minX, squareSize * cols.min);
        maxX = std::min(maxX, squareSize * cols.max);
        minY = std::max(minY, squareSize * rows.min);
        maxY = std::min(maxY, squareSize * rows.max);
    }

    const QColor color = foreground(config, page.background.color, 1, COLOR_SILVER, COLOR_DARK_SLATE_GRAY);
    auto drawLines = [&](double width, bool bold) {
        p.setPen(rulingPen(p, page.background.color, QPen(color, width, Qt::SolidLine, Qt::SquareCap)));
        auto wanted = [&](int i) { return boldInterval <= 0 || (i % boldInterval == 0) == bold; };
        if (minY < maxY) {
            for (int i = cols.min; i <= cols.max; ++i) {
                if (wanted(i)) {
                    p.drawLine(QPointF(i * squareSize, minY), QPointF(i * squareSize, maxY));
                }
            }
        }
        if (minX < maxX) {
            for (int i = rows.min; i <= rows.max; ++i) {
                if (wanted(i)) {
                    p.drawLine(QPointF(minX, i * squareSize), QPointF(maxX, i * squareSize));
                }
            }
        }
    };
    drawLines(lineWidth, false);
    if (boldInterval > 0) {
        drawLines(boldLineWidth, true);
    }
}

void renderDotted(QPainter& p, const Page& page, const BackgroundConfig& config) {
    const double lineWidth = config.number(u"lw"_s, 1.5);
    const double squareSize = config.number(u"r1"_s, DEFAULT_RASTER);
    if (squareSize <= 0) {
        return;
    }
    // The clearance keeps dots from being cut at the edge of the sheet
    const double halfLineWidth = 0.5 * lineWidth;
    const IndexBounds cols =
            indexBounds(-halfLineWidth, page.width + halfLineWidth, squareSize, halfLineWidth, page.width);
    const IndexBounds rows =
            indexBounds(-halfLineWidth, page.height + halfLineWidth, squareSize, halfLineWidth, page.height);

    p.setPen(rulingPen(p, page.background.color,
                       QPen(foreground(config, page.background.color, 1, COLOR_SILVER, COLOR_DARK_SLATE_GRAY),
                            lineWidth, Qt::SolidLine, Qt::RoundCap)));
    for (int i = cols.min; i <= cols.max; ++i) {
        for (int j = rows.min; j <= rows.max; ++j) {
            p.drawPoint(QPointF(i * squareSize, j * squareSize));
        }
    }
}

void renderStaves(QPainter& p, const Page& page, const BackgroundConfig& config) {
    const double lineWidth = config.number(u"lw"_s, 0.5);
    const double staffHeight = lineWidth + 4 * STAVES_LINE_SPACING;
    // The 4 * lineWidth do not make a lot of sense, but Xournal++ keeps them so that existing documents do not change
    const double staffOffset = STAVES_SPACING + staffHeight + 4 * lineWidth;
    const IndexBounds staves = indexBounds(-RULING_HEADER - staffHeight, page.height + 0.5 * lineWidth - RULING_HEADER,
                                           staffOffset, 0.0, page.height - RULING_HEADER - RULING_FOOTER);

    p.setPen(rulingPen(p, page.background.color,
                       QPen(foreground(config, page.background.color, 1, Qt::black, Qt::white), lineWidth,
                            Qt::SolidLine, Qt::SquareCap)));
    const double left = STAVES_MARGIN;
    const double right = page.width - STAVES_MARGIN;
    for (int i = staves.min; i <= staves.max; ++i) {
        const double top = RULING_HEADER + i * staffOffset;
        const double bottom = top + 4 * STAVES_LINE_SPACING;
        for (int line = 0; line < 5; ++line) {
            const double y = top + line * STAVES_LINE_SPACING;
            p.drawLine(QPointF(left, y), QPointF(right, y));
        }
        p.drawLine(QPointF(left, top), QPointF(left, bottom));
        p.drawLine(QPointF(right, top), QPointF(right, bottom));
    }
}

void renderIsometric(QPainter& p, const Page& page, const BackgroundConfig& config, bool dotted) {
    const double lineWidth = config.number(u"lw"_s, dotted ? 1.5 : 1.0);
    const double triangleSize = config.number(u"r1"_s, DEFAULT_RASTER);
    if (triangleSize <= 0) {
        return;
    }
    const double xstep = std::sqrt(3.0) / 2.0 * triangleSize;
    const double ystep = triangleSize / 2.0;

    // The largest grid that keeps a margin, centered on the page
    const double margin = triangleSize;
    const int cols = static_cast<int>(std::floor((page.width - 2 * margin) / xstep));
    const int rows = static_cast<int>(std::floor((page.height - 2 * margin) / ystep));
    if (cols <= 0 || rows <= 0) {
        return;
    }
    const double contentWidth = cols * xstep;
    const double contentHeight = rows * ystep;
    const QPointF offset((page.width - contentWidth) / 2, (page.height - contentHeight) / 2);

    p.setPen(rulingPen(p, page.background.color,
                       QPen(foreground(config, page.background.color, 1, COLOR_SILVER, COLOR_DARK_SLATE_GRAY),
                            lineWidth, Qt::SolidLine, Qt::RoundCap)));

    if (dotted) {
        for (int col = 0; col <= cols; ++col) {
            const bool evenCol = col % 2 == 0;
            const double colOffset = evenCol ? ystep : 0.0;
            const int rowsInCol = evenCol ? rows - 2 : rows;
            for (int row = 0; row <= rowsInCol; row += 2) {
                p.drawPoint(offset + QPointF(col * xstep, colOffset + row * ystep));
            }
        }
        return;
    }

    auto drawLine = [&](double x1, double y1, double x2, double y2) {
        p.drawLine(offset + QPointF(x1, y1), offset + QPointF(x2, y2));
    };
    drawLine(0.0, 0.0, contentWidth, 0.0);
    drawLine(0.0, contentHeight, contentWidth, contentHeight);
    for (int col = 0; col <= cols; ++col) {
        drawLine(col * xstep, 0.0, col * xstep, contentHeight);
    }

    const int hdiags = cols / 2;
    const int vdiags = rows / 2;
    const int diags = hdiags + vdiags;
    const int hcorr = cols - 2 * hdiags;
    const int vcorr = rows - 2 * vdiags;

    // Diagonals starting in the top left corner (left-down)
    for (int d = 0; d < diags + hcorr * vcorr; ++d) {
        // Point 1 travels from top left to top right, then down to bottom right
        double x1 = contentWidth, y1 = 0.0;
        if (d < hdiags) {
            x1 = xstep + d * 2 * xstep;
        } else {
            y1 = ystep + (d - hdiags) * 2 * ystep - hcorr * ystep;
        }
        // Point 2 travels from top left to bottom left, then right to bottom right
        double x2 = 0.0, y2 = contentHeight;
        if (d < vdiags) {
            y2 = ystep + d * 2 * ystep;
        } else {
            x2 = xstep + (d - vdiags) * 2 * xstep - vcorr * xstep;
        }
        drawLine(x1, y1, x2, y2);
    }

    // Diagonals starting in the top right corner (right-down)
    for (int d = 0; d < diags; ++d) {
        // Point 1 travels from top right to top left, then down to bottom left
        double x1 = 0.0, y1 = 0.0;
        if (d < hdiags) {
            x1 = contentWidth - (xstep + d * 2 * xstep) - hcorr * xstep;
        } else {
            y1 = ystep + (d - hdiags) * 2 * ystep;
        }
        // Point 2 travels from top right to bottom right, then left to bottom left
        double x2 = contentWidth, y2 = contentHeight;
        if (d < vdiags) {
            y2 = ystep + d * 2 * ystep + hcorr * ystep;
        } else {
            x2 = contentWidth - (xstep + (d - vdiags) * 2 * xstep) + (vcorr - hcorr) * xstep;
        }
        drawLine(x1, y1, x2, y2);
    }
}

void renderRuling(QPainter& p, const Page& page) {
    const Background& bg = page.background;
    const BackgroundConfig config(bg.config);
    if (bg.style == u"ruled") {
        renderRuled(p, page, config, false);
    } else if (bg.style == u"lined") {
        renderRuled(p, page, config, true);
    } else if (bg.style == u"graph") {
        renderGraph(p, page, config);
    } else if (bg.style == u"dotted") {
        renderDotted(p, page, config);
    } else if (bg.style == u"staves") {
        renderStaves(p, page, config);
    } else if (bg.style == u"isodotted") {
        renderIsometric(p, page, config, true);
    } else if (bg.style == u"isograph") {
        renderIsometric(p, page, config, false);
    }
    // "plain" and unknown styles: the paper colour only
}

TextStyle styleOf(const TextElement& text) {
    return TextStyle{text.font, text.size, text.align, text.wrap, text.justify, false};
}

TextStyle styleOf(const LinkElement& link) { return TextStyle{link.font, link.size, link.align, -1, false, true}; }

QTransform textTransform(const TextElement& text) {
    return text.matrix ? toTransform(*text.matrix) : QTransform::fromTranslate(text.pos.x(), text.pos.y());
}

void renderTextBlock(QPainter& p, const TextBlock& block, const QTransform& transform, const QColor& color) {
    p.save();
    p.setTransform(transform, true);
    block.draw(p, color);
    p.restore();
}

void renderText(QPainter& p, const TextElement& text) {
    renderTextBlock(p, TextBlock(text.text, styleOf(text)), textTransform(text), text.color);
}

void renderLink(QPainter& p, const LinkElement& link) {
    renderTextBlock(p, TextBlock(link.text, styleOf(link)), toTransform(link.matrix), link.color);
}

/// A formula from its PDF, as vectors. False if that cannot be done
bool renderFormulaVectors(QPainter& p, const ImageElement& image) {
    if (!image.tex || !image.data.startsWith("%PDF")) {
        return false;
    }
    Pdf::Reader reader;
    if (!reader.load(image.data)) {
        return false;
    }
    const std::vector<Pdf::PageInfo> pages = reader.pages();
    if (pages.empty()) {
        return false;
    }
    Pdf::PageDrawer drawer(reader);
    p.save();
    QSizeF size;
    if (image.matrix && image.naturalSize.isValid()) {
        p.setTransform(toTransform(*image.matrix), true);
        size = image.naturalSize;
    } else {
        p.translate(image.rect.topLeft());
        size = image.rect.size();
    }
    const bool drawn = drawer.draw(p, pages.front(), size);
    p.restore();
    return drawn;
}

void renderImage(QPainter& p, const ImageElement& image, bool vectorFormulas) {
    if (vectorFormulas && renderFormulaVectors(p, image)) {
        return;
    }
    if (image.image.isNull()) {
        // Keep the place visible, e.g. for formulas if Qt PDF is not available
        p.save();
        p.setPen(QPen(QColor(0x88, 0x88, 0x88), 0.5, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        p.drawRect(image.rect);
        p.restore();
        return;
    }
    if (image.matrix) {
        p.save();
        p.setTransform(toTransform(*image.matrix), true);
        p.drawImage(QRectF(QPointF(0, 0), image.naturalSize), image.image);
        p.restore();
    } else {
        p.drawImage(image.rect, image.image);
    }
}

}  // namespace

void Renderer::renderBackground(QPainter& p, const Page& page, const QImage* pdfBackground) {
    const QRectF rect(0, 0, page.width, page.height);
    const Background& bg = page.background;
    p.save();

    if (!page.backgroundVisible) {
        // A checkerboard stands for "nothing", as in Xournal++
        constexpr int CHECKER_SIZE = 8;
        p.fillRect(rect, QColor(0xc0, 0xc0, 0xc0));
        p.setClipRect(rect, Qt::IntersectClip);
        for (int row = 0; row * CHECKER_SIZE < page.height; ++row) {
            for (int col = row % 2; col * CHECKER_SIZE < page.width; col += 2) {
                p.fillRect(QRectF(col * CHECKER_SIZE, row * CHECKER_SIZE, CHECKER_SIZE, CHECKER_SIZE),
                           QColor(0x80, 0x80, 0x80));
            }
        }
        p.restore();
        return;
    }

    if (bg.type == Background::Type::Pdf) {
        p.fillRect(rect, Qt::white);
        if (pdfBackground && !pdfBackground->isNull()) {
            p.drawImage(rect, *pdfBackground);
        } else {
            p.setPen(QColor(0x88, 0x88, 0x88));
            QFont font;
            font.setPixelSize(14);
            p.setFont(font);
            p.drawText(rect, Qt::AlignCenter,
                       QCoreApplication::translate("Renderer", "PDF background (page %1) not available")
                               .arg(bg.pdfPage + 1));
        }
    } else if (bg.type == Background::Type::Pixmap) {
        p.fillRect(rect, Qt::white);
        if (!bg.pixmap.isNull()) {
            p.drawImage(rect, bg.pixmap);
        }
    } else {
        p.fillRect(rect, bg.color);
        renderRuling(p, page);
    }

    p.restore();
}

namespace {
thread_local bool patternFills = false;

/// The pattern that covers about as much as a colour of this opacity; the patterns of Qt are dots in a grid of
/// 8 x 8 pixels of the device
Qt::BrushStyle patternFor(int alpha) {
    static const Qt::BrushStyle PATTERNS[] = {Qt::Dense7Pattern, Qt::Dense6Pattern, Qt::Dense5Pattern,
                                              Qt::Dense4Pattern, Qt::Dense3Pattern, Qt::Dense2Pattern,
                                              Qt::Dense1Pattern};
    const int index = qRound(alpha / 255.0 * 8) - 1;
    return index < 0 ? Qt::NoBrush : index > 6 ? Qt::SolidPattern : PATTERNS[index];
}
}  // namespace

Renderer::CrispRuling::CrispRuling(bool enabled): m_before(crispRuling) { crispRuling = enabled; }

Renderer::CrispRuling::~CrispRuling() { crispRuling = m_before; }

Renderer::PatternFills::PatternFills(bool enabled): m_before(patternFills) { patternFills = enabled; }

Renderer::PatternFills::~PatternFills() { patternFills = m_before; }

void Renderer::renderStroke(QPainter& p, const Stroke& s) {
    if (s.points.isEmpty()) {
        return;
    }
    p.save();

    QColor color = s.color;
    if (s.tool == Stroke::Tool::Highlighter) {
        if (color.alpha() == 255) {
            color.setAlpha(HIGHLIGHTER_ALPHA);
        }
        p.setCompositionMode(QPainter::CompositionMode_Multiply);
    }

    if (s.fill >= 0 && s.points.size() > 2) {
        QColor fillColor = color;
        fillColor.setAlpha(s.fill);
        p.setPen(Qt::NoPen);
        if (patternFills && s.tool != Stroke::Tool::Highlighter) {
            fillColor.setAlpha(255);
            p.setBrush(QBrush(fillColor, patternFor(s.fill)));
        } else {
            p.setBrush(fillColor);
        }
        p.drawPolygon(s.points.constData(), static_cast<int>(s.points.size()));
    }

    QPen pen(color, s.width, Qt::SolidLine, s.cap, Qt::RoundJoin);
    // The dashes are lengths on the page, as in Xournal++; Qt counts them in widths of the pen
    auto setDashes = [&](double width, double offset) {
        if (s.dashes.isEmpty() || width <= 0) {
            return;
        }
        QList<qreal> pattern;
        pattern.reserve(s.dashes.size());
        for (qreal dash: s.dashes) {
            pattern.append(dash / width);
        }
        pen.setDashPattern(pattern);
        pen.setDashOffset(offset / width);
    };
    setDashes(s.width, 0);
    p.setBrush(Qt::NoBrush);

    if (s.hasPressure()) {
        // Each segment has its width; the pattern goes on where the segment before ended
        double travelled = 0;
        for (qsizetype i = 0; i + 1 < s.points.size(); ++i) {
            pen.setWidthF(s.widths[i]);
            setDashes(s.widths[i], travelled);
            travelled += QLineF(s.points[i], s.points[i + 1]).length();
            p.setPen(pen);
            if (s.points[i] == s.points[i + 1]) {
                p.drawPoint(s.points[i]);
            } else {
                p.drawLine(s.points[i], s.points[i + 1]);
            }
        }
    } else {
        p.setPen(pen);
        const bool isDot = s.points.size() == 1 || (s.points.size() == 2 && s.points[0] == s.points[1]);
        if (isDot) {
            p.drawPoint(s.points.first());
        } else {
            p.drawPolyline(s.points.constData(), static_cast<int>(s.points.size()));
        }
    }

    p.restore();
}

QRectF Renderer::elementBounds(const Element& element) {
    if (const auto* stroke = std::get_if<Stroke>(&element)) {
        return stroke->bounds;
    }
    if (const auto* text = std::get_if<TextElement>(&element)) {
        const QSizeF size = TextBlock(text->text, styleOf(*text)).size();
        return textTransform(*text).mapRect(QRectF(QPointF(0, 0), size));
    }
    if (const auto* image = std::get_if<ImageElement>(&element)) {
        if (image->matrix && image->naturalSize.isValid()) {
            return toTransform(*image->matrix).mapRect(QRectF(QPointF(0, 0), image->naturalSize));
        }
        return image->rect;
    }
    const auto& link = std::get<LinkElement>(element);
    const QSizeF size = TextBlock(link.text, styleOf(link)).size();
    return toTransform(link.matrix).mapRect(QRectF(QPointF(0, 0), size));
}

void Renderer::renderLayers(QPainter& p, const Page& page, const QRectF& exposed, bool vectorFormulas) {
    for (const Layer& layer: page.layers) {
        if (!layer.visible) {
            continue;
        }
        for (const Element& element: layer.elements) {
            if (const auto* stroke = std::get_if<Stroke>(&element)) {
                if (stroke->bounds.intersects(exposed)) {
                    renderStroke(p, *stroke);
                }
            } else if (const auto* text = std::get_if<TextElement>(&element)) {
                renderText(p, *text);
            } else if (const auto* image = std::get_if<ImageElement>(&element)) {
                // The rectangle of a turned image is not where it is drawn
                if (elementBounds(element).intersects(exposed)) {
                    renderImage(p, *image, vectorFormulas);
                }
            } else if (const auto* link = std::get_if<LinkElement>(&element)) {
                renderLink(p, *link);
            }
        }
    }
}

void Renderer::renderPage(QPainter& p, const Page& page, const QImage* pdfBackground, const QRectF& exposed) {
    renderBackground(p, page, pdfBackground);
    renderLayers(p, page, exposed);
}
