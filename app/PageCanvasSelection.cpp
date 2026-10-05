/*
 * Qournal
 *
 * The part of PageCanvas that selects elements, transforms the selection and exchanges it with the clipboard
 *
 * @license GNU GPLv2 or later
 */

#include <QBuffer>
#include <QClipboard>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>

#include "PageCanvas.h"
#include "Renderer.h"
#include "XoppLoader.h"
#include "XoppWriter.h"
#include "XournalClipboard.h"

namespace {

/// The elements of a selection on the clipboard: a document in the .xopp format with one page and one layer
const QString CLIPBOARD_FORMAT = QStringLiteral("application/x-qournal-elements");

// The frame of the selection, in pixels
constexpr double HANDLE_SIZE = 8.0;
constexpr double EDGE_PADDING = HANDLE_SIZE / 2 + 2;  // around the corners
constexpr double BORDER_PADDING = HANDLE_SIZE / 2;    // around the edges
constexpr double DELETE_PADDING = 20.0;               // between the frame and the handle that deletes
constexpr double ROTATE_PADDING = 8.0;                // between the frame and the handle that rotates
constexpr double MIN_SIZE = 5.0;                      // a selection is not scaled below this
constexpr double TAP_DISTANCE = 10.0;                 // a smaller rectangle or lasso is a tap
constexpr double EDGE_PAN_MARGIN = 20.0;              // dragging the selection there scrolls the view
constexpr double EDGE_PAN_STEP = 15.0;

constexpr int MAX_SELECTION_IMAGE_PX = 4096;
constexpr double CLIPBOARD_IMAGE_SCALE = 2.0;

// Moving the selection with the arrow keys, in points
constexpr double KEY_MOVE = 3.0;
constexpr double KEY_MOVE_SMALL = 1.0;
constexpr double KEY_MOVE_LARGE = 20.0;


QRectF boundsOf(const std::vector<Element>& elements) {
    QRectF bounds;
    for (const Element& element: elements) {
        bounds |= Renderer::elementBounds(element);
    }
    return bounds;
}

/// Rotation around a point; QTransform applies what is added last first
QTransform rotationAround(const QPointF& center, double radians) {
    return QTransform().translate(center.x(), center.y()).rotateRadians(radians).translate(-center.x(), -center.y());
}

}  // namespace

void PageCanvas::setSelectAllLayers(bool all) {
    if (m_selectAllLayers != all) {
        m_selectAllLayers = all;
        emit toolChanged();
    }
}

// Selecting

void PageCanvas::selectPress(const PointerInput& input) {
    const QPointF pagePos = viewToPage(m_curPage, input.pos);
    if (m_pressTool == SelectObject) {
        // The object under the pointer, which can be dragged at once
        if (selectObjectAt(m_curPage, pagePos)) {
            m_action = Action::SelectionGesture;
            m_selection.handle = Handle::Move;
            m_selection.start = pagePos;
            m_selection.pending = QTransform();
            m_selection.pendingWidthFactor = 1.0;
            m_selection.gestureRect = m_selection.rect;
            m_selection.gestureRotation = m_selection.rotation;
        }
        return;
    }
    m_action = Action::Select;
    m_selectPoints = {pagePos};
}

void PageCanvas::selectRelease() {
    const QList<QPointF> points = std::move(m_selectPoints);
    m_selectPoints.clear();
    updateView();
    if (points.isEmpty() || m_curPage < 0 || m_curPage >= pageCount()) {
        return;
    }

    SelectionArea area = m_pressTool == SelectRegion ? SelectionArea::lasso(points) :
                                                       SelectionArea::rectangle(points.first(), points.last());
    if (area.isTap(TAP_DISTANCE / m_scale)) {
        selectObjectAt(m_curPage, points.first());
        return;
    }

    const Page& page = m_doc.pages[static_cast<size_t>(m_curPage)];
    area.extendAtPageEdges(page.width, page.height);
    // On the layer that is drawn on, or on the top-most visible layer that has something in the area
    const int active = page.activeLayer();
    for (int l = m_selectAllLayers ? static_cast<int>(page.layers.size()) - 1 : active; l >= 0; --l) {
        const bool searched = m_selectAllLayers ? page.layers[static_cast<size_t>(l)].visible : l == active;
        const auto& elements = page.layers[static_cast<size_t>(l)].elements;
        std::vector<size_t> indices;
        for (size_t i = 0; searched && i < elements.size(); ++i) {
            if (Selection::isInArea(elements[i], area)) {
                indices.push_back(i);
            }
        }
        if (!indices.empty()) {
            setSelection(m_curPage, l, std::move(indices));
            return;
        }
    }
}

