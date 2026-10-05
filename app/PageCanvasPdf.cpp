/*
 * Qournal
 *
 * The part of PageCanvas that works with the PDF of the background: selecting its text, its outline and links,
 * and the search in the PDF and in the texts of the document
 *
 * @license GNU GPLv2 or later
 */

#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <algorithm>
#include <cmath>

#ifdef HAVE_QTPDF
#include <QPdfBookmarkModel>
#include <QPdfLinkModel>
#include <QPdfSearchModel>
#include <QPdfSelection>
#endif

#include "PageCanvas.h"
#include "TextBlock.h"

namespace {

const QColor PDF_SELECTION_COLOR(0x33, 0x66, 0xff, 0x55);
const QColor SEARCH_RESULT_COLOR(0xff, 0xe1, 0x00, 0x70);
const QColor SEARCH_CURRENT_COLOR(0xff, 0x80, 0x00, 0x90);

/// Smaller movements of the pointer select nothing, in page units
bool validMotion(const QPointF& a, const QPointF& b) { return std::hypot(b.x() - a.x(), b.y() - a.y()) >= 2.0; }

// What markPdfSelection() draws
constexpr int MARK_HIGHLIGHT = 0;
constexpr int MARK_UNDERLINE = 1;
constexpr int MARK_STRIKE_THROUGH = 2;

}  // namespace

bool PageCanvas::pageHasPdf(int page) const {
#ifdef HAVE_QTPDF
    if (page < 0 || page >= pageCount() || m_pdf.status() != QPdfDocument::Status::Ready) {
        return false;
    }
    const Background& bg = m_doc.pages[static_cast<size_t>(page)].background;
    return bg.type == Background::Type::Pdf && bg.pdfPage >= 0 && bg.pdfPage < m_pdf.pageCount();
#else
    Q_UNUSED(page)
    return false;
#endif
}

QSizeF PageCanvas::pdfScale(int pageIndex) const {
#ifdef HAVE_QTPDF
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    const QSizeF size = m_pdf.pagePointSize(page.background.pdfPage);
    if (size.width() > 0 && size.height() > 0) {
        return QSizeF(page.width / size.width(), page.height / size.height());
    }
#else
    Q_UNUSED(pageIndex)
#endif
    return QSizeF(1, 1);
}

// Text selection

void PageCanvas::pdfSelectPress(const PointerInput& input) {
    if (!pageHasPdf(m_curPage)) {
        return;
    }
    m_action = Action::PdfSelect;
    m_pdfSelection = PdfSelectionState();
    m_pdfSelection.active = true;
    m_pdfSelection.page = m_curPage;
    m_pdfSelection.start = viewToPage(m_curPage, input.pos);
}

void PageCanvas::pdfSelectMove(const PointerInput& input) {
#ifdef HAVE_QTPDF
    PdfSelectionState& sel = m_pdfSelection;
    const int pdfPage = m_doc.pages[static_cast<size_t>(sel.page)].background.pdfPage;
    const QSizeF scale = pdfScale(sel.page);
    auto toPdf = [&](const QPointF& p) { return QPointF(p.x() / scale.width(), p.y() / scale.height()); };
    auto toPage = [&](const QRectF& r) {
        return QRectF(r.x() * scale.width(), r.y() * scale.height(), r.width() * scale.width(),
                      r.height() * scale.height());
    };
    const QPointF end = viewToPage(sel.page, input.pos);
    sel.bounds.clear();
    sel.text.clear();

    if (m_pressTool == SelectPdfTextRect) {
        // The characters whose middle is in the rectangle, line by line
        const QRectF area = QRectF(toPdf(sel.start), toPdf(end)).normalized();
        const PdfCharacters& characters = pdfCharacters(pdfPage);
        QRectF line;
        qsizetype last = -2;
        for (qsizetype i = 0; i < characters.boxes.size(); ++i) {
            const QRectF& box = characters.boxes[i];
            if (box.isEmpty() || !area.contains(box.center())) {
                continue;
            }
            // A new line starts where the characters are not next to each other at the same height
            const bool sameLine = line.isValid() && std::abs(box.center().y() - line.center().y()) < line.height() / 2;
            if (!sameLine) {
                if (line.isValid()) {
                    sel.bounds.append(toPage(line));
                    sel.text += u'\n';
                }
                line = box;
            } else {
                if (i != last + 1) {
                    sel.text += u' ';
                }
                line |= box;
            }
            sel.text += characters.text[i];
            last = i;
        }
        if (line.isValid()) {
            sel.bounds.append(toPage(line));
        }
    } else {
        // The text between the characters next to the two positions, in reading order. The positions do not
        // have to be on a character
        const PdfCharacters& characters = pdfCharacters(pdfPage);
        auto nearest = [&](const QPointF& pos) {
            qsizetype best = -1;
            double bestDistance = 0;
            for (qsizetype i = 0; i < characters.boxes.size(); ++i) {
                const QRectF& box = characters.boxes[i];
                if (box.isEmpty()) {
                    continue;
                }
                // The line counts more than the place in it
                const double dx = std::max({box.left() - pos.x(), pos.x() - box.right(), 0.0});
                const double dy = std::max({box.top() - pos.y(), pos.y() - box.bottom(), 0.0});
                const double distance = dx + 4 * dy;
                if (best < 0 || distance < bestDistance) {
                    best = i;
                    bestDistance = distance;
                }
            }
            return best;
        };
        const qsizetype from = nearest(toPdf(sel.start));
        const qsizetype to = nearest(toPdf(end));
        if (from >= 0 && to >= 0 && validMotion(sel.start, end)) {
            const qsizetype first = std::min(from, to);
            const qsizetype count = std::max(from, to) - first + 1;
            sel.text = characters.text.mid(first, count);
            sel.text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
            QRectF line;
            for (qsizetype i = first; i < first + count; ++i) {
                const QRectF& box = characters.boxes[i];
                if (box.isEmpty()) {
                    continue;
                }
                if (line.isValid() && std::abs(box.center().y() - line.center().y()) >= line.height() / 2) {
                    sel.bounds.append(toPage(line));
                    line = QRectF();
                }
                line |= box;
            }
            if (line.isValid()) {
                sel.bounds.append(toPage(line));
            }
        }
    }
    updateView();
#else
    Q_UNUSED(input)
#endif
}

