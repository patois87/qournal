#include "PageCanvas.h"

#include <QClipboard>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QPointingDevice>
#include <QQuickPaintedItem>
#include <QSGImageNode>
#include <QSGRectangleNode>
#include <QSGTexture>
#include <QTabletEvent>
#include <QThread>
#include <QTouchEvent>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>

#include "CrashHandler.h"
#include "Platform.h"
#include "Renderer.h"
#include "XoppLoader.h"
#include "XoppWriter.h"

namespace {

constexpr double PAGE_GAP = 12.0;
constexpr double PAIR_GAP = 4.0;
constexpr double PRESENTATION_GAP = 20000.0;  // keeps the other pages out of sight in presentation mode
constexpr double MIN_PAGE_SIZE = 10.0;
constexpr int TILE_PRERENDER_MARGIN = 1;  // tiles around the view that are rendered in advance
constexpr int TILE_KEEP_MARGIN = 3;       // tiles further away are discarded
constexpr double MIN_SCALE = 0.1;
constexpr double MAX_SCALE = 16.0;
constexpr int INPUT_INFO_INTERVAL_MS = 250;
constexpr int LASER_FADE_STEP_MS = 50;

/// Bounds of an element in page coordinates; invalid if they are not known
QRectF elementBounds(const Element& element) {
    if (const auto* stroke = std::get_if<Stroke>(&element)) {
        return stroke->bounds;
    }
    return {};
}

bool sameBackground(const Background& a, const Background& b) {
    return a.type == b.type && a.name == b.name && a.color == b.color && a.style == b.style && a.config == b.config &&
           a.pdfPage == b.pdfPage && a.domain == b.domain && a.filename == b.filename &&
           a.pixmap.cacheKey() == b.pixmap.cacheKey();
}

QColor paperColor(const Page& page) {
    return page.background.type == Background::Type::Solid ? page.background.color : QColor(Qt::white);
}

}  // namespace

/// Everything a worker thread needs to render a tile
struct PageCanvas::TileRequest {
    QColor background;          ///< around the pages
    bool smoothEdges = true;    ///< antialiasing of strokes and shapes
    bool patternFills = false;  ///< fills as a pattern of dots, for electronic paper
    bool crispRuling = false;   ///< the ruling in black lines of whole pixels, for electronic paper
    bool pageFrames = false;    ///< a thin black line around each page, where the background is white as the paper
    struct PagePart {
        std::shared_ptr<const Page> page;
        QPointF pos;  ///< top left corner in world coordinates
    };

    QPoint tile;
    double scale = 1.0;  ///< device pixels per point
    std::vector<PagePart> pages;
#ifdef HAVE_QTPDF
    QPdfDocument* pdf = nullptr;
#endif
};

/// Paints what is in progress on top of the tiles. Only this is painted; the tiles are textures
class CanvasOverlay: public QQuickPaintedItem {
public:
    explicit CanvasOverlay(PageCanvas* canvas): QQuickPaintedItem(canvas), m_canvas(canvas) { setAntialiasing(true); }

    void paint(QPainter* painter) override { m_canvas->paintOverlays(painter); }

private:
    PageCanvas* m_canvas;
};

namespace {

/// The root of the nodes of the canvas, with the textures of the tiles that are shown.
/// A transform node (that transforms nothing): the software renderer of Qt only keeps the position and the clip
/// for such nodes, and draws what is added later to a plain node at the origin of the window, unclipped
class CanvasNode: public QSGTransformNode {
public:
    struct TileTexture {
        QSGTexture* texture = nullptr;
        quint64 version = 0;
    };

    ~CanvasNode() override {
        clear(textures);
        clear(oldTextures);
    }

    static void clear(QHash<QPoint, TileTexture>& set) {
        for (const TileTexture& entry: std::as_const(set)) {
            delete entry.texture;
        }
        set.clear();
    }

    QHash<QPoint, TileTexture> textures;
    QHash<QPoint, TileTexture> oldTextures;  ///< of the tiles of the previous zoom level
    quint64 oldGeneration = 0;
};

}  // namespace

PageCanvas::PageCanvas(QQuickItem* parent): QQuickItem(parent) {
    setFlag(ItemHasContents);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptTouchEvents(true);
    // The first child: items that are put on the canvas later (the text editor) are above it
    m_overlay = new CanvasOverlay(this);

    m_einkPenClock.start();
    m_einkPenTick.setInterval(100);
    connect(&m_einkPenTick, &QTimer::timeout, this, &PageCanvas::einkPenUpdate);
    connect(this, &PageCanvas::toolChanged, this, [this] { einkPenHold(300); });
    connect(&m_input, &InputSettings::changed, this, [this] {
        if (m_drawingStyleDrawn != drawingStyle()) {
            m_drawingStyleDrawn = drawingStyle();
            resetTiles();
            scheduleTiles();
            updateView();
        }
    });
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && m_einkPenOn) {
            Platform::EinkPen::stop();  // the screen belongs to another application now
            m_einkPenOn = false;
        }
    });

    m_settle.setSingleShot(true);
    m_settle.setInterval(200);
    connect(&m_settle, &QTimer::timeout, this, [this] {
        m_zooming = false;
        scheduleTiles();
    });

    // Leave one core to the user interface
    m_pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount() - 1));
    m_layoutSettings.padding = PAGE_GAP;
    m_layoutSettings.gap = PAGE_GAP;
    m_layoutSettings.pairGap = PAIR_GAP;

    connect(&m_autosaveTimer, &QTimer::timeout, this, &PageCanvas::autosaveNow);
    m_autosaveTimer.start(m_autosaveMinutes * 60 * 1000);
    m_laserFadeDelay.setSingleShot(true);
    connect(&m_laserFadeDelay, &QTimer::timeout, this, [this] { m_laserFade.start(LASER_FADE_STEP_MS); });
    connect(&m_laserFade, &QTimer::timeout, this, &PageCanvas::fadeLaser);
    connect(&m_edgePan, &QTimer::timeout, this, &PageCanvas::edgePanStep);

    // The colours the tools start with; every tool keeps its own
    m_toolStates[Pen].color = QColor(0x00, 0x2e, 0x99);
    m_toolStates[Highlighter].color = QColor(0xff, 0xe1, 0x6b);
    m_toolStates[LaserPen].color = QColor(0xff, 0x00, 0x00);
    m_toolStates[LaserHighlighter].color = QColor(0xff, 0x00, 0x00);
    m_toolStates[Link].color = QColor(0x33, 0x33, 0xcc);

    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, &PageCanvas::clipboardChanged);
    setAcceptHoverEvents(true);  // a spline follows the pointer between its knots
    m_inputClock.start();
    m_rateTimer.start();
    // The document of the canvas that was made last is the one that is saved if the application crashes
    CrashHandler::setEmergencySave([this] { emergencySave(); });
    watchWindow(window());  // itemChange() is not called for a parent passed to the constructor
    newDocument();
}