bool PageCanvas::selectObjectAt(int pageIndex, const QPointF& pagePos) {
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    const int active = page.activeLayer();
    for (int l = m_selectAllLayers ? static_cast<int>(page.layers.size()) - 1 : active; l >= 0; --l) {
        if (m_selectAllLayers ? !page.layers[static_cast<size_t>(l)].visible : l != active) {
            continue;
        }
        if (const auto index = Selection::elementAt(page.layers[static_cast<size_t>(l)], pagePos)) {
            setSelection(pageIndex, l, {*index});
            return true;
        }
    }
    return false;
}

void PageCanvas::setSelection(int page, int layer, std::vector<size_t> indices) {
    clearSelection();
    if (indices.empty()) {
        return;
    }
    m_selection = SelectionState();
    m_selection.active = true;
    m_selection.page = page;
    m_selection.layer = layer;
    m_selection.indices = std::move(indices);

    const auto& elements = m_doc.pages[static_cast<size_t>(page)].layers[static_cast<size_t>(layer)].elements;
    for (size_t index: m_selection.indices) {
        m_selection.rect |= Renderer::elementBounds(elements[index]);
    }
    m_selection.gestureRect = m_selection.rect;
    renderSelectionImage();
    // The tiles are rendered without the selected elements from now on
    markDirty(page, m_selection.rect);
    updateView();
    emit selectionChanged();
}

void PageCanvas::clearSelection() {
    if (!m_selection.active) {
        return;
    }
    if (m_action == Action::SelectionGesture) {
        m_action = Action::None;
    }
    const int page = m_selection.page;
    m_selection = SelectionState();
    markDirty(page, {});
    updateView();
    emit selectionChanged();
}

void PageCanvas::selectAll() {
    finishInput();
    if (m_currentPage < 0 || m_currentPage >= pageCount()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(m_currentPage)];
    if (page.layers.empty()) {
        return;
    }
    const int layer = page.activeLayer();
    std::vector<size_t> indices(page.layers[static_cast<size_t>(layer)].elements.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        indices[i] = i;
    }
    setSelection(m_currentPage, layer, std::move(indices));
}

void PageCanvas::renderSelectionImage() {
    m_selection.image = QImage();
    if (!m_selection.active) {
        return;
    }
    const auto& elements =
            m_doc.pages[static_cast<size_t>(m_selection.page)].layers[static_cast<size_t>(m_selection.layer)].elements;
    Page selected;
    selected.layers.emplace_back();
    for (size_t index: m_selection.indices) {
        selected.layers.back().elements.push_back(elements[index]);
    }
    m_selection.imageRect = boundsOf(selected.layers.back().elements).adjusted(-2, -2, 2, 2);
    if (m_selection.imageRect.isEmpty()) {
        return;
    }

    // Large selections are rendered at a lower resolution
    m_selection.imageScale = tileScale();
    const double scale =
            std::min(m_selection.imageScale,
                     MAX_SELECTION_IMAGE_PX / std::max(m_selection.imageRect.width(), m_selection.imageRect.height()));
    m_selection.image = QImage((m_selection.imageRect.size() * scale).toSize().expandedTo(QSize(1, 1)),
                               QImage::Format_ARGB32_Premultiplied);
    m_selection.image.fill(Qt::transparent);
    QPainter p(&m_selection.image);
    p.setRenderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing, smoothEdges());
    const Renderer::PatternFills fills(patternFills());
    p.scale(scale, scale);
    p.translate(-m_selection.imageRect.topLeft());
    Renderer::renderLayers(p, selected, m_selection.imageRect);
}