const PageCanvas::PdfCharacters& PageCanvas::pdfCharacters(int pdfPage) {
    auto it = m_pdfCharacters.find(pdfPage);
    if (it == m_pdfCharacters.end()) {
        PdfCharacters characters;
#ifdef HAVE_QTPDF
        characters.text = m_pdf.getAllText(pdfPage).text();
        characters.boxes.reserve(characters.text.size());
        for (qsizetype i = 0; i < characters.text.size(); ++i) {
            characters.boxes.append(m_pdf.getSelectionAtIndex(pdfPage, static_cast<int>(i), 1).boundingRectangle());
        }
#endif
        it = m_pdfCharacters.insert(pdfPage, characters);
    }
    return *it;
}

QRectF PageCanvas::pdfSelectionRect() const {
    if (!hasPdfSelection() || m_pdfSelection.page >= m_pageRects.size()) {
        return {};
    }
    QRectF bounds;
    for (const QRectF& rect: m_pdfSelection.bounds) {
        bounds |= rect;
    }
    const QPointF topLeft = m_pageRects[m_pdfSelection.page].topLeft();
    return QRectF(worldToView(bounds.topLeft() + topLeft), bounds.size() * m_scale);
}

void PageCanvas::clearPdfSelection() {
    if (m_pdfSelection.active) {
        m_pdfSelection = PdfSelectionState();
        updateView();
        emit pdfSelectionChanged();
        emit pdfSelectionRectChanged();
    }
}

void PageCanvas::copyPdfSelection() {
    if (hasPdfSelection()) {
        QGuiApplication::clipboard()->setText(m_pdfSelection.text);
    }
}

void PageCanvas::highlightPdfSelection() { markPdfSelection(MARK_HIGHLIGHT); }

void PageCanvas::setPdfMarkerAlpha(int alpha) {
    alpha = std::clamp(alpha, 1, 255);
    if (alpha != m_pdfMarkerAlpha) {
        m_pdfMarkerAlpha = alpha;
        emit toolChanged();
    }
}

void PageCanvas::underlinePdfSelection() { markPdfSelection(MARK_UNDERLINE); }

void PageCanvas::strikeThroughPdfSelection() { markPdfSelection(MARK_STRIKE_THROUGH); }

/// Draws a stroke for each line of the selected text, in one step
void PageCanvas::markPdfSelection(int kind) {
    if (!hasPdfSelection()) {
        return;
    }
    const PdfSelectionState selection = m_pdfSelection;
    clearPdfSelection();
    finishInput();

    const int layer = activeLayer(selection.page);
    const auto& layers = m_doc.pages[static_cast<size_t>(selection.page)].layers;
    const size_t first = layers[static_cast<size_t>(layer)].elements.size();
    EditGroup group;
    for (const QRectF& rect: selection.bounds) {
        Stroke stroke;
        if (kind == MARK_HIGHLIGHT) {
            // As wide as the line is high
            stroke.tool = Stroke::Tool::Highlighter;
            stroke.color = m_toolStates[Highlighter].color;
            stroke.color.setAlpha(m_pdfMarkerAlpha);
            stroke.width = rect.height();
            stroke.cap = Qt::FlatCap;
            stroke.points = {QPointF(rect.left(), rect.center().y()), QPointF(rect.right(), rect.center().y())};
        } else {
            stroke.color = m_toolStates[Pen].color;
            stroke.width = thicknessOf(Pen, Fine);
            const double y = kind == MARK_UNDERLINE ? rect.bottom() : rect.center().y();
            stroke.points = {QPointF(rect.left(), y), QPointF(rect.right(), y)};
        }
        stroke.updateBounds();
        group.push_back(ElementEdit{true, selection.page, layer, first + group.size(), stroke});
    }
    perform(std::move(group));
}

