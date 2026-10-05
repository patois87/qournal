/*
 * Qournal
 *
 * Tests for the canvas: page operations, view layout, navigation and the rendering in tiles.
 * They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QPainter>
#include <QPdfWriter>
#include <QPointingDevice>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTabletEvent>
#include <QTemporaryDir>
#include <QTest>
#include <QWheelEvent>
#include <memory>

#include "CanvasFixture.h"
#include "Document.h"
#include "PageCanvas.h"
#include "PagePreview.h"
#include "Platform.h"
#include "Renderer.h"
#include "XoppLoader.h"

namespace {

/// Notes the mouse events a window gets
/// Stands for the overlay of Qt Quick Controls, in which menus and dialogs are: the canvas knows it by its name
class QQuickOverlay: public QQuickItem {
    Q_OBJECT
public:
    using QQuickItem::QQuickItem;
};

/// Stands for a menu: it takes the clicks, so that they do not reach what is below it
class ClickTaker: public QQuickItem {
public:
    explicit ClickTaker(QQuickItem* parent): QQuickItem(parent) { setAcceptedMouseButtons(Qt::AllButtons); }

protected:
    void mousePressEvent(QMouseEvent* event) override { event->accept(); }
    void mouseMoveEvent(QMouseEvent* event) override { event->accept(); }
    void mouseReleaseEvent(QMouseEvent* event) override { event->accept(); }
};

class MouseRecorder: public QObject {
public:
    QList<QEvent::Type> types;
    QPointF lastPosition;

protected:
    bool eventFilter(QObject*, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease ||
            event->type() == QEvent::MouseMove) {
            types.append(event->type());
            lastPosition = static_cast<QMouseEvent*>(event)->position();
        }
        return false;
    }
};

/// A PDF with a black rectangle on each page; the units are points
void writePdf(const QString& path, const QList<QRectF>& rects) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p(&writer);
    for (qsizetype i = 0; i < rects.size(); ++i) {
        if (i > 0) {
            writer.newPage();
        }
        p.fillRect(rects[i], Qt::black);
    }
}

}  // namespace

class TestCanvas: public QObject {
    Q_OBJECT

private slots:
    /// The double tap of the Apple Pencil, as iOS reports it
    void pencilDoubleTap() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::Highlighter);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::SwitchEraser));
        QCOMPARE(c->tool(), PageCanvas::Eraser);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::SwitchEraser));
        QCOMPARE(c->tool(), PageCanvas::Highlighter);
        c->setTool(PageCanvas::Text);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::SwitchPrevious));
        QCOMPARE(c->tool(), PageCanvas::Highlighter);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::SwitchPrevious));
        QCOMPARE(c->tool(), PageCanvas::Text);
        QSignalSpy toolbox(c, &PageCanvas::floatingToolboxRequested);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::ShowColorPalette));
        QCOMPARE(toolbox.count(), 1);
        c->pencilTapped(static_cast<int>(Platform::PencilTap::Ignore));
        QCOMPARE(c->tool(), PageCanvas::Text);
    }

    void newDocument() {
        Fixture f;
        QCOMPARE(f.canvas->pageCount(), 1);
        QCOMPARE(f.canvas->currentPage(), 0);
        QVERIFY(!f.canvas->modified());

        // The page fills the width of the view
        const QRectF rect = f.canvas->pageViewRect(0);
        QVERIFY(rect.left() > 0 && rect.left() < 30);
        QCOMPARE(rect.left() + rect.right(), 800.0);

        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(400, 300), Qt::white);
        COMPARE_COLOR(image.pixelColor(2, 300), CANVAS_COLOR);

        // On electronic paper it is white around the pages, which have a thin black frame
        f.canvas->setEinkMode(true);
        const QImage paper = f.grab();
        COMPARE_COLOR(paper.pixelColor(400, 300), Qt::white);
        COMPARE_COLOR(paper.pixelColor(2, 300), Qt::white);
        bool frame = false;
        for (int x = static_cast<int>(rect.left()) - 2; x <= static_cast<int>(rect.left()) + 2; ++x) {
            frame = frame || paper.pixelColor(x, 300).lightness() < 60;
        }
        QVERIFY(frame);
        // The pen starts black there instead of blue, and strokes have hard edges unless the settings say otherwise
        QCOMPARE(f.canvas->tool(), PageCanvas::Pen);
        QCOMPARE(f.canvas->color(), QColor(Qt::black));
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 180)});
        const auto grays = [&f] {
            QTest::qWait(300);  // the tiles are rendered again a moment after the setting changed
            const QImage image = f.grab();
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    const QColor c = image.pixelColor(x, y);
                    count += c.lightness() > 40 && c.lightness() < 215 && c.saturation() < 30;
                }
            }
            return count;
        };
        const int hard = grays();
        f.canvas->input()->setProperty("einkSmoothing", true);
        QVERIFY2(grays() > hard + 50, qPrintable(QStringLiteral("%1 hard").arg(hard)));
        f.canvas->input()->setProperty("einkSmoothing", false);
        f.canvas->undo();
        // A filled shape is a pattern of dots of the colour there, not a tone, which such a screen shows with
        // noise; the document keeps the fill as it is
        f.canvas->setDrawingType(PageCanvas::Rectangle);
        f.canvas->setFill(true);
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 220)});
        const int dotted = grays();
        const QImage dots = f.grab();
        int black = 0;
        int white = 0;
        const QPoint from = f.onPage(0, QPointF(150, 130)).toPoint();
        for (int y = 0; y < 32; ++y) {
            for (int x = 0; x < 32; ++x) {
                const int lightness = dots.pixelColor(from + QPoint(x, y)).lightness();
                black += lightness < 40;
                white += lightness > 215;
            }
        }
        QCOMPARE(black + white, 32 * 32);
        QVERIFY2(black > 400 && white > 400, qPrintable(QStringLiteral("%1 black, %2 white").arg(black).arg(white)));
        f.canvas->input()->setProperty("einkPatternFills", false);
        QVERIFY2(grays() > dotted + 1000, qPrintable(QStringLiteral("%1 dotted").arg(dotted)));
        f.canvas->input()->setProperty("einkPatternFills", true);
        QCOMPARE(std::get<Stroke>(f.canvas->document().pages[0].layers[0].elements.back()).fill, 128);
        f.canvas->undo();
        f.canvas->setFill(false);
        f.canvas->setDrawingType(PageCanvas::Freehand);
        // The lines of a ruling are thinner than a pixel at this zoom. On such a screen each one is a whole
        // pixel of black, and none is missing
        f.canvas->setPageProperties(0, {{QStringLiteral("style"), QStringLiteral("graph")}});
        QTest::qWait(300);
        const QImage squares = f.grab();
        const QPoint rowStart = f.onPage(0, QPointF(4, 5.5 * 14.17)).toPoint();
        const int rowEnd = f.onPage(0, QPointF(591, 5.5 * 14.17)).toPoint().x();
        int lines = 0;
        int run = 0;
        for (int x = rowStart.x(); x <= rowEnd; ++x) {
            const int lightness = squares.pixelColor(x, rowStart.y()).lightness();
            QVERIFY2(lightness < 40 || lightness > 215, qPrintable(QStringLiteral("tone at %1").arg(x)));
            if (lightness < 40) {
                ++run;
            } else if (run > 0) {
                QCOMPARE(run, 1);
                ++lines;
                run = 0;
            }
        }
        QCOMPARE(lines, 41);
        f.canvas->setPageProperties(0, {{QStringLiteral("style"), QStringLiteral("plain")}});
        f.canvas->setEinkMode(false);
        COMPARE_COLOR(f.grab().pixelColor(2, 300), CANVAS_COLOR);
    }

    /// The tiles put together show the same as the page rendered in one piece
    void tilesMatchDirectRendering() {
        Fixture f(QSize(1100, 900));
        f.canvas->openFile(dataFile(QStringLiteral("strokes.xopp")));
        f.canvas->zoomTo(1.7);  // the view spans several tiles, with borders at odd positions
        f.wheel(QPointF(400, 300), -333);
        const QImage image = f.grab();

        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("strokes.xopp")).toLocalFile(), doc, nullptr));
        QImage reference(image.size(), QImage::Format_RGB32);
        reference.fill(CANVAS_COLOR);
        {
            const QRectF rect = f.canvas->pageViewRect(0);
            QPainter p(&reference);
            p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            p.translate(rect.topLeft());
            p.scale(f.canvas->zoom(), f.canvas->zoom());
            p.setClipRect(QRectF(0, 0, doc.pages[0].width, doc.pages[0].height));
            Renderer::renderPage(p, doc.pages[0], nullptr, QRectF(0, 0, doc.pages[0].width, doc.pages[0].height));
        }

        int different = 0;
        int ink = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                different += !similar(image.pixelColor(x, y), reference.pixelColor(x, y), 40);
                const QColor expected = reference.pixelColor(x, y);
                ink += !similar(expected, Qt::white) && !similar(expected, CANVAS_COLOR);
            }
        }
        QVERIFY(ink > 5000);  // the reference shows strokes and the ruling
        QVERIFY2(different < 50, qPrintable(QStringLiteral("%1 pixels differ").arg(different)));
    }

    void drawWithPen() {
        Fixture f;
        QSignalSpy pageChanged(f.canvas, &PageCanvas::pageChanged);
        const QPointF from = f.onPage(0, QPointF(100, 100));
        const QPointF to = f.onPage(0, QPointF(300, 100));
        f.stroke(from, to);

        QVERIFY(f.canvas->canUndo());
        QVERIFY(f.canvas->modified());
        QCOMPARE(pageChanged.count(), 1);
        COMPARE_COLOR(f.pixel((from + to) / 2), PEN_COLOR);
        COMPARE_COLOR(f.pixel((from + to) / 2 + QPointF(0, 20)), Qt::white);

        f.canvas->undo();
        COMPARE_COLOR(f.pixel((from + to) / 2), Qt::white);
        f.canvas->redo();
        COMPARE_COLOR(f.pixel((from + to) / 2), PEN_COLOR);

        // The stroke is still there after everything is rendered again
        f.canvas->zoomTo(2.0);
        f.canvas->zoomTo(f.canvas->zoom() / 2);
        const QPointF middle = f.onPage(0, QPointF(200, 100));
        COMPARE_COLOR(f.pixel(middle), PEN_COLOR);
    }

    void eraseWithEraserTip() {
        Fixture f;
        const QPointF from = f.onPage(0, QPointF(100, 100));
        const QPointF to = f.onPage(0, QPointF(300, 100));
        f.stroke(from, to);
        f.stroke(f.onPage(0, QPointF(100, 200)), f.onPage(0, QPointF(300, 200)));

        // Across the first stroke
        f.stroke(f.onPage(0, QPointF(200, 60)), f.onPage(0, QPointF(200, 140)), QPointingDevice::PointerType::Eraser);
        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(((from + to) / 2).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 200)).toPoint()), PEN_COLOR);

        f.canvas->undo();
        COMPARE_COLOR(f.pixel((from + to) / 2), PEN_COLOR);
    }

    void insertDeleteDuplicateMove() {
        Fixture f;
        QSignalSpy documentChanged(f.canvas, &PageCanvas::documentChanged);
        f.canvas->setPageProperties(
                0, {{QStringLiteral("style"), QStringLiteral("graph")}, {QStringLiteral("width"), 400.0}});
        f.stroke(f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));

        // A new page takes the format of the page before it
        f.canvas->insertPage(1);
        QCOMPARE(f.canvas->pageCount(), 2);
        QCOMPARE(f.canvas->currentPage(), 1);
        QCOMPARE(f.canvas->pageSize(1).width(), 400.0);
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("style")).toString(), QStringLiteral("graph"));
        QVERIFY(documentChanged.count() >= 2);

        // The copy has the stroke as well
        f.canvas->duplicatePage(0);
        QCOMPARE(f.canvas->pageCount(), 3);
        QCOMPARE(f.canvas->currentPage(), 1);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(200, 100))), PEN_COLOR);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(200, 107))), Qt::white);

        // Pages: stroke, stroke, empty -> stroke, empty, stroke
        f.canvas->movePage(1, 2);
        QCOMPARE(f.canvas->currentPage(), 2);
        f.canvas->setCurrentPage(1);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(200, 100))), Qt::white);

        f.canvas->deletePage(0);
        QCOMPARE(f.canvas->pageCount(), 2);
        f.canvas->setCurrentPage(0);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), Qt::white);
        f.canvas->setCurrentPage(1);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(200, 100))), PEN_COLOR);

        // Back to the single page with the stroke, and forth again
        for (int i = 0; i < 4; ++i) {
            f.canvas->undo();
        }
        QCOMPARE(f.canvas->pageCount(), 1);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), PEN_COLOR);
        for (int i = 0; i < 4; ++i) {
            f.canvas->redo();
        }
        QCOMPARE(f.canvas->pageCount(), 2);
        f.canvas->setCurrentPage(0);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), Qt::white);
        f.canvas->setCurrentPage(1);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(200, 100))), PEN_COLOR);
    }

    void theLastPageIsNotDeleted() {
        Fixture f;
        f.canvas->deletePage(0);
        QCOMPARE(f.canvas->pageCount(), 1);
        QVERIFY(!f.canvas->canUndo());

        // Invalid indices are ignored
        f.canvas->deletePage(5);
        f.canvas->duplicatePage(-1);
        f.canvas->movePage(0, 3);
        f.canvas->setPageProperties(7, {{QStringLiteral("style"), QStringLiteral("graph")}});
        QCOMPARE(f.canvas->pageCount(), 1);
        QVERIFY(!f.canvas->canUndo());
    }

    void pageFormatAndBackground() {
        Fixture f;
        f.canvas->insertPage(1);
        f.canvas->undo();
        f.canvas->redo();
        QCOMPARE(f.canvas->pageTypes().size(), 11);

        const QColor yellow(0xfe, 0xf8, 0xc9);
        f.canvas->setPageProperties(0, {{QStringLiteral("type"), QStringLiteral("solid")},
                                        {QStringLiteral("color"), yellow},
                                        {QStringLiteral("style"), QStringLiteral("ruled")}});
        f.canvas->setCurrentPage(0);
        QVariantMap properties = f.canvas->pageProperties(0);
        QCOMPARE(properties.value(QStringLiteral("style")).toString(), QStringLiteral("ruled"));
        QCOMPARE(properties.value(QStringLiteral("color")).value<QColor>(), yellow);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 90))), yellow);
        // The other page is not changed
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("style")).toString(), QStringLiteral("plain"));

        // Landscape for all pages: the layout follows
        f.canvas->setPageProperties(0, {{QStringLiteral("width"), 842.0}, {QStringLiteral("height"), 595.0}}, true);
        QCOMPARE(f.canvas->pageSize(0), QSizeF(842, 595));
        QCOMPARE(f.canvas->pageSize(1), QSizeF(842, 595));
        const QRectF rect = f.canvas->pageViewRect(0);
        QVERIFY(qAbs(rect.width() / rect.height() - 842.0 / 595.0) < 1e-6);
        COMPARE_COLOR(f.pixel(rect.center()), yellow);

        // One undo step for all pages
        f.canvas->undo();
        QCOMPARE(f.canvas->pageSize(1).height(), Page().height);
        f.canvas->undo();
        QCOMPARE(f.canvas->pageProperties(0).value(QStringLiteral("style")).toString(), QStringLiteral("plain"));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 90))), Qt::white);

        // Nothing changes: nothing to undo
        while (f.canvas->canUndo()) {
            f.canvas->undo();
        }
        f.canvas->setPageProperties(0, f.canvas->pageProperties(0));
        QVERIFY(!f.canvas->canUndo());
    }

    void savedFileHasThePages() {
        Fixture f;
        f.canvas->insertPage(1);
        f.canvas->setPageProperties(1, {{QStringLiteral("type"), QStringLiteral("solid")},
                                        {QStringLiteral("style"), QStringLiteral("graph")},
                                        {QStringLiteral("config"), QStringLiteral("m1=40,rm=1")},
                                        {QStringLiteral("width"), 500.0}});
        f.stroke(f.onPage(1, QPointF(100, 100)), f.onPage(1, QPointF(300, 100)));

        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("pages.xopp"));
        QVERIFY(f.canvas->saveAs(QUrl::fromLocalFile(path)));
        QVERIFY(!f.canvas->modified());

        Document doc;
        QString error;
        QVERIFY2(loadXopp(path, doc, &error), qPrintable(error));
        QCOMPARE(doc.pages.size(), size_t(2));
        QCOMPARE(doc.pages[1].width, 500.0);
        QCOMPARE(doc.pages[1].background.style, QStringLiteral("graph"));
        QCOMPARE(doc.pages[1].background.config, QStringLiteral("m1=40,rm=1"));
        QCOMPARE(doc.pages[0].layers[0].elements.size(), size_t(0));
        QCOMPARE(doc.pages[1].layers[0].elements.size(), size_t(1));
    }

    void goToPageAndCurrentPage() {
        Fixture f;
        for (int i = 0; i < 5; ++i) {
            f.canvas->insertPage(f.canvas->pageCount());
        }
        QCOMPARE(f.canvas->pageCount(), 6);

        QSignalSpy currentPageChanged(f.canvas, &PageCanvas::currentPageChanged);
        f.canvas->setCurrentPage(3);
        QCOMPARE(f.canvas->currentPage(), 3);
        QCOMPARE(currentPageChanged.count(), 1);
        // The top of the page is at the top of the view
        const double top = f.canvas->pageViewRect(3).top();
        QVERIFY2(top > 0 && top < 30, qPrintable(QString::number(top)));

        // Out of range: the nearest page
        f.canvas->setCurrentPage(99);
        QCOMPARE(f.canvas->currentPage(), 5);
        f.canvas->setCurrentPage(-3);
        QCOMPARE(f.canvas->currentPage(), 0);

        // Scrolling changes the current page to the one that fills most of the view
        for (int i = 0; i < 40; ++i) {
            f.wheel(QPointF(400, 300), -120);
        }
        int page = f.canvas->currentPage();
        QVERIFY(page > 0);
        const QRectF rect = f.canvas->pageViewRect(page).intersected(QRectF(0, 0, 800, 600));
        QVERIFY(rect.height() >= 300 - 20);
        COMPARE_COLOR(f.pixel(rect.center()), Qt::white);

        // Drawing on another page selects it
        f.canvas->zoomTo(0.5);
        page = f.canvas->currentPage();
        const int other = f.canvas->pageViewRect(page).top() > 100 ? page - 1 : page + 1;
        const QRectF otherRect = f.canvas->pageViewRect(other).intersected(QRectF(0, 0, 800, 600));
        QVERIFY(otherRect.height() > 30);
        f.stroke(otherRect.center() - QPointF(50, 0), otherRect.center() + QPointF(50, 0));
        QCOMPARE(f.canvas->currentPage(), other);
    }

    void zoom() {
        Fixture f;
        QSignalSpy viewChanged(f.canvas, &PageCanvas::viewChanged);
        f.canvas->zoomTo(1.0);
        QCOMPARE(f.canvas->zoom(), 1.0);
        QCOMPARE(f.canvas->pageViewRect(0).size(), f.canvas->pageSize(0));
        QVERIFY(viewChanged.count() >= 1);
        // The page is narrower than the view: centered
        QVERIFY(qAbs(f.canvas->pageViewRect(0).center().x() - 400.0) <= 1.0);

        f.canvas->fitPage();
        const QRectF rect = f.canvas->pageViewRect(0);
        QVERIFY(QRectF(0, 0, 800, 600).contains(rect));
        QVERIFY(rect.height() > 600 - 40);

        f.canvas->fitWidth();
        QVERIFY(f.canvas->pageViewRect(0).width() > 800 - 40);

        // Zooming with the wheel keeps the position under the pointer
        const QPointF anchor(250, 200);
        const QPointF before = (anchor - f.canvas->pageViewRect(0).topLeft()) / f.canvas->zoom();
        f.wheel(anchor, 240, Qt::ControlModifier);
        QVERIFY(f.canvas->zoom() > 1.6);
        const QPointF after = (anchor - f.canvas->pageViewRect(0).topLeft()) / f.canvas->zoom();
        QVERIFY2(QLineF(before, after).length() < 1.0, "the anchor moved");
        COMPARE_COLOR(f.pixel(anchor), Qt::white);

        f.canvas->zoomTo(1000);
        QCOMPARE(f.canvas->zoom(), 16.0);
        f.canvas->zoomTo(0);
        QCOMPARE(f.canvas->zoom(), 0.1);
        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(400, 300), Qt::white);
        COMPARE_COLOR(image.pixelColor(100, 100), CANVAS_COLOR);
    }

    void pairedPagesAndColumns() {
        Fixture f;
        for (int i = 0; i < 4; ++i) {
            f.canvas->insertPage(f.canvas->pageCount());
        }
        QSignalSpy layoutChanged(f.canvas, &PageCanvas::layoutChanged);
        f.canvas->setPairedPages(true);
        QCOMPARE(layoutChanged.count(), 1);
        QVERIFY(f.canvas->pairedPages());

        // Pages 2 and 3 side by side, both visible; page 1 alone above the right one
        const QRectF first = f.canvas->pageViewRect(0);
        const QRectF left = f.canvas->pageViewRect(1);
        const QRectF right = f.canvas->pageViewRect(2);
        QCOMPARE(left.top(), right.top());
        QVERIFY(left.right() <= right.left());
        QVERIFY(left.left() >= 0 && right.right() <= 800);
        QCOMPARE(first.left(), right.left());
        QVERIFY(first.bottom() < left.top());

        f.canvas->setCurrentPage(1);
        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.canvas->pageViewRect(1).center().toPoint().x(), 300), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.canvas->pageViewRect(2).center().toPoint().x(), 300), Qt::white);

        f.canvas->setPairedPages(false);
        f.canvas->setLayoutColumns(3);
        QCOMPARE(f.canvas->layoutColumns(), 3);
        QCOMPARE(f.canvas->layoutRows(), 0);
        QCOMPARE(f.canvas->pageViewRect(0).top(), f.canvas->pageViewRect(2).top());
        QVERIFY(f.canvas->pageViewRect(3).top() > f.canvas->pageViewRect(0).bottom());

        f.canvas->setLayoutRows(1);
        QCOMPARE(f.canvas->layoutRows(), 1);
        QCOMPARE(f.canvas->layoutColumns(), 0);
        QCOMPARE(f.canvas->pageViewRect(0).top(), f.canvas->pageViewRect(4).top());

        f.canvas->setLayoutColumns(1);
        QVERIFY(f.canvas->pageViewRect(1).top() > f.canvas->pageViewRect(0).bottom());
    }

    void presentationMode() {
        Fixture f;
        for (int i = 0; i < 2; ++i) {
            f.canvas->insertPage(f.canvas->pageCount());
        }
        f.canvas->setCurrentPage(1);
        f.stroke(f.onPage(1, QPointF(100, 100)), f.onPage(1, QPointF(300, 100)));

        f.canvas->setPresentationMode(true);
        QVERIFY(f.canvas->presentationMode());
        QCOMPARE(f.canvas->currentPage(), 1);

        // The whole page, and nothing of the others
        const QRectF view(0, 0, 800, 600);
        QVERIFY(view.contains(f.canvas->pageViewRect(1)));
        QVERIFY(!view.intersects(f.canvas->pageViewRect(0)));
        QVERIFY(!view.intersects(f.canvas->pageViewRect(2)));
        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(1, QPointF(200, 100)).toPoint()), PEN_COLOR);
        COMPARE_COLOR(image.pixelColor(10, 300), CANVAS_COLOR);

        // The wheel turns the pages and stops at the last one
        f.wheel(QPointF(400, 300), -120);
        QCOMPARE(f.canvas->currentPage(), 2);
        QVERIFY(view.contains(f.canvas->pageViewRect(2)));
        f.wheel(QPointF(400, 300), -120);
        QCOMPARE(f.canvas->currentPage(), 2);
        f.wheel(QPointF(400, 300), 120);
        f.wheel(QPointF(400, 300), 120);
        QCOMPARE(f.canvas->currentPage(), 0);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), Qt::white);

        // Drawing works in presentation mode
        f.stroke(f.onPage(0, QPointF(100, 300)), f.onPage(0, QPointF(300, 300)));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 300))), PEN_COLOR);

        f.canvas->setPresentationMode(false);
        QCOMPARE(f.canvas->currentPage(), 0);
        QVERIFY(f.canvas->pageViewRect(0).width() > 800 - 40);
        QVERIFY(f.canvas->pageViewRect(1).top() - f.canvas->pageViewRect(0).bottom() < 30);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 300))), PEN_COLOR);
    }

    void pdfBackground() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("background.pdf"));
        writePdf(path, {QRectF(100, 100, 300, 500), QRectF(300, 50, 200, 100)});

        Fixture f;
        f.canvas->openFile(QUrl::fromLocalFile(path));
        QCOMPARE(f.canvas->pageCount(), 2);
        QCOMPARE(f.canvas->pdfPageCount(), 2);
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("pdfPage")).toInt(), 2);

        // The rectangle of the PDF is at its place, also where it crosses the borders of tiles
        for (double zoom: {f.canvas->zoom(), 2.3}) {
            f.canvas->zoomTo(zoom);
            f.canvas->setCurrentPage(0);
            const QImage image = f.grab();
            const QRectF black = QRectF(f.onPage(0, QPointF(100, 100)), QSizeF(300, 500) * zoom);
            int wrong = 0;
            for (int y = 0; y < image.height(); y += 3) {
                for (int x = 0; x < image.width(); x += 3) {
                    const QPointF pos(x + 0.5, y + 0.5);
                    if (!f.canvas->pageViewRect(0).adjusted(2, 2, -2, -2).contains(pos)) {
                        continue;
                    }
                    if (black.adjusted(2, 2, -2, -2).contains(pos)) {
                        wrong += !similar(image.pixelColor(x, y), Qt::black);
                    } else if (!black.adjusted(-2, -2, 2, 2).contains(pos)) {
                        wrong += !similar(image.pixelColor(x, y), Qt::white);
                    }
                }
            }
            QVERIFY2(wrong == 0, qPrintable(QStringLiteral("%1 wrong pixels at zoom %2").arg(wrong).arg(zoom)));
        }

        // Annotations are drawn on top of the PDF
        f.canvas->fitWidth();
        f.canvas->setColor(Qt::red);
        f.stroke(f.onPage(0, QPointF(150, 300)), f.onPage(0, QPointF(350, 300)));
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(250, 300))), Qt::red);
        f.canvas->undo();
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(250, 300))), Qt::black);

        // A new page after a PDF page is an empty sheet of the same size; it can show a PDF page as well
        f.canvas->insertPage(1);
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("type")).toString(), QStringLiteral("solid"));
        QCOMPARE(f.canvas->pageSize(1), f.canvas->pageSize(0));
        f.canvas->setPageProperties(1,
                                    {{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("pdfPage"), 2}});
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("pdfPage")).toInt(), 2);
        f.canvas->setCurrentPage(1);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(400, 100))), Qt::black);
        COMPARE_COLOR(f.pixel(f.onPage(1, QPointF(250, 300))), Qt::white);

        // A PDF page that does not exist is refused
        f.canvas->setPageProperties(1,
                                    {{QStringLiteral("type"), QStringLiteral("pdf")}, {QStringLiteral("pdfPage"), 3}});
        QCOMPARE(f.canvas->pageProperties(1).value(QStringLiteral("pdfPage")).toInt(), 2);

        // Previews show the PDF as well
        QImage preview(120, 170, QImage::Format_RGB32);
        preview.fill(Qt::gray);
        {
            QPainter p(&preview);
            f.canvas->paintPage(&p, 0, preview.size());
        }
        const double scale = 120 / f.canvas->pageSize(0).width();
        COMPARE_COLOR(preview.pixelColor((QPointF(250, 300) * scale).toPoint()), Qt::black);
        COMPARE_COLOR(preview.pixelColor((QPointF(50, 300) * scale).toPoint()), Qt::white);

        // Another document replaces the PDF while tiles may still be rendered
        f.canvas->zoomTo(3.1);
        f.canvas->newDocument();
        QCOMPARE(f.canvas->pdfPageCount(), 0);
        COMPARE_COLOR(f.pixel(QPointF(400, 300)), Qt::white);
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void preview() {
        Fixture f;
        f.stroke(f.onPage(0, QPointF(100, 100)), f.onPage(0, QPointF(300, 100)));
        f.stroke(f.onPage(0, QPointF(100, 400)), f.onPage(0, QPointF(500, 400)));

        QImage preview(200, 400, QImage::Format_RGB32);
        preview.fill(Qt::gray);
        {
            QPainter p(&preview);
            f.canvas->paintPage(&p, 0, preview.size());
            f.canvas->paintPage(&p, 3, preview.size());  // no such page: nothing happens
        }
        // The page keeps its aspect ratio: it does not fill the height
        const double scale = 200 / f.canvas->pageSize(0).width();
        COMPARE_COLOR(preview.pixelColor(100, 20), Qt::white);
        COMPARE_COLOR(preview.pixelColor((QPointF(300, 400) * scale).toPoint()), PEN_COLOR);
        COMPARE_COLOR(preview.pixelColor(100, 390), Qt::gray);
    }

    /// The item of the sidebar follows the changes of its page
    void previewItem() {
        Fixture f;
        f.canvas->setWidth(400);
        auto* item = new PagePreview(f.window.contentItem());
        item->setPosition(QPointF(450, 50));
        item->setSize(QSizeF(300, 424));
        item->setCanvas(f.canvas);
        item->setPage(0);
        QCOMPARE(item->canvas(), f.canvas);

        const double scale = 300 / f.canvas->pageSize(0).width();
        const QPointF onStroke = QPointF(450, 50) + QPointF(300, 400) * scale;
        COMPARE_COLOR(f.pixel(onStroke), Qt::white);

        f.stroke(f.onPage(0, QPointF(100, 400)), f.onPage(0, QPointF(500, 400)));
        // The stroke is only three pixels wide in the preview: its edge is lighter
        QTRY_VERIFY(similar(f.pixel(onStroke), PEN_COLOR, 90));

        // Another page takes its place
        f.canvas->insertPage(0);
        QTRY_VERIFY(similar(f.pixel(onStroke), Qt::white));
        item->setPage(1);
        QTRY_VERIFY(similar(f.pixel(onStroke), PEN_COLOR, 90));
    }

    void manyPagesStayResponsive() {
        Fixture f;
        f.canvas->openFile(dataFile(QStringLiteral("strokes.xopp")));
        for (int i = 0; i < 6; ++i) {
            f.canvas->duplicatePage(f.canvas->pageCount() - 1);  // 2, 3, ... pages
        }
        for (int i = 0; i < 8; ++i) {
            for (int page = f.canvas->pageCount() - 1; page >= 0; page -= 2) {
                f.canvas->duplicatePage(page);
            }
        }
        QVERIFY(f.canvas->pageCount() > 150);

        // Scrolling and zooming do not wait for the rendering
        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 200; ++i) {
            f.wheel(QPointF(400, 300), -120);
        }
        f.canvas->setCurrentPage(f.canvas->pageCount() - 1);
        f.canvas->zoomTo(3.0);
        f.canvas->setCurrentPage(0);
        f.canvas->fitWidth();
        QVERIFY2(timer.elapsed() < 2000, qPrintable(QStringLiteral("%1 ms").arg(timer.elapsed())));

        const QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(3, 300), CANVAS_COLOR);
        QVERIFY(!similar(image.pixelColor(400, 300), CANVAS_COLOR));
    }

    /// A pen next to the pages (on a toolbar) is a mouse there, where Qt does not make it one (Android)
    void penOutsideOfThePages() {
        Fixture f;
        PageCanvas* c = f.canvas;
        // The canvas takes the left half of the window; the right half stands for a toolbar
        const QSizeF size = c->size();
        c->setSize(QSizeF(size.width() / 2, size.height()));
        const QPointF outside(size.width() * 0.75, size.height() / 2);
        const QPointF inside(size.width() * 0.25, size.height() / 2);
        MouseRecorder recorder;
        f.window.installEventFilter(&recorder);

        // As on the desktop: left to Qt
        c->setPenClicksAsMouse(false);
        f.tablet(QEvent::TabletPress, outside, 0.5);
        f.tablet(QEvent::TabletRelease, outside, 0);
        QVERIFY(recorder.types.isEmpty());

        c->setPenClicksAsMouse(true);
        f.tablet(QEvent::TabletPress, outside, 0.5);
        QCOMPARE(recorder.types, QList<QEvent::Type>{QEvent::MouseButtonPress});
        QCOMPARE(recorder.lastPosition, outside);
        // Moved over the pages while pressed, it does not draw: it belongs to where it was pressed
        f.tablet(QEvent::TabletMove, inside, 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(40, 0), 0.5);
        f.tablet(QEvent::TabletRelease, inside + QPointF(40, 0), 0);
        QCOMPARE(recorder.types.last(), QEvent::MouseButtonRelease);
        QVERIFY(c->document().pages[0].layers[0].elements.empty());

        // In a menu or dialog, which may lie over the pages, a tap stays where it began although the pen slides
        // a little: a menu that scrolls took it for the beginning of that, and its entry was not chosen
        QQuickOverlay overlay(f.window.contentItem());
        overlay.setSize(QSizeF(f.window.size()));
        ClickTaker menu(&overlay);
        menu.setSize(overlay.size());
        recorder.types.clear();
        f.tablet(QEvent::TabletPress, inside, 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(3, 4), 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(6, -5), 0.5);
        f.tablet(QEvent::TabletRelease, inside + QPointF(6, -5), 0);
        QCOMPARE(recorder.types, (QList<QEvent::Type>{QEvent::MouseButtonPress, QEvent::MouseButtonRelease}));
        QCOMPARE(recorder.lastPosition, inside);
        QVERIFY(c->document().pages[0].layers[0].elements.empty());
        // Moved clearly, it drags: a slider, or the menu is scrolled
        recorder.types.clear();
        f.tablet(QEvent::TabletPress, inside, 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(0, 60), 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(0, 62), 0.5);
        f.tablet(QEvent::TabletRelease, inside + QPointF(0, 62), 0);
        QCOMPARE(recorder.types, (QList<QEvent::Type>{QEvent::MouseButtonPress, QEvent::MouseMove, QEvent::MouseMove,
                                                      QEvent::MouseButtonRelease}));
        QCOMPARE(recorder.lastPosition, inside + QPointF(0, 62));
        menu.setParentItem(nullptr);
        overlay.setParentItem(nullptr);

        // On the pages it still draws, and is no mouse
        recorder.types.clear();
        f.tablet(QEvent::TabletPress, inside, 0.5);
        f.tablet(QEvent::TabletMove, inside + QPointF(40, 10), 0.5);
        f.tablet(QEvent::TabletRelease, inside + QPointF(40, 10), 0);
        QVERIFY(recorder.types.isEmpty());
        QCOMPARE(c->document().pages[0].layers[0].elements.size(), size_t(1));
        f.window.removeEventFilter(&recorder);
    }
};

QTEST_MAIN(TestCanvas)
#include "tst_canvas.moc"