// The frame and its handles

PageCanvas::Handle PageCanvas::selectionHandleAt(const QPointF& viewPos) const {
    if (!m_selection.active) {
        return Handle::None;
    }
    // In the coordinates of the frame before its rotation, in pixels
    const QRectF& rect = m_selection.rect;
    const QPointF pagePos = viewToPage(m_selection.page, viewPos);
    const QPointF local = rotationAround(rect.center(), -m_selection.rotation).map(pagePos);
    const double x = local.x() * m_scale;
    const double y = local.y() * m_scale;
    const double x1 = rect.left() * m_scale;
    const double x2 = rect.right() * m_scale;
    const double y1 = rect.top() * m_scale;
    const double y2 = rect.bottom() * m_scale;
    auto near = [](double a, double b, double padding) { return std::abs(a - b) <= padding; };
    // The handles of a small frame are smaller, so that its middle can still be grabbed to move it
    const double padX = std::min(BORDER_PADDING, (x2 - x1) / 4);
    const double padY = std::min(BORDER_PADDING, (y2 - y1) / 4);
    const double cornerX = std::min(EDGE_PADDING, (x2 - x1) / 4);
    const double cornerY = std::min(EDGE_PADDING, (y2 - y1) / 4);
    // Outside of the frame the handles have their full size
    auto nearEdge = [](double value, double edge, double inner, double outer, bool lowEdge) {
        const double d = value - edge;
        return lowEdge ? (d >= -outer && d <= inner) : (d <= outer && d >= -inner);
    };
    const bool atLeft = nearEdge(x, x1, cornerX, EDGE_PADDING, true);
    const bool atRight = nearEdge(x, x2, cornerX, EDGE_PADDING, false);
    const bool atTop = nearEdge(y, y1, cornerY, EDGE_PADDING, true);
    const bool atBottom = nearEdge(y, y2, cornerY, EDGE_PADDING, false);

    if (atLeft && atTop) {
        return Handle::TopLeft;
    }
    if (atRight && atTop) {
        return Handle::TopRight;
    }
    if (atLeft && atBottom) {
        return Handle::BottomLeft;
    }
    if (atRight && atBottom) {
        return Handle::BottomRight;
    }
    if (near(x, x1 - (DELETE_PADDING + HANDLE_SIZE), BORDER_PADDING) && near(y, y1, BORDER_PADDING)) {
        return Handle::Delete;
    }
    if (near(x, x2 + ROTATE_PADDING + HANDLE_SIZE, BORDER_PADDING) && near(y, (y1 + y2) / 2, 4 + BORDER_PADDING)) {
        return Handle::Rotate;
    }
    if (x1 <= x && x <= x2) {
        if (nearEdge(y, y1, padY, BORDER_PADDING, true)) {
            return Handle::Top;
        }
        if (nearEdge(y, y2, padY, BORDER_PADDING, false)) {
            return Handle::Bottom;
        }
    }
    if (y1 <= y && y <= y2) {
        if (nearEdge(x, x1, padX, BORDER_PADDING, true)) {
            return Handle::Left;
        }
        if (nearEdge(x, x2, padX, BORDER_PADDING, false)) {
            return Handle::Right;
        }
    }
    if (x1 <= x && x <= x2 && y1 <= y && y <= y2) {
        return Handle::Move;
    }
    return Handle::None;
}