PageCanvas::~PageCanvas() {
    CrashHandler::setEmergencySave(nullptr);
    // A regular end: the user has saved or decided not to. After a crash the autosave is still there
    removeAutosave();
    // The worker threads use the PDF document and report to this object
    cancelPending();
    m_pool.waitForDone();
}

void PageCanvas::setUsePressure(bool use) {
    if (m_usePressure != use) {
        m_usePressure = use;
        emit usePressureChanged();
    }
}

void PageCanvas::setFingerDraws(bool draws) {
    if (m_fingerDraws != draws) {
        m_fingerDraws = draws;
        emit fingerDrawsChanged();
    }
}

void PageCanvas::openFile(const QUrl& url) {
    // Non-file URLs (e.g. content:// on Android) are passed on as they are: QFile understands them there
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    const QString name = QFileInfo(path).fileName();
    Platform::keepAccess(path);

    // By the content: a location that is not a file (content:// on Android) need not have an extension
    bool isPdf = path.endsWith(u".pdf", Qt::CaseInsensitive);
    if (!isPdf) {
        QFile file(path);
        isPdf = file.open(QIODevice::ReadOnly) && file.read(5) == "%PDF-";
    }
    if (isPdf) {
#ifdef HAVE_QTPDF
        QPdfDocument pdf;
        if (pdf.load(path) != QPdfDocument::Error::None || pdf.pageCount() == 0) {
            emit loadFailed(tr("\"%1\" could not be read as a PDF file").arg(path));
            return;
        }
        // One page per PDF page, with the PDF as background
        Document doc;
        doc.pdfDomain = QStringLiteral("absolute");
        doc.pdfFilename = path;
        doc.pdfPath = path;
        for (int i = 0; i < pdf.pageCount(); ++i) {
            Page page;
            const QSizeF size = pdf.pagePointSize(i);
            page.width = size.width();
            page.height = size.height();
            page.background.type = Background::Type::Pdf;
            page.background.pdfPage = i;
            page.layers.emplace_back();
            doc.pages.push_back(std::move(page));
        }
        setDocument(std::move(doc), name);
#else
        emit loadFailed(tr("This build cannot open PDF files: it was built without Qt PDF"));
#endif
        return;
    }

    Document doc;
    QString error;
    if (!loadXopp(path, doc, &error)) {
        emit loadFailed(error);
        return;
    }
    setDocument(std::move(doc), name, path);
    emit fileOpened(path);
}

bool PageCanvas::save() { return !m_filePath.isEmpty() && saveTo(m_filePath); }

bool PageCanvas::saveAs(const QUrl& url) {
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (url.isLocalFile() && QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".xopp");
    }
    return saveTo(path);
}

bool PageCanvas::saveTo(const QString& path) {
    finishInput();
    QString error;
    if (!saveXopp(path, m_doc, &error)) {
        emit saveFailed(error);
        return false;
    }
    Platform::keepAccess(path);
    removeAutosave();  // of the place the document had before
    m_doc.sourcePath = path;
    m_filePath = path;
    m_title = QFileInfo(path).fileName();
    setModified(false);
    emit documentChanged();
    emit fileSaved(path);
    emit layersChanged();
    return true;
}

void PageCanvas::setModified(bool modified) {
    m_autosaveDirty = modified;
    if (m_modified != modified) {
        m_modified = modified;
        emit modifiedChanged();
    }
}

void PageCanvas::newDocument() {
    Document doc = Document::createEmpty();
    applyPageTemplate(doc.pages.front());
    setDocument(std::move(doc), tr("Untitled"));
}

void PageCanvas::setDocument(Document doc, const QString& title, const QString& filePath) {
    m_doc = std::move(doc);
    m_title = title;
    m_filePath = filePath;
    setModified(false);
    m_undo.clear();
    m_redo.clear();
    cancelInput();
    // The document that is replaced was saved or given up
    removeAutosave();
    m_autosaveId = QString::number(QDateTime::currentMSecsSinceEpoch());
    m_pdfSelection = PdfSelectionState();
    m_pdfCharacters.clear();
    m_search = SearchState();
    m_textEdit = TextEditState();
    m_insertTarget = InsertTarget();
    m_selection = SelectionState();
    m_group.clear();
    m_laserStrokes.clear();
    m_geometryToolType = NoGeometryTool;
    m_geometryPage = -1;
    m_currentPage = 0;
    m_placesBack.clear();
    m_placesForward.clear();
    emit navigationChanged();

    resetTiles();
#ifdef HAVE_QTPDF
    m_pool.waitForDone();  // the worker threads may still render from the PDF
    m_pdf.close();
    if (!m_doc.pdfPath.isEmpty() && m_pdf.load(m_doc.pdfPath) != QPdfDocument::Error::None) {
        qWarning("Could not load background PDF \"%s\"", qPrintable(m_doc.pdfPath));
    }
#endif

    m_snapshots.assign(m_doc.pages.size(), nullptr);
    relayout();
    m_origin = QPointF();
    if (width() <= 0) {
        m_fitPending = true;
    } else if (m_presentationMode) {
        fitPage();
    } else {
        fitWidth();
    }
    scheduleTiles();
    updateView();
    emit documentChanged();
    emit layersChanged();
    emit currentPageChanged();
    emit pageChanged(-1);
    emit undoChanged();
    emit geometryToolChanged();
    emit selectionChanged();
    emit textEditChanged();
    emit pdfSelectionChanged();
    emit searchChanged();
}

int PageCanvas::pdfPageCount() const {
#ifdef HAVE_QTPDF
    return m_pdf.status() == QPdfDocument::Status::Ready ? m_pdf.pageCount() : 0;
#else
    return 0;
#endif
}

void PageCanvas::relayout() {
    std::vector<QSizeF> sizes;
    sizes.reserve(m_doc.pages.size());
    for (const Page& page: m_doc.pages) {
        sizes.emplace_back(page.width, page.height);
    }

    LayoutSettings settings = m_layoutSettings;
    if (m_presentationMode) {
        // A single column with the pages so far apart that only one of them can be seen
        settings = LayoutSettings();
        settings.padding = PAGE_GAP;
        settings.gap = PRESENTATION_GAP;
    }
    m_layout = PageLayout(sizes, settings);
    m_pageRects = m_layout.pageRects();
    m_worldSize = m_layout.size();
}

