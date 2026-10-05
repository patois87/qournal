/*
 * Qournal
 *
 * The part of PageCanvas for moving through the document and for pages that come from elsewhere: the history
 * of visited places, the pages of the PDF that are not in the document yet, an image as background
 *
 * @license GNU GPLv2 or later
 */

#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <algorithm>

#include "PageCanvas.h"

namespace {

constexpr qsizetype MAX_PLACES = 100;

}  // namespace

// The history of visited places

void PageCanvas::notePlace() {
    const Place place{m_currentPage, m_origin};
    if (!m_placesBack.isEmpty() && m_placesBack.last().page == place.page &&
        m_placesBack.last().origin == place.origin) {
        return;
    }
    m_placesBack.append(place);
    if (m_placesBack.size() > MAX_PLACES) {
        m_placesBack.removeFirst();
    }
    m_placesForward.clear();
    emit navigationChanged();
}

void PageCanvas::goToPlace(const Place& place) {
    if (m_presentationMode) {
        setCurrentPageInternal(std::clamp(place.page, 0, pageCount() - 1));
        fitPage();
        return;
    }
    m_origin = place.origin;
    clampView();
    updateCurrentPage();
    viewMoved();
}

int PageCanvas::nextAnnotatedPage() const {
    for (int index = m_currentPage + 1; index < pageCount(); ++index) {
        for (const Layer& layer: m_doc.pages[static_cast<size_t>(index)].layers) {
            if (!layer.elements.empty()) {
                return index;
            }
        }
    }
    return -1;
}

QVariantList PageCanvas::recordings() const {
    struct Recording {
        QString file;
        int elements = 0;
        QMap<int, qint64> firstOnPage;  ///< page -> earliest timestamp there
    };
    QList<Recording> list;
    for (int index = 0; index < pageCount(); ++index) {
        for (const Layer& layer: m_doc.pages[static_cast<size_t>(index)].layers) {
            for (const Element& element: layer.elements) {
                const AudioRef* audio = nullptr;
                if (const auto* stroke = std::get_if<Stroke>(&element)) {
                    audio = &stroke->audio;
                } else if (const auto* text = std::get_if<TextElement>(&element)) {
                    audio = &text->audio;
                }
                if (!audio || audio->filename.isEmpty()) {
                    continue;
                }
                auto it = std::find_if(list.begin(), list.end(),
                                       [audio](const Recording& r) { return r.file == audio->filename; });
                if (it == list.end()) {
                    list.append({audio->filename, 0, {}});
                    it = list.end() - 1;
                }
                ++it->elements;
                auto mark = it->firstOnPage.find(index);
                if (mark == it->firstOnPage.end() || audio->timestamp < *mark) {
                    it->firstOnPage[index] = audio->timestamp;
                }
            }
        }
    }
    QVariantList result;
    for (const Recording& recording: std::as_const(list)) {
        QList<std::pair<qint64, int>> marks;
        for (auto it = recording.firstOnPage.cbegin(); it != recording.firstOnPage.cend(); ++it) {
            marks.append({it.value(), it.key()});
        }
        std::sort(marks.begin(), marks.end());
        QVariantList markList;
        for (const auto& [timestamp, page]: std::as_const(marks)) {
            markList.append(QVariantMap{{QStringLiteral("page"), page}, {QStringLiteral("timestamp"), timestamp}});
        }
        result.append(QVariantMap{{QStringLiteral("file"), recording.file},
                                  {QStringLiteral("elements"), recording.elements},
                                  {QStringLiteral("marks"), markList}});
    }
    return result;
}

int PageCanvas::previousAnnotatedPage() const {
    for (int index = std::min(m_currentPage, pageCount()) - 1; index >= 0; --index) {
        for (const Layer& layer: m_doc.pages[static_cast<size_t>(index)].layers) {
            if (!layer.elements.empty()) {
                return index;
            }
        }
    }
    return -1;
}

void PageCanvas::navigateBack() {
    if (m_placesBack.isEmpty()) {
        return;
    }
    m_placesForward.append(Place{m_currentPage, m_origin});
    goToPlace(m_placesBack.takeLast());
    emit navigationChanged();
}

void PageCanvas::navigateForward() {
    if (m_placesForward.isEmpty()) {
        return;
    }
    m_placesBack.append(Place{m_currentPage, m_origin});
    goToPlace(m_placesForward.takeLast());
    emit navigationChanged();
}

// Pages

int PageCanvas::appendNewPdfPages() {
#ifdef HAVE_QTPDF
    QSet<int> used;
    for (const Page& page: m_doc.pages) {
        if (page.background.type == Background::Type::Pdf) {
            used.insert(page.background.pdfPage);
        }
    }
    EditGroup group;
    int index = pageCount();
    for (int i = 0; i < pdfPageCount(); ++i) {
        if (used.contains(i)) {
            continue;
        }
        Page page;
        const QSizeF size = m_pdf.pagePointSize(i);
        page.width = size.width();
        page.height = size.height();
        page.background.type = Background::Type::Pdf;
        page.background.pdfPage = i;
        page.layers.emplace_back();
        group.push_back(PageEdit{true, index++, std::move(page)});
    }
    const int count = static_cast<int>(group.size());
    perform(std::move(group));
    return count;
#else
    return 0;
#endif
}

bool PageCanvas::setPageBackgroundImage(int page, const QUrl& url) {
    if (page < 0 || page >= pageCount()) {
        return false;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QImage image(path);
    if (image.isNull()) {
        emit loadFailed(tr("\"%1\" is not an image that can be read").arg(path));
        return false;
    }
    // As in Xournal++: the page takes the size of the image, and the image is kept next to the document when it
    // is saved. Its name has to differ from those of the other pages
    QSet<QString> names;
    for (const Page& other: m_doc.pages) {
        names.insert(other.background.filename);
    }
    int number = 1;
    while (names.contains(QStringLiteral("bg_%1.png").arg(number))) {
        ++number;
    }

    const PageFormat before = pageFormat(page);
    PageFormat after = before;
    after.width = image.width();
    after.height = image.height();
    after.background = Background();
    after.background.name = before.background.name;
    after.background.type = Background::Type::Pixmap;
    after.background.domain = QStringLiteral("attach");
    after.background.filename = QStringLiteral("bg_%1.png").arg(number);
    after.background.pixmap = image;
    perform({PageFormatEdit{page, before, after}});
    return true;
}

void PageCanvas::setPairedPagesOffset(int offset) {
    LayoutSettings settings = m_layoutSettings;
    settings.pairsOffset = std::max(offset, 0);
    setLayoutSettings(settings);
}

// The position of the pointer, for an audience

void PageCanvas::setHighlightPosition(bool highlight) {
    if (m_highlightPosition != highlight) {
        m_highlightPosition = highlight;
        updateView();
        emit highlightPositionChanged();
    }
}

void PageCanvas::notePointer(const QPointF& pos) {
    if (m_pointerPos == pos) {
        return;
    }
    m_pointerPos = pos;
    // The square of the eraser follows the pointer too
    const bool eraserShown = m_tool == Eraser && (m_input.eraserVisibility == InputSettings::EraserAlways ||
                                                  m_input.eraserVisibility == InputSettings::EraserHover);
    if (m_highlightPosition || eraserShown) {
        updateView();
    }
}