void PageCanvas::selectionGestureMove(const PointerInput& input) {
    SelectionState& sel = m_selection;
    const QRectF& rect = sel.rect;
    const QPointF center = rect.center();
    const QPointF pagePos = viewToPage(sel.page, input.pos);
    const bool alt = input.modifiers & Qt::AltModifier;
    const Snapper snap = snapper(sel.page);
    const QTransform toLocal = rotationAround(center, -sel.rotation);
    const QTransform toPage = rotationAround(center, sel.rotation);

    sel.pendingWidthFactor = 1.0;
    sel.gestureRect = rect;
    sel.gestureRotation = sel.rotation;

    if (sel.handle == Handle::Move) {
        // The corner of the frame next to where it was grabbed snaps to the grid
        const QPointF grabbed = toLocal.map(sel.start);
        const QPointF corner = toPage.map(QPointF(grabbed.x() > center.x() ? rect.right() : rect.left(),
                                                  grabbed.y() > center.y() ? rect.bottom() : rect.top()));
        const QPointF delta = snap.snapToGrid(corner + (pagePos - sel.start), alt) - corner;
        sel.pending = QTransform::fromTranslate(delta.x(), delta.y());
        sel.gestureRect = rect.translated(delta);

        // Near the border of the view the page scrolls along
        QPointF pan;
        if (input.pos.x() < EDGE_PAN_MARGIN) {
            pan.setX(EDGE_PAN_STEP);
        } else if (input.pos.x() > width() - EDGE_PAN_MARGIN) {
            pan.setX(-EDGE_PAN_STEP);
        }
        if (input.pos.y() < EDGE_PAN_MARGIN) {
            pan.setY(EDGE_PAN_STEP);
        } else if (input.pos.y() > height() - EDGE_PAN_MARGIN) {
            pan.setY(-EDGE_PAN_STEP);
        }
        if (!pan.isNull()) {
            panBy(pan);
        }
    } else if (sel.handle == Handle::Rotate) {
        // The handle is on the right of the frame: its direction from the center is the rotation
        const double angle = snap.snapAngle(std::atan2(pagePos.y() - center.y(), pagePos.x() - center.x()), alt);
        sel.pending = rotationAround(center, angle - sel.rotation);
        sel.gestureRotation = angle;
    } else if (sel.handle != Handle::Delete) {
        // Scaling, along the sides of the frame. The side or corner opposite of the handle stays where it is
        int xSide = 0;
        int ySide = 0;
        switch (sel.handle) {
            case Handle::TopLeft:
                xSide = -1;
                ySide = -1;
                break;
            case Handle::TopRight:
                xSide = 1;
                ySide = -1;
                break;
            case Handle::BottomLeft:
                xSide = -1;
                ySide = 1;
                break;
            case Handle::BottomRight:
                xSide = 1;
                ySide = 1;
                break;
            case Handle::Top:
                ySide = -1;
                break;
            case Handle::Bottom:
                ySide = 1;
                break;
            case Handle::Left:
                xSide = -1;
                break;
            default:
                xSide = 1;
                break;
        }
        const double w = std::max(rect.width(), 1e-6);
        const double h = std::max(rect.height(), 1e-6);
        const QPointF local = toLocal.map(pagePos);
        const QPointF handlePos(xSide > 0 ? rect.right() :
                                xSide < 0 ? rect.left() :
                                            center.x(),
                                ySide > 0 ? rect.bottom() :
                                ySide < 0 ? rect.top() :
                                            center.y());
        const QPointF anchor(xSide > 0 ? rect.left() :
                             xSide < 0 ? rect.right() :
                                         center.x(),
                             ySide > 0 ? rect.top() :
                             ySide < 0 ? rect.bottom() :
                                         center.y());

        // The movement along the diagonal (or across the side) gives the factor; corners keep the aspect ratio
        const double diag = std::hypot(xSide * w, ySide * h);
        const QPointF direction(xSide * w / diag, ySide * h / diag);
        const double minFactor = (MIN_SIZE / m_scale) / std::min(w, h);
        double f = (QPointF::dotProduct(local - handlePos, direction) + diag) / diag;
        // Dragged past the opposite side, the selection is mirrored. It never gets smaller than the handles need
        const double smallest = std::min(minFactor, 1.0);
        if (std::abs(f) < smallest) {
            f = f < 0 ? -smallest : smallest;
        }

        if (sel.rotation == 0 && f > 0) {
            // The moved edges snap to the grid
            const double edgeX = anchor.x() + xSide * w * f;
            const double edgeY = anchor.y() + ySide * h * f;
            const double fx = xSide ? (snap.snapHorizontally(edgeX, alt) - anchor.x()) / (xSide * w) : f;
            const double fy = ySide ? (snap.snapVertically(edgeY, alt) - anchor.y()) / (ySide * h) : f;
            // A corner follows the edge that is closer to a line of the grid
            double snapped = xSide ? fx : fy;
            if (xSide && ySide && (fx == f || (fy != f && std::abs(fy - f) * h < std::abs(fx - f) * w))) {
                snapped = fy;
            }
            if (snapped > minFactor) {
                f = snapped;
            }
        }

        const double fx = xSide ? f : 1.0;
        const double fy = ySide ? f : 1.0;
        const QTransform scaleLocal =
                QTransform().translate(anchor.x(), anchor.y()).scale(fx, fy).translate(-anchor.x(), -anchor.y());
        sel.pending = toLocal * scaleLocal * toPage;
        sel.pendingWidthFactor = std::sqrt(std::abs(fx * fy));
        // The scaled frame turns around its own center
        QRectF scaled = scaleLocal.mapRect(rect);
        scaled.moveCenter(toPage.map(scaled.center()));
        sel.gestureRect = scaled;
    }
    updateView();
}