void PageCanvas::structureChanged(int focusPage) {
    // What is shown on top of a page does not follow it to another place
    m_laserStrokes.clear();
    if (m_selection.active) {
        m_selection = SelectionState();
        emit selectionChanged();
    }
    m_insertTarget = InsertTarget();
    // The pages have other numbers now
    if (m_pdfSelection.active) {
        m_pdfSelection = PdfSelectionState();
        emit pdfSelectionChanged();
    }
    m_search.results.clear();
    m_search.page = -1;
    m_search.index = -1;
    if (m_geometryToolType != NoGeometryTool) {
        m_geometryToolType = NoGeometryTool;
        m_geometryPage = -1;
        emit geometryToolChanged();
    }
    resetTiles();
    m_snapshots.assign(m_doc.pages.size(), nullptr);
    relayout();

    const int last = pageCount() - 1;
    setCurrentPageInternal(std::clamp(focusPage >= 0 ? focusPage : m_currentPage, 0, last));
    if (m_presentationMode) {
        fitPage();
    } else if (focusPage >= 0) {
        scrollToPage(m_currentPage);
    } else {
        clampView();
    }
    scheduleTiles();
    updateView();
    emit documentChanged();
    emit layersChanged();
    emit pageChanged(-1);
}

void PageCanvas::setLayoutSettings(const LayoutSettings& settings) {
    if (settings == m_layoutSettings) {
        return;
    }
    m_layoutSettings = settings;
    emit layoutChanged();
    if (m_presentationMode) {
        return;  // takes effect when the presentation ends
    }
    resetTiles();
    relayout();
    if (width() > 0) {
        fitWidth();
        scrollToPage(m_currentPage);
    }
    scheduleTiles();
    updateView();
}

void PageCanvas::setPairedPages(bool paired) {
    LayoutSettings settings = m_layoutSettings;
    settings.pairedPages = paired;
    setLayoutSettings(settings);
}

void PageCanvas::setLayoutColumns(int columns) {
    LayoutSettings settings = m_layoutSettings;
    settings.fixedRows = false;
    settings.columns = std::max(columns, 1);
    setLayoutSettings(settings);
}

void PageCanvas::setLayoutRows(int rows) {
    LayoutSettings settings = m_layoutSettings;
    settings.fixedRows = true;
    settings.rows = std::max(rows, 1);
    setLayoutSettings(settings);
}

void PageCanvas::setLayoutVertical(bool vertical) {
    LayoutSettings settings = m_layoutSettings;
    settings.vertical = vertical;
    setLayoutSettings(settings);
}

void PageCanvas::setLayoutRightToLeft(bool rightToLeft) {
    LayoutSettings settings = m_layoutSettings;
    settings.rightToLeft = rightToLeft;
    setLayoutSettings(settings);
}

void PageCanvas::setLayoutBottomToTop(bool bottomToTop) {
    LayoutSettings settings = m_layoutSettings;
    settings.bottomToTop = bottomToTop;
    setLayoutSettings(settings);
}

void PageCanvas::setPresentationMode(bool presentation) {
    if (m_presentationMode == presentation) {
        return;
    }
    finishInput();
    m_presentationMode = presentation;
    m_wheelPages = 0;
    resetTiles();
    relayout();
    if (width() > 0) {
        if (m_presentationMode) {
            fitPage();
        } else {
            fitWidth();
            scrollToPage(m_currentPage);
        }
    }
    scheduleTiles();
    updateView();
    emit presentationModeChanged();
}

int PageCanvas::pageAt(const QPointF& world) const {
    for (int i = 0; i < m_pageRects.size(); ++i) {
        if (m_pageRects[i].contains(world)) {
            return i;
        }
    }
    return -1;
}

void PageCanvas::setCurrentPage(int page) {
    if (m_doc.pages.empty()) {
        return;
    }
    page = std::clamp(page, 0, pageCount() - 1);
    // A jump, not a step to a neighbouring page: the place that is left can be returned to
    if (std::abs(page - m_currentPage) > 1) {
        notePlace();
    }
    setCurrentPageInternal(page);
    if (m_presentationMode) {
        fitPage();
    } else {
        scrollToPage(m_currentPage);
    }
}

void PageCanvas::setCurrentPageInternal(int page) {
    if (m_currentPage != page) {
        m_currentPage = page;
        m_wheelPages = 0;
        emit currentPageChanged();
        emit layersChanged();
    }
}

void PageCanvas::updateCurrentPage() {
    if (m_presentationMode || m_pageRects.isEmpty()) {
        return;
    }
    // The page that takes the largest part of the view; the current page wins a tie
    const QRectF visible(m_origin, size() / m_scale);
    auto visibleArea = [&](int page) {
        const QRectF part = m_pageRects[page].intersected(visible);
        return part.isValid() ? part.width() * part.height() : 0.0;
    };
    int best = std::clamp(m_currentPage, 0, pageCount() - 1);
    double bestArea = visibleArea(best);
    for (int i = 0; i < m_pageRects.size(); ++i) {
        const double area = visibleArea(i);
        if (area > bestArea) {
            best = i;
            bestArea = area;
        }
    }
    setCurrentPageInternal(best);
}

void PageCanvas::scrollToPage(int page) {
    if (page < 0 || page >= m_pageRects.size() || width() <= 0) {
        return;
    }
    const QRectF& rect = m_pageRects[page];
    const QSizeF view = size() / m_scale;
    // Top of the page at the top of the view; horizontally centered if it fits
    const double x =
            rect.width() + 2 * PAGE_GAP <= view.width() ? rect.center().x() - view.width() / 2 : rect.left() - PAGE_GAP;
    m_origin = QPointF(x, rect.top() - PAGE_GAP);
    clampView();
    viewMoved();
}

void PageCanvas::applyPageTransform(QPainter& p, int page) const {
    const QRectF& rect = m_pageRects[page];
    p.scale(m_scale, m_scale);
    p.translate(rect.topLeft() - m_origin);
    p.setClipRect(QRectF(QPointF(0, 0), rect.size()), Qt::IntersectClip);
}

QRectF PageCanvas::scrollBounds() const {
    if (m_presentationMode && m_currentPage < m_pageRects.size()) {
        return m_pageRects[m_currentPage].adjusted(-PAGE_GAP, -PAGE_GAP, PAGE_GAP, PAGE_GAP);
    }
    QRectF bounds = QRectF(QPointF(0, 0), m_worldSize) + m_space;
    if (m_unlimitedScrolling && m_scale > 0) {
        // The edges of the pages can go to the other side of the view
        const QSizeF view = size() / m_scale;
        bounds.adjust(-view.width(), -view.height(), view.width(), view.height());
    }
    return bounds;
}

void PageCanvas::clampView() {
    auto clampAxis = [](double origin, double start, double length, double view) {
        if (length <= view) {
            return start + (length - view) / 2;  // center
        }
        return std::clamp(origin, start, start + length - view);
    };
    const QRectF bounds = scrollBounds();
    m_origin.setX(clampAxis(m_origin.x(), bounds.x(), bounds.width(), width() / m_scale));
    m_origin.setY(clampAxis(m_origin.y(), bounds.y(), bounds.height(), height() / m_scale));

    // Whole device pixels, so that the tiles are copied to the screen without resampling
    const double scale = tileScale();
    m_origin = QPointF(std::round(m_origin.x() * scale), std::round(m_origin.y() * scale)) / scale;
}