// Outline and links

QVariantList PageCanvas::pdfOutline() const {
    QVariantList outline;
#ifdef HAVE_QTPDF
    if (m_pdf.status() != QPdfDocument::Status::Ready) {
        return outline;
    }
    QPdfBookmarkModel model;
    model.setDocument(const_cast<QPdfDocument*>(&m_pdf));
    // The tree in the order it is shown
    std::function<void(const QModelIndex&)> visit = [&](const QModelIndex& parent) {
        for (int row = 0; row < model.rowCount(parent); ++row) {
            const QModelIndex index = model.index(row, 0, parent);
            outline.append(
                    QVariantMap{{QStringLiteral("title"), index.data(static_cast<int>(QPdfBookmarkModel::Role::Title))},
                                {QStringLiteral("level"), index.data(static_cast<int>(QPdfBookmarkModel::Role::Level))},
                                {QStringLiteral("page"), index.data(static_cast<int>(QPdfBookmarkModel::Role::Page))}});
            visit(index);
        }
    };
    visit(QModelIndex());
#endif
    return outline;
}

bool PageCanvas::goToPdfPage(int pdfPage) {
    for (int i = 0; i < pageCount(); ++i) {
        const Background& bg = m_doc.pages[static_cast<size_t>(i)].background;
        if (bg.type == Background::Type::Pdf && bg.pdfPage == pdfPage) {
            setCurrentPage(i);
            return true;
        }
    }
    return false;
}

/// Follows the link of the PDF at a position: to another page, or to an address
bool PageCanvas::followPdfLink(int page, const QPointF& pagePos) {
#ifdef HAVE_QTPDF
    if (!pageHasPdf(page)) {
        return false;
    }
    const QSizeF scale = pdfScale(page);
    const QPointF pos(pagePos.x() / scale.width(), pagePos.y() / scale.height());
    QPdfLinkModel links;
    links.setDocument(&m_pdf);
    links.setPage(m_doc.pages[static_cast<size_t>(page)].background.pdfPage);
    for (int row = 0; row < links.rowCount({}); ++row) {
        const QModelIndex index = links.index(row, 0);
        if (!index.data(static_cast<int>(QPdfLinkModel::Role::Rectangle)).toRectF().contains(pos)) {
            continue;
        }
        const QUrl url = index.data(static_cast<int>(QPdfLinkModel::Role::Url)).toUrl();
        if (url.isValid() && !url.isEmpty()) {
            emit openLinkRequested(url.toString());
            return true;
        }
        return goToPdfPage(index.data(static_cast<int>(QPdfLinkModel::Role::Page)).toInt());
    }
#else
    Q_UNUSED(page)
    Q_UNUSED(pagePos)
#endif
    return false;
}

// Search

/// The places of the searched text on a page: in its texts and in its PDF. Found once and kept
const QList<QRectF>& PageCanvas::searchResults(int pageIndex) {
    auto it = m_search.results.find(pageIndex);
    if (it != m_search.results.end()) {
        return *it;
    }
    QList<QRectF> results;
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];

#ifdef HAVE_QTPDF
    if (pageHasPdf(pageIndex)) {
        const QSizeF scale = pdfScale(pageIndex);
        QPdfSearchModel model;
        model.setDocument(&m_pdf);
        model.setSearchString(m_search.text);
        for (const QPdfLink& result: model.resultsOnPage(page.background.pdfPage)) {
            QRectF bounds;
            for (const QRectF& rect: result.rectangles()) {
                bounds |= rect;
            }
            results.append(QRectF(bounds.x() * scale.width(), bounds.y() * scale.height(),
                                  bounds.width() * scale.width(), bounds.height() * scale.height()));
        }
    }