void PageCanvas::selectionGestureEnd(const PointerInput* input) {
    SelectionState& sel = m_selection;
    const Handle handle = sel.handle;
    sel.handle = Handle::None;
    if (!sel.active) {
        return;
    }
    if (handle == Handle::Delete) {
        deleteSelection();
        return;
    }
    const QTransform pending = sel.pending;
    const double widthFactor = sel.pendingWidthFactor;
    const QRectF rect = sel.gestureRect;
    const double rotation = sel.gestureRotation;
    sel.pending = QTransform();
    if (pending.isIdentity()) {
        updateView();
        return;
    }

    // Dropped on another page: the elements move there
    const int target = input && handle == Handle::Move ? pageAt(viewToWorld(input->pos)) : -1;
    if (target >= 0 && target != sel.page) {
        const int source = sel.page;
        const int sourceLayer = sel.layer;
        const std::vector<size_t> indices = sel.indices;
        const QPointF offset = m_pageRects[source].topLeft() - m_pageRects[target].topLeft();
        const QTransform toTarget = pending * QTransform::fromTranslate(offset.x(), offset.y());

        const int targetLayer = activeLayer(target);
        const Page& targetPage = m_doc.pages[static_cast<size_t>(target)];
        const size_t first = targetPage.layers[static_cast<size_t>(targetLayer)].elements.size();
        const auto& elements =
                m_doc.pages[static_cast<size_t>(source)].layers[static_cast<size_t>(sourceLayer)].elements;

        EditGroup group;
        std::vector<size_t> added;
        for (auto it = indices.rbegin(); it != indices.rend(); ++it) {
            group.push_back(ElementEdit{false, source, sourceLayer, *it, elements[*it]});
        }
        for (size_t i = 0; i < indices.size(); ++i) {
            Element element = elements[indices[i]];
            Selection::transform(element, toTarget, widthFactor);
            group.push_back(ElementEdit{true, target, targetLayer, first + i, std::move(element)});
            added.push_back(first + i);
        }
        clearSelection();
        perform(std::move(group));
        setSelection(target, targetLayer, std::move(added));
        m_selection.rect = rect.translated(offset);
        m_selection.gestureRect = m_selection.rect;
        m_selection.rotation = rotation;
        setCurrentPageInternal(target);
        updateView();
        return;
    }

    transformSelection(pending, widthFactor, rect, rotation);
}

bool PageCanvas::selectionKey(QKeyEvent* event) {
    QPointF direction;
    switch (event->key()) {
        case Qt::Key_Escape:
            clearSelection();
            break;
        case Qt::Key_Delete:
        case Qt::Key_Backspace:
            deleteSelection();
            break;
        case Qt::Key_Left:
            direction = QPointF(-1, 0);
            break;
        case Qt::Key_Right:
            direction = QPointF(1, 0);
            break;
        case Qt::Key_Up:
            direction = QPointF(0, -1);
            break;
        case Qt::Key_Down:
            direction = QPointF(0, 1);
            break;
        default:
            return false;
    }
    if (!direction.isNull() && !inputActive()) {
        const double amount = (event->modifiers() & Qt::AltModifier)   ? KEY_MOVE_SMALL :
                              (event->modifiers() & Qt::ShiftModifier) ? KEY_MOVE_LARGE :
                                                                         KEY_MOVE;
        const QPointF delta = amount * direction;
        transformSelection(QTransform::fromTranslate(delta.x(), delta.y()), 1.0, m_selection.rect.translated(delta),
                           m_selection.rotation);
    }
    event->accept();
    return true;
}