void PageCanvas::updateView() {
    update();
    m_fullUpdate = true;
    m_overlay->update();
}

void PageCanvas::updateView(const QRect& rect) {
    update();
    // QQuickPaintedItem forgets a pending full update when a partial one follows it
    if (m_fullUpdate) {
        return;
    }
    if (rect.intersects(boundingRect().toAlignedRect())) {
        m_overlay->update(rect);
    }
}

void PageCanvas::viewMoved() {
    einkPenHold();
    emit scrollChanged();
    appendPageIfScrolledToEnd();
    if (m_textEdit.active) {
        emit textEditMatrixChanged();
    }
    if (m_pdfSelection.active) {
        emit pdfSelectionRectChanged();
    }
    scheduleTiles();
    // The tiles only move. The overlay is painted again if it shows something, or did
    if (hasOverlays() || m_overlayShown) {
        updateView();
    } else {
        update();
    }
}

void PageCanvas::panBy(const QPointF& viewDelta) {
    m_origin -= viewDelta / m_scale;
    clampView();
    updateCurrentPage();
    viewMoved();
}

void PageCanvas::setScale(double scale) {
    scale = std::clamp(scale, MIN_SCALE, MAX_SCALE);
    if (qFuzzyCompare(scale, m_scale)) {
        return;
    }
    // The tiles of the old zoom level are shown scaled until the new ones are rendered
    cancelPending();
    if (m_oldTiles.isEmpty()) {
        m_oldTileScale = tileScale();
        ++m_oldTileGeneration;
        for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
            m_oldTiles.insert(it.key(), it->image);
        }
    }
    m_tiles.clear();
    m_scale = scale;
    emit viewChanged();
}

void PageCanvas::zoomAt(const QPointF& viewPos, double factor) {
    const QPointF anchor = viewToWorld(viewPos);
    const double before = m_scale;
    setScale(m_scale * factor);
    if (m_scale == before) {
        return;
    }
    m_origin = anchor - viewPos / m_scale;
    clampView();
    updateCurrentPage();

    // Gestures change the zoom continuously: render when it has settled
    m_zooming = true;
    m_settle.start();
    viewMoved();
}

void PageCanvas::zoomBy(double factor) { zoomAt(QPointF(width() / 2, height() / 2), factor); }

void PageCanvas::zoomIn() { zoomBy(1.0 + std::max(m_input.zoomStep, 1.0) / 100.0); }

void PageCanvas::zoomOut() { zoomBy(1.0 / (1.0 + std::max(m_input.zoomStep, 1.0) / 100.0)); }

void PageCanvas::zoomTo(double zoom) {
    const QPointF center(width() / 2, height() / 2);
    const QPointF anchor = viewToWorld(center);
    setScale(zoom * m_displayDpi / 72.0);
    m_origin = anchor - center / m_scale;
    clampView();
    updateCurrentPage();
    viewMoved();
}

void PageCanvas::fitWidth() {
    if (width() <= 0 || m_worldSize.width() <= 0) {
        return;
    }
    m_fitPending = false;
    // All columns of a narrow layout, else the current page
    double worldWidth = m_worldSize.width();
    const bool singlePage = m_presentationMode || m_layout.columns() > 2;
    if (singlePage && m_currentPage < m_pageRects.size()) {
        worldWidth = m_pageRects[m_currentPage].width() + 2 * PAGE_GAP;
    }
    setScale(width() / worldWidth);
    if (singlePage) {
        scrollToPage(m_currentPage);
    } else {
        clampView();
        viewMoved();
    }
}

void PageCanvas::fitPage() {
    if (width() <= 0 || height() <= 0 || m_currentPage >= m_pageRects.size()) {
        return;
    }
    m_fitPending = false;
    const QRectF& rect = m_pageRects[m_currentPage];
    setScale(std::min(width() / (rect.width() + 2 * PAGE_GAP), height() / (rect.height() + 2 * PAGE_GAP)));
    m_origin = rect.center() - QPointF(width(), height()) / (2 * m_scale);
    clampView();
    viewMoved();
}

double PageCanvas::tileScale() const {
    const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    return m_scale * dpr;
}

QRect PageCanvas::tileRange(const QRectF& world) const {
    const double factor = tileScale() / TILE_SIZE;
    const QPoint first(static_cast<int>(std::floor(world.left() * factor)),
                       static_cast<int>(std::floor(world.top() * factor)));
    const QPoint last(static_cast<int>(std::floor(world.right() * factor)),
                      static_cast<int>(std::floor(world.bottom() * factor)));
    return QRect(first, last);
}

QRectF PageCanvas::tileViewRect(const QPoint& tile) const {
    const double size = TILE_SIZE / tileScale();  // in world coordinates
    return QRectF(worldToView(QPointF(tile.x() * size, tile.y() * size)), QSizeF(size, size) * m_scale);
}

std::shared_ptr<const Page> PageCanvas::snapshot(int page) {
    auto& snapshot = m_snapshots[static_cast<size_t>(page)];
    if (!snapshot) {
        auto copy = std::make_shared<Page>(m_doc.pages[static_cast<size_t>(page)]);
        if (m_selection.active && m_selection.page == page) {
            // The selection is painted on top of the tiles
            auto& elements = copy->layers[static_cast<size_t>(m_selection.layer)].elements;
            for (auto it = m_selection.indices.rbegin(); it != m_selection.indices.rend(); ++it) {
                elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(*it));
            }
        }
        if (m_textEdit.active && m_textEdit.page == page && m_textEdit.index) {
            // The editor shows the text that is being edited
            auto& elements = copy->layers[static_cast<size_t>(m_textEdit.layer)].elements;
            elements.erase(elements.begin() + static_cast<std::ptrdiff_t>(*m_textEdit.index));
        }
        snapshot = std::move(copy);
    }
    return snapshot;
}