#endif

    for (const Layer& layer: page.layers) {
        if (!layer.visible) {
            continue;
        }
        for (const Element& element: layer.elements) {
            const auto* text = std::get_if<TextElement>(&element);
            if (!text) {
                continue;
            }
            const TextStyle style{text->font, text->size, text->align, text->wrap, text->justify};
            const TextBlock block(text->text, style);
            const QTransform transform =
                    text->matrix ? toTransform(*text->matrix) : QTransform::fromTranslate(text->pos.x(), text->pos.y());
            for (qsizetype pos = text->text.indexOf(m_search.text, 0, Qt::CaseInsensitive); pos >= 0;
                 pos = text->text.indexOf(m_search.text, pos + 1, Qt::CaseInsensitive)) {
                QRectF bounds;
                const auto rects = block.rangeRects(static_cast<int>(pos), static_cast<int>(m_search.text.size()));
                for (const QRectF& rect: rects) {
                    bounds |= transform.mapRect(rect);
                }
                results.append(bounds);
            }
        }
    }
    // In reading order
    std::stable_sort(results.begin(), results.end(), [](const QRectF& a, const QRectF& b) {
        return a.center().y() < b.center().y() - 1 || (std::abs(a.center().y() - b.center().y()) <= 1 && a.x() < b.x());
    });
    return *m_search.results.insert(pageIndex, results);
}

int PageCanvas::searchResultCount() const {
    return m_search.page >= 0 ? static_cast<int>(m_search.results.value(m_search.page).size()) : 0;
}

bool PageCanvas::search(const QString& text) {
    m_search = SearchState();
    m_search.text = text;
    if (text.isEmpty() || m_doc.pages.empty()) {
        updateView();
        emit searchChanged();
        return false;
    }
    m_search.page = std::clamp(m_currentPage, 0, pageCount() - 1);
    m_search.index = -1;
    return searchStep(true, true);
}

bool PageCanvas::searchNext() { return searching() && searchStep(true, false); }

bool PageCanvas::searchPrevious() { return searching() && searchStep(false, false); }

/// Goes to the next or previous result, through the pages and around the end of the document
bool PageCanvas::searchStep(bool forward, bool includeCurrent) {
    const int pages = pageCount();
    int page = std::clamp(m_search.page, 0, pages - 1);
    int index = m_search.index + (forward ? 1 : -1);
    if (includeCurrent) {
        index = 0;
    }
    for (int visited = 0; visited <= pages; ++visited) {
        const int count = static_cast<int>(searchResults(page).size());
        if (index >= 0 && index < count) {
            m_search.page = page;
            m_search.index = index;
            showSearchResult();
            emit searchChanged();
            return true;
        }
        page = (page + (forward ? 1 : pages - 1)) % pages;
        index = forward ? 0 : static_cast<int>(searchResults(page).size()) - 1;
    }
    m_search.page = -1;
    m_search.index = -1;
    updateView();
    emit searchChanged();
    return false;
}

void PageCanvas::showSearchResult() {
    const QRectF rect = m_search.results.value(m_search.page).value(m_search.index);
    if (std::abs(m_search.page - m_currentPage) > 1) {
        notePlace();
    }
    setCurrentPageInternal(m_search.page);
    const QRectF world = rect.translated(m_pageRects[m_search.page].topLeft());
    const QRectF visible(m_origin, size() / m_scale);
    if (m_presentationMode) {
        fitPage();
    } else if (!visible.contains(world)) {
        // In the middle of the view
        m_origin = world.center() - QPointF(width(), height()) / (2 * m_scale);
        clampView();
        viewMoved();
    }
    updateView();
}

void PageCanvas::clearSearch() {
    if (searching()) {
        m_search = SearchState();
        updateView();
        emit searchChanged();
    }
}

// Painting

void PageCanvas::paintPdfOverlays(QPainter* painter) {
    auto fillRects = [&](int page, const QList<QRectF>& rects, const QColor& color) {
        if (page < 0 || page >= m_pageRects.size() || rects.isEmpty()) {
            return;
        }
        painter->save();
        applyPageTransform(*painter, page);
        painter->setPen(Qt::NoPen);
        painter->setBrush(color);
        for (const QRectF& rect: rects) {
            painter->drawRect(rect);
        }
        painter->restore();
    };

    if (searching()) {
        // The results on the pages that are in view
        const QRectF visible(m_origin, size() / m_scale);
        for (int i = 0; i < m_pageRects.size(); ++i) {
            if (m_pageRects[i].intersects(visible)) {
                fillRects(i, searchResults(i), SEARCH_RESULT_COLOR);
            }
        }
        if (m_search.page >= 0 && m_search.index >= 0) {
            fillRects(m_search.page, {m_search.results.value(m_search.page).value(m_search.index)},
                      SEARCH_CURRENT_COLOR);
        }
    }
    if (m_pdfSelection.active) {
        fillRects(m_pdfSelection.page, m_pdfSelection.bounds, PDF_SELECTION_COLOR);
    }
}