// Changing the selection

void PageCanvas::modifySelection(const std::function<void(Element&)>& modify) {
    if (!m_selection.active) {
        return;
    }
    finishInput();
    const auto& elements =
            m_doc.pages[static_cast<size_t>(m_selection.page)].layers[static_cast<size_t>(m_selection.layer)].elements;
    ElementReplaceEdit edit{m_selection.page, {}};
    for (size_t index: m_selection.indices) {
        Element after = elements[index];
        modify(after);
        edit.items.push_back({m_selection.layer, index, elements[index], std::move(after)});
    }
    perform({std::move(edit)});

    if (m_selection.rotation == 0) {
        // The size of the elements may have changed
        m_selection.rect = QRectF();
        for (size_t index: m_selection.indices) {
            m_selection.rect |= Renderer::elementBounds(elements[index]);
        }
        m_selection.gestureRect = m_selection.rect;
    }
    renderSelectionImage();
    updateView();
}

void PageCanvas::transformSelection(const QTransform& transformation, double widthFactor, const QRectF& rect,
                                    double rotation) {
    if (!m_selection.active) {
        return;
    }
    m_selection.pending = QTransform();
    const auto& elements =
            m_doc.pages[static_cast<size_t>(m_selection.page)].layers[static_cast<size_t>(m_selection.layer)].elements;
    ElementReplaceEdit edit{m_selection.page, {}};
    for (size_t index: m_selection.indices) {
        Element after = elements[index];
        Selection::transform(after, transformation, widthFactor);
        edit.items.push_back({m_selection.layer, index, elements[index], std::move(after)});
    }
    perform({std::move(edit)});

    m_selection.rect = rect;
    m_selection.gestureRect = rect;
    m_selection.rotation = rotation;
    m_selection.gestureRotation = rotation;
    renderSelectionImage();
    updateView();
}

void PageCanvas::deleteSelection() {
    if (!m_selection.active) {
        return;
    }
    finishInput();
    const SelectionState selection = m_selection;
    const auto& elements =
            m_doc.pages[static_cast<size_t>(selection.page)].layers[static_cast<size_t>(selection.layer)].elements;
    EditGroup group;
    for (auto it = selection.indices.rbegin(); it != selection.indices.rend(); ++it) {
        group.push_back(ElementEdit{false, selection.page, selection.layer, *it, elements[*it]});
    }
    clearSelection();
    perform(std::move(group));
}

void PageCanvas::arrangeSelection(OrderChange change) {
    if (!m_selection.active) {
        return;
    }
    finishInput();
    const auto& elements =
            m_doc.pages[static_cast<size_t>(m_selection.page)].layers[static_cast<size_t>(m_selection.layer)].elements;
    const std::vector<size_t> before = m_selection.indices;
    const std::vector<size_t> after =
            Selection::arrange(before, elements.size(), static_cast<Selection::OrderChange>(change));
    if (after == before) {
        return;
    }

    // Taken out from the top down, put back from the bottom up
    EditGroup group;
    std::vector<Element> moved;
    for (size_t index: before) {
        moved.push_back(elements[index]);
    }
    for (size_t i = before.size(); i-- > 0;) {
        group.push_back(ElementEdit{false, m_selection.page, m_selection.layer, before[i], moved[i]});
    }
    for (size_t i = 0; i < after.size(); ++i) {
        group.push_back(ElementEdit{true, m_selection.page, m_selection.layer, after[i], moved[i]});
    }
    m_selection.indices = after;
    perform(std::move(group));
    updateView();
}

// Clipboard

bool PageCanvas::canPaste() const {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    return mime && (mime->hasFormat(CLIPBOARD_FORMAT) || mime->hasFormat(XournalClipboard::mimeType()) ||
                    mime->hasImage() || mime->hasText());
}