void PageCanvas::scheduleTiles() {
    auto setRendering = [this](bool rendering) {
        if (m_rendering != rendering) {
            m_rendering = rendering;
            emit renderingChanged();
        }
    };
    if (width() <= 0 || height() <= 0) {
        setRendering(false);
        return;
    }
    if (m_zooming) {
        setRendering(true);
        return;
    }

    const QRect all = tileRange(QRectF(QPointF(0, 0), m_worldSize));
    const QRect visible = tileRange(QRectF(m_origin, size() / m_scale)) & all;
    const int pre = TILE_PRERENDER_MARGIN;
    const int keepMargin = TILE_KEEP_MARGIN;
    const QRect wanted = visible.adjusted(-pre, -pre, pre, pre) & all;
    const QRect keep = visible.adjusted(-keepMargin, -keepMargin, keepMargin, keepMargin);

    // The pages that can be on the wanted tiles
    const double tileWorld = TILE_SIZE / tileScale();
    const QRectF wantedWorld(wanted.left() * tileWorld, wanted.top() * tileWorld, wanted.width() * tileWorld,
                             wanted.height() * tileWorld);
    QList<int> candidates;
    for (int i = 0; i < m_pageRects.size(); ++i) {
        if (m_pageRects[i].intersects(wantedWorld)) {
            candidates.append(i);
        }
    }

    m_tiles.removeIf([&](const auto& tile) { return !keep.contains(tile.key()); });
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        if (wanted.contains(it.key())) {
            ++it;
        } else {
            it->cancelled->store(true);
            it = m_pending.erase(it);
        }
    }

    bool complete = true;  // every visible tile has an image
    bool upToDate = true;
    for (int y = wanted.top(); y <= wanted.bottom(); ++y) {
        for (int x = wanted.left(); x <= wanted.right(); ++x) {
            const QPoint key(x, y);
            const bool isVisible = visible.contains(key);
            const auto tile = m_tiles.constFind(key);
            if (tile != m_tiles.constEnd() && !tile->dirty) {
                continue;
            }
            if (isVisible) {
                upToDate = false;
                complete &= tile != m_tiles.constEnd();
            }
            if (!m_pending.contains(key)) {
                startTile(key, candidates, isVisible);
            }
        }
    }
    if (complete && !m_oldTiles.isEmpty()) {
        m_oldTiles.clear();
        updateView();
    }
    setRendering(!upToDate);
}

void PageCanvas::startTile(const QPoint& tile, const QList<int>& candidatePages, bool visible) {
    TileRequest request;
    request.tile = tile;
    request.background = surroundColor();
    request.pageFrames = m_einkMode;
    request.smoothEdges = smoothEdges();
    request.patternFills = patternFills();
    request.crispRuling = m_einkMode;
    request.scale = tileScale();
#ifdef HAVE_QTPDF
    request.pdf = m_pdf.status() == QPdfDocument::Status::Ready ? &m_pdf : nullptr;
#endif
    const double tileWorld = TILE_SIZE / request.scale;
    const QRectF world(tile.x() * tileWorld, tile.y() * tileWorld, tileWorld, tileWorld);
    for (int page: candidatePages) {
        if (m_pageRects[page].intersects(world)) {
            request.pages.push_back({snapshot(page), m_pageRects[page].topLeft()});
        }
    }

    PendingTile pending;
    pending.id = m_nextTileId++;
    pending.cancelled = std::make_shared<std::atomic_bool>(false);
    m_pending.insert(tile, pending);

    m_pool.start(
            [this, request = std::move(request), id = pending.id, cancelled = pending.cancelled] {
                if (cancelled->load()) {
                    return;
                }
                QElapsedTimer timer;
                timer.start();
                const QImage image = renderTile(request);
                const double ms = static_cast<double>(timer.nsecsElapsed()) / 1e6;
                // The destructor waits for this thread: the object still exists
                QMetaObject::invokeMethod(
                        this, [this, tile = request.tile, id, image, ms] { tileReady(tile, id, image, ms); },
                        Qt::QueuedConnection);
            },
            visible ? 1 : 0);
}

QImage PageCanvas::renderTile(const TileRequest& request) {
    QImage image(TILE_SIZE, TILE_SIZE, QImage::Format_RGB32);
    image.fill(request.background);

    QPainter p(&image);
    p.setRenderHints(QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing, request.smoothEdges);
    const Renderer::PatternFills fills(request.patternFills);
    const Renderer::CrispRuling ruling(request.crispRuling);
    const QPointF tileOrigin(static_cast<double>(request.tile.x()) * TILE_SIZE,
                             static_cast<double>(request.tile.y()) * TILE_SIZE);  // in device pixels of the world
    const double scale = request.scale;

    for (const TileRequest::PagePart& part: request.pages) {
        const Page& page = *part.page;
        p.save();
        p.translate(-tileOrigin);
        p.scale(scale, scale);
        p.translate(part.pos);
        p.setClipRect(QRectF(0, 0, page.width, page.height));

        bool backgroundDone = false;
#ifdef HAVE_QTPDF
        const int pdfPage = page.background.pdfPage;
        if (page.background.type == Background::Type::Pdf && request.pdf && pdfPage >= 0 &&
            pdfPage < request.pdf->pageCount()) {
            // Only the part of the PDF page that is on this tile
            const QSize pageSize = (QSizeF(page.width, page.height) * scale).toSize();
            const QPoint pageOrigin = (part.pos * scale - tileOrigin).toPoint();
            const QRect clip = QRect(-pageOrigin, QSize(TILE_SIZE, TILE_SIZE)) & QRect(QPoint(0, 0), pageSize);
            p.fillRect(QRectF(0, 0, page.width, page.height), Qt::white);
            if (!clip.isEmpty()) {
                QPdfDocumentRenderOptions options;
                // With the annotations of the PDF (comments, highlights, form fields), as Xournal++ shows them
                options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
                options.setScaledSize(pageSize);
                options.setScaledClipRect(clip);
                const QImage rendered = request.pdf->render(pdfPage, clip.size(), options);
                p.save();
                p.resetTransform();
                p.drawImage(pageOrigin + clip.topLeft(), rendered);
                p.restore();
            }
            backgroundDone = true;
        }
#endif
        if (!backgroundDone) {
            Renderer::renderBackground(p, page, nullptr);
        }
        const QRectF exposed(tileOrigin / scale - part.pos, QSizeF(TILE_SIZE, TILE_SIZE) / scale);
        Renderer::renderLayers(p, page, exposed);
        if (request.pageFrames) {
            // On the edge of the page, one pixel wide at any zoom
            p.setClipping(false);
            p.setRenderHint(QPainter::Antialiasing, false);
            QPen frame(Qt::black, 1);
            frame.setCosmetic(true);
            p.setPen(frame);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(0, 0, page.width, page.height));
        }
        p.restore();
    }
    return image;
}

void PageCanvas::tileReady(const QPoint& tile, quint64 id, const QImage& image, double ms) {
    const auto pending = m_pending.constFind(tile);
    if (pending == m_pending.constEnd() || pending->id != id) {
        return;  // cancelled
    }
    const bool outdated = pending->outdated;
    m_pending.erase(pending);

    m_tiles.insert(tile, Tile{image, outdated, ++m_tileVersion});
    m_tileMs = ms;
    update();
    scheduleTiles();
}

void PageCanvas::cancelPending() {
    for (const PendingTile& pending: std::as_const(m_pending)) {
        pending.cancelled->store(true);
    }
    m_pending.clear();
}

void PageCanvas::resetTiles() {
    cancelPending();
    m_tiles.clear();
    m_oldTiles.clear();
}

