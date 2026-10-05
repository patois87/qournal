/*
 * Qournal
 *
 * The part of PageCanvas that manages the layers of the current page
 *
 * @license GNU GPLv2 or later
 */

#include <algorithm>

#include "PageCanvas.h"

namespace {

bool validLayer(const Page& page, int layer) { return layer >= 0 && layer < static_cast<int>(page.layers.size()); }

}  // namespace

int PageCanvas::activeLayer(int pageIndex) {
    Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    if (page.layers.empty()) {
        page.layers.emplace_back();
    }
    return page.activeLayer();
}

QVariantList PageCanvas::layers() const {
    QVariantList list;
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return list;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    for (size_t i = 0; i < page.layers.size(); ++i) {
        const Layer& layer = page.layers[i];
        // Layers without a name are numbered, as in Xournal++
        list.append(QVariantMap{{QStringLiteral("name"), layer.name.isNull() ? tr("Layer %1").arg(i + 1) : layer.name},
                                {QStringLiteral("visible"), layer.visible}});
    }
    return list;
}

int PageCanvas::currentLayer() const {
    return m_currentPage >= 0 && m_currentPage < pageCount() ?
                   m_doc.pages[static_cast<size_t>(m_currentPage)].activeLayer() :
                   -1;
}

void PageCanvas::setCurrentLayer(int layer) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, layer) || layer == page.activeLayer()) {
        return;
    }
    finishInput();
    clearSelection();
    page.currentLayer = layer;
    if (!page.layers[static_cast<size_t>(layer)].visible) {
        // What is drawn should be seen
        page.layers[static_cast<size_t>(layer)].visible = true;
        markDirty(m_currentPage, {});
    }
    emit layersChanged();
}

bool PageCanvas::backgroundVisible() const {
    return m_currentPage < 0 || m_currentPage >= pageCount() ||
           m_doc.pages[static_cast<size_t>(m_currentPage)].backgroundVisible;
}

void PageCanvas::setBackgroundVisible(bool visible) {
    if (m_currentPage < 0 || m_currentPage >= pageCount() || backgroundVisible() == visible) {
        return;
    }
    m_doc.pages[static_cast<size_t>(m_currentPage)].backgroundVisible = visible;
    markDirty(m_currentPage, {});
    emit layersChanged();
}

void PageCanvas::setLayerVisible(int layer, bool visible) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, layer) || page.layers[static_cast<size_t>(layer)].visible == visible) {
        return;
    }
    finishInput();
    if (m_selection.active && m_selection.page == m_currentPage && m_selection.layer == layer) {
        clearSelection();
    }
    page.layers[static_cast<size_t>(layer)].visible = visible;
    markDirty(m_currentPage, {});
    emit layersChanged();
}

void PageCanvas::addLayer(bool below) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    clearSelection();
    const int index = m_doc.pages[static_cast<size_t>(m_currentPage)].activeLayer() + (below ? 0 : 1);
    perform({LayerEdit{true, m_currentPage, index, Layer()}});
}

void PageCanvas::setAllLayersVisible(bool visible) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const int count = static_cast<int>(m_doc.pages[static_cast<size_t>(m_currentPage)].layers.size());
    for (int layer = 0; layer < count; ++layer) {
        setLayerVisible(layer, visible);
    }
}

void PageCanvas::deleteLayer(int layer) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    // There is always a layer to draw on
    if (!validLayer(page, layer) || page.layers.size() < 2) {
        return;
    }
    clearSelection();
    perform({LayerEdit{false, m_currentPage, layer, page.layers[static_cast<size_t>(layer)]}});
}

void PageCanvas::duplicateLayer(int layer) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, layer)) {
        return;
    }
    clearSelection();
    perform({LayerEdit{true, m_currentPage, layer + 1, page.layers[static_cast<size_t>(layer)]}});
}

void PageCanvas::renameLayer(int layer, const QString& name) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, layer) || page.layers[static_cast<size_t>(layer)].name == name) {
        return;
    }
    perform({LayerRenameEdit{m_currentPage, layer, page.layers[static_cast<size_t>(layer)].name, name}});
}

void PageCanvas::moveLayer(int from, int to) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, from) || !validLayer(page, to) || from == to) {
        return;
    }
    clearSelection();
    perform({LayerMoveEdit{m_currentPage, from, to}});
}

void PageCanvas::mergeLayerDown(int layer) {
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (!validLayer(page, layer) || layer == 0) {
        return;
    }
    clearSelection();
    // Its elements go on top of those of the layer below, then the layer is removed
    const Layer& merged = page.layers[static_cast<size_t>(layer)];
    const size_t first = page.layers[static_cast<size_t>(layer - 1)].elements.size();
    EditGroup group;
    for (size_t i = 0; i < merged.elements.size(); ++i) {
        group.push_back(ElementEdit{true, m_currentPage, layer - 1, first + i, merged.elements[i]});
    }
    group.push_back(LayerEdit{false, m_currentPage, layer, merged});
    perform(std::move(group));
}

void PageCanvas::moveSelectionToLayer(int layer) {
    if (!m_selection.active) {
        return;
    }
    finishInput();
    const int pageIndex = m_selection.page;
    const int source = m_selection.layer;
    const std::vector<size_t> indices = m_selection.indices;
    Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    if (!validLayer(page, layer) || layer == source) {
        return;
    }

    const auto& elements = page.layers[static_cast<size_t>(source)].elements;
    const size_t first = page.layers[static_cast<size_t>(layer)].elements.size();
    EditGroup group;
    std::vector<size_t> moved;
    for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
        group.push_back(ElementEdit{false, pageIndex, source, *it, elements[*it]});
    }
    for (size_t i = 0; i < indices.size(); ++i) {
        group.push_back(ElementEdit{true, pageIndex, layer, first + i, elements[indices[i]]});
        moved.push_back(first + i);
    }
    clearSelection();
    perform(std::move(group));
    // The selection goes with them
    page.currentLayer = layer;
    page.layers[static_cast<size_t>(layer)].visible = true;
    setSelection(pageIndex, layer, std::move(moved));
    emit layersChanged();
}