void PageCanvas::copy() {
    if (!m_selection.active) {
        return;
    }
    finishInput();
    const Page& page = m_doc.pages[static_cast<size_t>(m_selection.page)];
    const auto& elements = page.layers[static_cast<size_t>(m_selection.layer)].elements;

    Document doc;
    doc.pages.emplace_back();
    doc.pages.back().width = page.width;
    doc.pages.back().height = page.height;
    doc.pages.back().layers.emplace_back();
    auto& copied = doc.pages.back().layers.back().elements;
    QStringList texts;
    for (size_t index: m_selection.indices) {
        copied.push_back(elements[index]);
        if (const auto* text = std::get_if<TextElement>(&elements[index])) {
            texts.append(text->text);
        }
    }

    auto* mime = new QMimeData;
    mime->setData(CLIPBOARD_FORMAT, writeXoppXml(doc));
    // For Xournal++ itself, which pastes it as a selection
    mime->setData(XournalClipboard::mimeType(), XournalClipboard::write(copied));

    // For other applications: a picture, and the text if there is any
    const QRectF bounds = boundsOf(copied).adjusted(-1, -1, 1, 1);
    const double scale =
            std::min(CLIPBOARD_IMAGE_SCALE, MAX_SELECTION_IMAGE_PX / std::max({bounds.width(), bounds.height(), 1.0}));
    QImage image((bounds.size() * scale).toSize().expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter p(&image);
        p.setRenderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        p.setRenderHint(QPainter::Antialiasing, smoothEdges());
        p.scale(scale, scale);
        p.translate(-bounds.topLeft());
        Renderer::renderLayers(p, doc.pages.back(), bounds);
    }
    mime->setImageData(image);
    if (!texts.isEmpty()) {
        mime->setText(texts.join(u'\n'));
    }
    QGuiApplication::clipboard()->setMimeData(mime);
}

void PageCanvas::cut() {
    copy();
    deleteSelection();
}

void PageCanvas::paste() {
    const QMimeData* mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || m_doc.pages.empty()) {
        return;
    }
    finishInput();
    const Page& page = m_doc.pages[static_cast<size_t>(std::clamp(m_currentPage, 0, pageCount() - 1))];

    std::vector<Element> elements;
    if (mime->hasFormat(CLIPBOARD_FORMAT)) {
        Document doc;
        if (loadXoppData(mime->data(CLIPBOARD_FORMAT), QString(), doc, nullptr)) {
            for (Layer& layer: doc.pages.front().layers) {
                for (Element& element: layer.elements) {
                    elements.push_back(std::move(element));
                }
            }
        }
    } else if (mime->hasFormat(XournalClipboard::mimeType())) {
        QString error;
        if (!XournalClipboard::read(mime->data(XournalClipboard::mimeType()), elements, &error)) {
            emit pasteFailed(error);
            return;
        }
    } else if (mime->hasImage()) {
        const QImage image = qvariant_cast<QImage>(mime->imageData());
        if (!image.isNull()) {
            elements.emplace_back(imageElement(image, QByteArray(), page));
        }
    } else if (mime->hasText() && !mime->text().isEmpty()) {
        TextElement element;
        element.text = mime->text();
        element.font = m_textFont;
        element.size = m_textSize;
        element.align = m_textAlign;
        element.color = m_toolStates[Text].color;
        elements.emplace_back(std::move(element));
    }
    pasteElements(std::move(elements));
}

