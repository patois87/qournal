/*
 * Qournal
 *
 * The part of PageCanvas that scripts (plugins) use: adding elements, the selection, the scroll position and
 * details of the tools
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>

#include "PageCanvas.h"

std::vector<PageCanvas::ElementRef> PageCanvas::addElements(const std::vector<Element>& elements, bool grouped) {
    std::vector<ElementRef> refs;
    if (elements.empty() || m_currentPage < 0 || m_currentPage >= pageCount()) {
        return refs;
    }
    finishInput();
    clearSelection();
    const int layer = activeLayer(m_currentPage);
    size_t index = m_doc.pages[static_cast<size_t>(m_currentPage)].layers[static_cast<size_t>(layer)].elements.size();
    EditGroup group;
    for (const Element& element: elements) {
        refs.push_back({m_currentPage, layer, index});
        group.push_back(ElementEdit{true, m_currentPage, layer, index++, element});
        if (!grouped) {
            perform(std::move(group));
            group.clear();
        }
    }
    if (!group.empty()) {
        perform(std::move(group));
    }
    return refs;
}

bool PageCanvas::selectedElements(int& page, int& layer, std::vector<size_t>& indices) const {
    if (!m_selection.active) {
        return false;
    }
    page = m_selection.page;
    layer = m_selection.layer;
    indices = m_selection.indices;
    return true;
}

void PageCanvas::addToSelection(const std::vector<size_t>& indices) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    finishInput();
    const int layer = activeLayer(m_currentPage);
    const size_t count =
            m_doc.pages[static_cast<size_t>(m_currentPage)].layers[static_cast<size_t>(layer)].elements.size();
    std::vector<size_t> all;
    if (m_selection.active && m_selection.page == m_currentPage && m_selection.layer == layer) {
        all = m_selection.indices;
    }
    for (size_t index: indices) {
        if (index < count) {
            all.push_back(index);
        }
    }
    std::sort(all.begin(), all.end());
    all.erase(std::unique(all.begin(), all.end()), all.end());
    if (!all.empty()) {
        setSelection(m_currentPage, layer, std::move(all));
    }
}

QPointF PageCanvas::scrollPosition() const { return m_origin * m_scale; }

void PageCanvas::scrollTo(const QPointF& position) { panBy((m_origin - position / m_scale) * m_scale); }

QString PageCanvas::pdfPageLabel(int pdfPage) const {
#ifdef HAVE_QTPDF
    if (pdfPage >= 0 && pdfPage < m_pdf.pageCount()) {
        return const_cast<QPdfDocument&>(m_pdf).pageLabel(pdfPage);  // not const in Qt, but it only reads
    }
#else
    Q_UNUSED(pdfPage)
#endif
    return {};
}

void PageCanvas::setBackgroundName(const QString& name) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const PageFormat before = pageFormat(m_currentPage);
    if (before.background.name == name) {
        return;
    }
    PageFormat after = before;
    after.background.name = name;
    perform({PageFormatEdit{m_currentPage, before, after}});
}

QVariantMap PageCanvas::toolInfo(Tool tool) const {
    const ToolState& state = m_toolStates[static_cast<size_t>(tool)];
    return {{QStringLiteral("color"), state.color},
            {QStringLiteral("size"), static_cast<int>(state.size)},
            {QStringLiteral("thickness"), thickness(tool)},
            {QStringLiteral("drawingType"), static_cast<int>(state.drawingType)},
            {QStringLiteral("fill"), state.fill},
            {QStringLiteral("fillAlpha"), state.fillAlpha},
            {QStringLiteral("lineStyle"), state.lineStyle}};
}

void PageCanvas::setToolInfo(Tool tool, const QVariantMap& values) {
    ToolState& state = m_toolStates[static_cast<size_t>(tool)];
    if (const QColor color = values.value(QStringLiteral("color")).value<QColor>(); color.isValid()) {
        state.color = color;
    }
    if (values.contains(QStringLiteral("size"))) {
        state.size = static_cast<ToolSize>(std::clamp(values.value(QStringLiteral("size")).toInt(), 0, 4));
    }
    if (values.contains(QStringLiteral("drawingType"))) {
        const int type = values.value(QStringLiteral("drawingType")).toInt();
        state.drawingType = static_cast<DrawingType>(std::clamp(type, 0, static_cast<int>(ShapeRecognizer)));
    }
    if (values.contains(QStringLiteral("fill"))) {
        state.fill = values.value(QStringLiteral("fill")).toBool();
    }
    if (values.contains(QStringLiteral("fillAlpha"))) {
        state.fillAlpha = std::clamp(values.value(QStringLiteral("fillAlpha")).toInt(), 1, 255);
    }
    if (values.contains(QStringLiteral("lineStyle"))) {
        state.lineStyle = values.value(QStringLiteral("lineStyle")).toString();
    }
    emit toolChanged();
}

void PageCanvas::setTextFontDescription(const QString& description) {
    if (!description.isEmpty() && m_textFont != description) {
        m_textFont = description;
        emit textStyleChanged();
    }
}

void PageCanvas::selectPage(int page) {
    if (pageCount() > 0) {
        setCurrentPageInternal(std::clamp(page, 0, pageCount() - 1));
    }
}

void PageCanvas::refresh() {
    m_snapshots.assign(m_doc.pages.size(), nullptr);
    resetTiles();
    scheduleTiles();
    updateView();
}