void PageCanvas::markDirty(int page, const QRectF& rect) {
    if (page < 0 || page >= m_pageRects.size()) {
        return;
    }
    m_snapshots[static_cast<size_t>(page)].reset();
    m_search.results.remove(page);  // its texts may have changed

    const QRectF& pageRect = m_pageRects[page];
    const QRect range = tileRange(rect.isValid() ? rect.translated(pageRect.topLeft()) : pageRect);
    for (auto it = m_tiles.begin(); it != m_tiles.end(); ++it) {
        if (range.contains(it.key())) {
            it->dirty = true;
        }
    }
    for (auto it = m_pending.begin(); it != m_pending.end(); ++it) {
        if (range.contains(it.key())) {
            it->outdated = true;
        }
    }
    scheduleTiles();
    emit pageChanged(page);
}

QSGNode* PageCanvas::updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData*) {
    auto* root = static_cast<CanvasNode*>(oldNode);
    if (!root) {
        root = new CanvasNode;
    }
    // The nodes are few and made anew each time; the textures, which are what costs, are kept
    while (QSGNode* child = root->firstChild()) {
        root->removeChildNode(child);
        delete child;
    }
    const QRectF viewRect = boundingRect();
    auto addRect = [&](const QRectF& rect, const QColor& color) {
        QSGRectangleNode* node = window()->createRectangleNode();
        node->setRect(rect);
        node->setColor(color);
        root->appendChildNode(node);
    };
    auto addImage = [&](const QRectF& rect, QSGTexture* texture, QSGTexture::Filtering filtering) {
        QSGImageNode* node = window()->createImageNode();
        node->setRect(rect);
        node->setSourceRect(QRectF(QPointF(0, 0), texture->textureSize()));
        node->setTexture(texture);
        node->setOwnsTexture(false);
        node->setFiltering(filtering);
        root->appendChildNode(node);
    };
    /// The texture of a tile, made anew if its image changed
    auto textureFor = [&](QHash<QPoint, CanvasNode::TileTexture>& set, const QPoint& key, const QImage& image,
                          quint64 version) {
        CanvasNode::TileTexture& entry = set[key];
        if (!entry.texture || entry.version != version) {
            delete entry.texture;
            entry.texture = window()->createTextureFromImage(image, QQuickWindow::TextureIsOpaque);
            entry.version = version;
        }
        return entry.texture;
    };
    auto dropUnused = [](QHash<QPoint, CanvasNode::TileTexture>& set, const QSet<QPoint>& used) {
        for (auto it = set.begin(); it != set.end();) {
            if (used.contains(it.key())) {
                ++it;
            } else {
                delete it->texture;
                it = set.erase(it);
            }
        }
    };

    addRect(viewRect, surroundColor());

    // Paper colour where nothing is rendered yet
    const QRectF visible(m_origin, size() / m_scale);
    for (int i = 0; i < m_pageRects.size(); ++i) {
        if (m_pageRects[i].intersects(visible)) {
            const QRectF& rect = m_pageRects[i];
            addRect(QRectF(worldToView(rect.topLeft()), rect.size() * m_scale).intersected(viewRect),
                    paperColor(m_doc.pages[static_cast<size_t>(i)]));
        }
    }

    // The tiles of the previous zoom level, scaled, until the new ones are rendered
    if (root->oldGeneration != m_oldTileGeneration) {
        CanvasNode::clear(root->oldTextures);
        root->oldGeneration = m_oldTileGeneration;
    }
    QSet<QPoint> used;
    if (!m_oldTiles.isEmpty()) {
        const double size = TILE_SIZE / m_oldTileScale;
        for (auto it = m_oldTiles.cbegin(); it != m_oldTiles.cend(); ++it) {
            const QRectF target(worldToView(QPointF(it.key().x() * size, it.key().y() * size)),
                                QSizeF(size, size) * m_scale);
            if (target.intersects(viewRect)) {
                used.insert(it.key());
                addImage(target, textureFor(root->oldTextures, it.key(), it.value(), m_oldTileGeneration),
                         QSGTexture::Linear);
            }
        }
    }
    dropUnused(root->oldTextures, used);

    used.clear();
    for (auto it = m_tiles.cbegin(); it != m_tiles.cend(); ++it) {
        const QRectF target = tileViewRect(it.key());
        if (target.intersects(viewRect)) {
            used.insert(it.key());
            // The tiles lie on whole device pixels: no resampling
            addImage(target, textureFor(root->textures, it.key(), it->image, it->version), QSGTexture::Nearest);
        }
    }
    dropUnused(root->textures, used);
    return root;
}

bool PageCanvas::hasOverlays() const {
    return m_action != Action::None || m_geometryToolType != NoGeometryTool || !m_laserStrokes.empty() ||
           m_spline.active || m_selection.active || m_pdfSelection.active || searching() || m_textEdit.active ||
           m_highlightPosition || (m_tool == Eraser && m_pointerPos.x() >= 0);
}

void PageCanvas::paintPage(QPainter* painter, int pageIndex, const QSizeF& size) {
    if (pageIndex < 0 || pageIndex >= pageCount() || size.isEmpty()) {
        return;
    }
    const Page& page = m_doc.pages[static_cast<size_t>(pageIndex)];
    const double scale = std::min(size.width() / page.width, size.height() / page.height);

    QImage pdfImage;
#ifdef HAVE_QTPDF
    const int pdfPage = page.background.pdfPage;
    if (page.background.type == Background::Type::Pdf && pdfPage >= 0 && pdfPage < pdfPageCount()) {
        const qreal dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        pdfImage = m_pdf.render(pdfPage, (QSizeF(page.width, page.height) * scale * dpr).toSize(), options);
    }
#endif

    painter->save();
    painter->setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    painter->scale(scale, scale);
    const QRectF pageRect(0, 0, page.width, page.height);
    painter->setClipRect(pageRect);
    Renderer::renderPage(*painter, page, pdfImage.isNull() ? nullptr : &pdfImage, pageRect);
    painter->restore();
}

void PageCanvas::itemChange(ItemChange change, const ItemChangeData& data) {
    if (change == ItemSceneChange) {
        watchWindow(data.window);
    } else if (change == ItemDevicePixelRatioHasChanged) {
        resetTiles();
        clampView();
        viewMoved();
    }
    QQuickItem::itemChange(change, data);
}

void PageCanvas::watchWindow(QQuickWindow* window) {
    // Qt Quick items do not receive tablet events directly: take them from the window
    if (m_window == window) {
        return;
    }
    if (m_window) {
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window) {
        m_window->installEventFilter(this);
        // The double tap of the Apple Pencil
        Platform::watchPencilTaps(m_window, [canvas = QPointer<PageCanvas>(this)](Platform::PencilTap tap) {
            if (canvas) {
                canvas->pencilTapped(static_cast<int>(tap));
            }
        });
    }
}

void PageCanvas::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    m_overlay->setSize(newGeometry.size());
    if (newGeometry.size() == oldGeometry.size()) {
        return;
    }
    if (m_presentationMode) {
        fitPage();
    } else if (m_fitPending) {
        fitWidth();
    } else {
        clampView();
        updateCurrentPage();
        viewMoved();
    }
}