/// Adds elements to a page (by default the current one, in the middle of what is visible of it) and selects them
void PageCanvas::pasteElements(std::vector<Element> elements, int targetPage, std::optional<QPointF> pagePos) {
    if (elements.empty() || m_doc.pages.empty()) {
        return;
    }
    const int pageIndex = std::clamp(targetPage >= 0 ? targetPage : m_currentPage, 0, pageCount() - 1);
    const int layer = activeLayer(pageIndex);
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    const size_t first = page.layers[static_cast<size_t>(layer)].elements.size();

    const QRectF visible =
            QRectF(viewToPage(pageIndex, QPointF(0, 0)), size() / m_scale) & QRectF(0, 0, page.width, page.height);
    const QPointF middle = visible.isEmpty() ? QPointF(page.width / 2, page.height / 2) : visible.center();
    const QPointF target = pagePos ? *pagePos : snapper(pageIndex).snapToGrid(middle);
    const QPointF delta = target - boundsOf(elements).center();

    EditGroup group;
    std::vector<size_t> indices;
    for (size_t i = 0; i < elements.size(); ++i) {
        translateElement(elements[i], delta);
        group.push_back(ElementEdit{true, pageIndex, layer, first + i, std::move(elements[i])});
        indices.push_back(first + i);
    }
    clearSelection();
    perform(std::move(group));
    setSelection(pageIndex, layer, std::move(indices));
}

// Painting

void PageCanvas::paintSelection(QPainter* painter) {
    // The rectangle or lasso that is being drawn
    if (m_action == Action::Select && m_selectPoints.size() >= 2 && m_curPage >= 0 && m_curPage < m_pageRects.size()) {
        painter->save();
        applyPageTransform(*painter, m_curPage);
        QPainterPath path;
        if (m_pressTool == SelectRegion) {
            path.addPolygon(QPolygonF(m_selectPoints));
            path.closeSubpath();
        } else {
            path.addRect(QRectF(m_selectPoints.first(), m_selectPoints.last()).normalized());
        }
        QColor fill = m_selectionColor;
        fill.setAlphaF(0.15);
        painter->setPen(QPen(m_selectionColor, 0));
        painter->setBrush(fill);
        painter->drawPath(path);
        painter->restore();
    }

    if (!m_selection.active || m_selection.page >= m_pageRects.size()) {
        return;
    }
    const SelectionState& sel = m_selection;
    const bool gesture = m_action == Action::SelectionGesture;

    // The elements, as the gesture in progress moves them
    if (sel.imageScale != tileScale()) {
        renderSelectionImage();
    }
    if (!sel.image.isNull()) {
        painter->save();
        applyPageTransform(*painter, sel.page);
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        if (gesture) {
            painter->setTransform(sel.pending, true);
        }
        painter->drawImage(sel.imageRect, sel.image);
        painter->restore();
    }

    // The frame. It is not cut at the border of the page: its handles can be next to it
    const QRectF rect = gesture ? sel.gestureRect : sel.rect;
    const double rotation = gesture ? sel.gestureRotation : sel.rotation;
    const double px = 1 / m_scale;
    painter->save();
    painter->scale(m_scale, m_scale);
    painter->translate(m_pageRects[sel.page].topLeft() - m_origin);
    painter->translate(rect.center());
    painter->rotate(rotation * 180 / M_PI);
    const QRectF frame(-rect.width() / 2, -rect.height() / 2, rect.width(), rect.height());

    QColor fill = m_selectionColor;
    fill.setAlphaF(0.08);
    painter->setPen(QPen(m_selectionColor, 0, Qt::DashLine));
    painter->setBrush(fill);
    painter->drawRect(frame);

    painter->setPen(QPen(m_selectionColor, 0));
    painter->setBrush(Qt::white);
    const double half = HANDLE_SIZE / 2 * px;
    for (const QPointF& corner: {frame.topLeft(), frame.topRight(), frame.bottomLeft(), frame.bottomRight()}) {
        painter->drawRect(QRectF(corner - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    }
    // The handle that rotates, on the right
    painter->drawEllipse(QPointF(frame.right() + (ROTATE_PADDING + HANDLE_SIZE) * px, 0), half, half);
    // The handle that deletes, on the top left
    const QPointF deleteCenter(frame.left() - (DELETE_PADDING + HANDLE_SIZE) * px, frame.top());
    painter->drawRect(QRectF(deleteCenter - QPointF(half, half), QSizeF(2 * half, 2 * half)));
    painter->drawLine(deleteCenter - QPointF(half, half), deleteCenter + QPointF(half, half));
    painter->drawLine(deleteCenter + QPointF(half, -half), deleteCenter + QPointF(-half, half));
    painter->restore();
}