PageCanvas::EditEffect PageCanvas::applyEdit(const Edit& edit, bool reverse) {
    auto& pages = m_doc.pages;

    if (const auto* e = std::get_if<ElementEdit>(&edit)) {
        auto& elements = pages[static_cast<size_t>(e->page)].layers[static_cast<size_t>(e->layer)].elements;
        const auto pos = elements.begin() + static_cast<std::ptrdiff_t>(std::min(e->index, elements.size()));
        if (e->added != reverse) {
            elements.insert(pos, e->element);
        } else if (pos != elements.end()) {
            elements.erase(pos);
        }
        return {false, false, e->page, elementBounds(e->element)};
    }

    if (const auto* e = std::get_if<PageEdit>(&edit)) {
        const auto pos = pages.begin() + std::min<std::ptrdiff_t>(e->index, static_cast<std::ptrdiff_t>(pages.size()));
        if (e->added != reverse) {
            pages.insert(pos, e->page);
            return {true, false, e->index, {}};
        }
        if (pos != pages.end()) {
            pages.erase(pos);
        }
        return {true, false, std::min(e->index, static_cast<int>(pages.size()) - 1), {}};
    }

    if (const auto* e = std::get_if<PageMoveEdit>(&edit)) {
        const int from = reverse ? e->to : e->from;
        const int to = reverse ? e->from : e->to;
        Page page = std::move(pages[static_cast<size_t>(from)]);
        pages.erase(pages.begin() + from);
        pages.insert(pages.begin() + to, std::move(page));
        return {true, false, to, {}};
    }

    if (const auto* e = std::get_if<ElementMoveEdit>(&edit)) {
        auto& layers = pages[static_cast<size_t>(e->page)].layers;
        for (const auto& [layer, index]: e->elements) {
            translateElement(layers[static_cast<size_t>(layer)].elements[index], reverse ? -e->delta : e->delta);
        }
        return {false, false, e->page, {}};
    }

    if (const auto* e = std::get_if<LayerEdit>(&edit)) {
        Page& page = pages[static_cast<size_t>(e->page)];
        if (e->added != reverse) {
            page.layers.insert(page.layers.begin() + e->index, e->layer);
            page.currentLayer = e->index;
        } else {
            page.layers.erase(page.layers.begin() + e->index);
            page.currentLayer = std::max(e->index - 1, 0);
        }
        return {false, true, e->page, {}};
    }

    if (const auto* e = std::get_if<LayerMoveEdit>(&edit)) {
        Page& page = pages[static_cast<size_t>(e->page)];
        const int from = reverse ? e->to : e->from;
        const int to = reverse ? e->from : e->to;
        Layer layer = std::move(page.layers[static_cast<size_t>(from)]);
        page.layers.erase(page.layers.begin() + from);
        page.layers.insert(page.layers.begin() + to, std::move(layer));
        page.currentLayer = to;
        return {false, true, e->page, {}};
    }

    if (const auto* e = std::get_if<LayerRenameEdit>(&edit)) {
        pages[static_cast<size_t>(e->page)].layers[static_cast<size_t>(e->index)].name = reverse ? e->before : e->after;
        return {false, true, e->page, {}};
    }

    if (const auto* e = std::get_if<ElementReplaceEdit>(&edit)) {
        auto& layers = pages[static_cast<size_t>(e->page)].layers;
        for (const auto& item: e->items) {
            layers[static_cast<size_t>(item.layer)].elements[item.index] = reverse ? item.before : item.after;
        }
        return {false, false, e->page, {}};
    }

    const auto& e = std::get<PageFormatEdit>(edit);
    const PageFormat& format = reverse ? e.before : e.after;
    Page& page = pages[static_cast<size_t>(e.page)];
    const bool resized = page.width != format.width || page.height != format.height;
    page.width = format.width;
    page.height = format.height;
    page.background = format.background;
    return {resized, false, resized ? -1 : e.page, {}};
}

void PageCanvas::applyEffects(const std::vector<EditEffect>& effects) {
    const auto structural = std::find_if(effects.rbegin(), effects.rend(), [](const auto& e) { return e.structural; });
    if (structural != effects.rend()) {
        structureChanged(structural->page);
        return;
    }
    bool layers = false;
    for (const EditEffect& effect: effects) {
        markDirty(effect.page, effect.rect);
        layers |= effect.layers;
    }
    if (layers) {
        emit layersChanged();
    }
}

void PageCanvas::perform(EditGroup group) {
    if (group.empty()) {
        return;
    }
    finishInput();
    std::vector<EditEffect> effects;
    for (const Edit& edit: group) {
        effects.push_back(applyEdit(edit, false));
    }
    pushUndo(std::move(group));
    applyEffects(effects);
}

void PageCanvas::pushUndo(EditGroup group) {
    appendPageIfWritten(group);
    m_undo.push_back(std::move(group));
    m_redo.clear();
    setModified(true);
    emit undoChanged();
}

void PageCanvas::undo() {
    if (inputActive() || m_undo.empty()) {
        return;
    }
    einkPenHold();
    clearSelection();
    EditGroup group = std::move(m_undo.back());
    m_undo.pop_back();
    std::vector<EditEffect> effects;
    for (auto it = group.rbegin(); it != group.rend(); ++it) {
        effects.push_back(applyEdit(*it, true));
    }
    m_redo.push_back(std::move(group));
    applyEffects(effects);
    setModified(true);
    emit undoChanged();
}

void PageCanvas::redo() {
    if (inputActive() || m_redo.empty()) {
        return;
    }
    einkPenHold();
    clearSelection();
    EditGroup group = std::move(m_redo.back());
    m_redo.pop_back();
    std::vector<EditEffect> effects;
    for (const Edit& edit: group) {
        effects.push_back(applyEdit(edit, false));
    }
    m_undo.push_back(std::move(group));
    applyEffects(effects);
    setModified(true);
    emit undoChanged();
}

Page PageCanvas::newPageAfter(int index) const {
    // Same paper as the page before it; a PDF or image background is not repeated
    Page page;
    if (!m_doc.pages.empty()) {
        const Page& model = m_doc.pages[static_cast<size_t>(std::clamp(index, 0, pageCount() - 1))];
        page.width = model.width;
        page.height = model.height;
        if (model.background.type == Background::Type::Solid) {
            page.background = model.background;
        }
    }
    page.layers.emplace_back();
    return page;
}

void PageCanvas::insertPage(int index) {
    index = std::clamp(index, 0, pageCount());
    perform({PageEdit{true, index, newPageAfter(index - 1)}});
}

void PageCanvas::deletePage(int page) {
    // There is always at least one page
    if (page < 0 || page >= pageCount() || pageCount() < 2) {
        return;
    }
    perform({PageEdit{false, page, m_doc.pages[static_cast<size_t>(page)]}});
}

void PageCanvas::duplicatePage(int page) {
    if (page < 0 || page >= pageCount()) {
        return;
    }
    perform({PageEdit{true, page + 1, m_doc.pages[static_cast<size_t>(page)]}});
}

void PageCanvas::movePage(int from, int to) {
    if (from < 0 || from >= pageCount() || to < 0 || to >= pageCount() || from == to) {
        return;
    }
    perform({PageMoveEdit{from, to}});
}

QSizeF PageCanvas::pageSize(int page) const {
    if (page < 0 || page >= pageCount()) {
        return {};
    }
    const Page& p = m_doc.pages[static_cast<size_t>(page)];
    return QSizeF(p.width, p.height);
}

QRectF PageCanvas::pageViewRect(int page) const {
    if (page < 0 || page >= m_pageRects.size()) {
        return {};
    }
    const QRectF& rect = m_pageRects[page];
    return QRectF(worldToView(rect.topLeft()), rect.size() * m_scale);
}

PageCanvas::PageFormat PageCanvas::pageFormat(int page) const {
    const Page& p = m_doc.pages[static_cast<size_t>(page)];
    return {p.width, p.height, p.background};
}

QVariantMap PageCanvas::pageProperties(int page) const {
    if (page < 0 || page >= pageCount()) {
        return {};
    }
    const Page& p = m_doc.pages[static_cast<size_t>(page)];
    const Background& bg = p.background;
    QVariantMap map{{QStringLiteral("width"), p.width}, {QStringLiteral("height"), p.height}};
    switch (bg.type) {
        case Background::Type::Solid:
            map.insert(QStringLiteral("type"), QStringLiteral("solid"));
            map.insert(QStringLiteral("color"), bg.color);
            map.insert(QStringLiteral("style"), bg.style.isEmpty() ? QStringLiteral("plain") : bg.style);
            map.insert(QStringLiteral("config"), bg.config);
            break;
        case Background::Type::Pdf:
            map.insert(QStringLiteral("type"), QStringLiteral("pdf"));
            map.insert(QStringLiteral("pdfPage"), bg.pdfPage + 1);
            break;
        case Background::Type::Pixmap:
            map.insert(QStringLiteral("type"), QStringLiteral("pixmap"));
            break;
    }
    return map;
}

void PageCanvas::setPageProperties(int page, const QVariantMap& properties, bool allPages) {
    if (page < 0 || page >= pageCount()) {
        return;
    }
    const QString type = properties.value(QStringLiteral("type")).toString();
    const bool solidProperties = properties.contains(QStringLiteral("color")) ||
                                 properties.contains(QStringLiteral("style")) ||
                                 properties.contains(QStringLiteral("config"));

    EditGroup group;
    for (int i = allPages ? 0 : page; i < (allPages ? pageCount() : page + 1); ++i) {
        const PageFormat before = pageFormat(i);
        PageFormat after = before;
        if (properties.contains(QStringLiteral("width"))) {
            after.width = std::max(properties.value(QStringLiteral("width")).toDouble(), MIN_PAGE_SIZE);
        }
        if (properties.contains(QStringLiteral("height"))) {
            after.height = std::max(properties.value(QStringLiteral("height")).toDouble(), MIN_PAGE_SIZE);
        }

        Background& bg = after.background;
        if (type == u"solid" || (type.isEmpty() && solidProperties && bg.type == Background::Type::Solid)) {
            if (bg.type != Background::Type::Solid) {
                bg = Background();
            }
            if (properties.contains(QStringLiteral("color"))) {
                bg.color = properties.value(QStringLiteral("color")).value<QColor>();
            }
            if (properties.contains(QStringLiteral("style"))) {
                bg.style = properties.value(QStringLiteral("style")).toString();
            }
            if (properties.contains(QStringLiteral("config"))) {
                bg.config = properties.value(QStringLiteral("config")).toString();
            }
        } else if (type == u"pdf") {
#ifdef HAVE_QTPDF
            const int pdfPage = properties.value(QStringLiteral("pdfPage")).toInt() - 1;
            if (pdfPage < 0 || pdfPage >= pdfPageCount()) {
                return;
            }
            // The page gets the size of the PDF page
            bg = Background();
            bg.type = Background::Type::Pdf;
            bg.pdfPage = pdfPage;
            const QSizeF size = m_pdf.pagePointSize(pdfPage);
            after.width = size.width();
            after.height = size.height();
#else
            return;
#endif
        }

        if (after.width != before.width || after.height != before.height ||
            !sameBackground(after.background, before.background)) {
            group.push_back(PageFormatEdit{i, before, after});
        }
    }
    perform(std::move(group));
}

QVariantList PageCanvas::pageTypes() const {
    // The predefined page types of Xournal++ (pagetemplates.ini)
    struct Type {
        QString name;
        const char* style;
        const char* config;
    };
    const Type types[] = {
            {tr("Plain"), "plain", ""},
            {tr("Ruled"), "ruled", ""},
            {tr("Ruled with vertical line"), "lined", ""},
            {tr("Ruled with vertical line on the right"), "lined", "m1=-72"},
            {tr("Staves"), "staves", ""},
            {tr("Graph"), "graph", ""},
            {tr("Dotted"), "dotted", ""},
            {tr("Isometric dotted"), "isodotted", ""},
            {tr("Isometric graph"), "isograph", ""},
            {tr("Graph with border"), "graph", "m1=40,rm=1"},
            {tr("Graph with border (bold every 5th)"), "graph", "m1=20,rm=1,bli=5,blw=1"},
    };
    QVariantList list;
    for (const Type& type: types) {
        list.append(QVariantMap{{QStringLiteral("name"), type.name},
                                {QStringLiteral("style"), QString::fromLatin1(type.style)},
                                {QStringLiteral("config"), QString::fromLatin1(type.config)}});
    }
    return list;
}

void PageCanvas::noteInput(const QString& device, double pressure, bool hasPressure) {
    ++m_eventCount;
    const qint64 elapsed = m_rateTimer.elapsed();
    if (elapsed < INPUT_INFO_INTERVAL_MS) {
        return;
    }
    // After a pause the interval says nothing about the device's report rate
    const bool continuous = elapsed < 2 * INPUT_INFO_INTERVAL_MS;
    m_eventRate = continuous ? m_eventCount * 1000.0 / static_cast<double>(elapsed) : 0;
    m_eventCount = 0;
    m_rateTimer.restart();

    const QString pressureText = hasPressure ? tr("pressure %1").arg(pressure, 0, 'f', 2) : tr("no pressure");
    const QString rateText = m_eventRate > 0 ? tr("%1 events/s").arg(qRound(m_eventRate)) : tr("– events/s");
    m_inputInfo = QStringLiteral("%1 · %2 · %3 · %4")
                          .arg(device, pressureText, rateText, tr("tile %1 ms").arg(m_tileMs, 0, 'f', 1));
    emit inputInfoChanged();
}
