/*
 * Qournal
 *
 * Tests for the layers and for the features that work with the PDF of the background: text selection, search.
 * They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QClipboard>
#include <QGuiApplication>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <cmath>

#include "CanvasFixture.h"
#include "Renderer.h"
#include "TextBlock.h"
#include "XoppLoader.h"

namespace {

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }

const std::vector<Layer>& layersOf(const Fixture& f, int page = 0) {
    return f.canvas->document().pages[static_cast<size_t>(page)].layers;
}

QString layerName(const Fixture& f, int layer) {
    return f.canvas->layers().value(layer).toMap().value(QStringLiteral("name")).toString();
}

/// A PDF with text: two lines on the first page, one on the second. The units are points
void writeTextPdf(const QString& path) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p(&writer);
    QFont font(QStringLiteral("Sans"));
    font.setPixelSize(20);
    p.setFont(font);
    p.drawText(QPointF(100, 200), QStringLiteral("Hello PDF world"));
    p.drawText(QPointF(100, 240), QStringLiteral("Second line here"));
    writer.newPage();
    p.drawText(QPointF(100, 300), QStringLiteral("Another page of the world"));
}

}  // namespace

class TestLayers: public QObject {
    Q_OBJECT

private slots:
    void addSelectAndHide() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy layersChanged(c, &PageCanvas::layersChanged);
        QCOMPARE(c->layers().size(), 1);
        QCOMPARE(c->currentLayer(), 0);
        QCOMPARE(layerName(f, 0), QStringLiteral("Layer 1"));

        // What is drawn goes to the current layer
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        c->addLayer();
        QCOMPARE(c->layers().size(), 2);
        QCOMPARE(c->currentLayer(), 1);
        QVERIFY(layersChanged.count() >= 1);
        f.strokeOnPage(0, {{100, 200}, {300, 200}});
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(1));
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(1));
        c->setCurrentLayer(0);
        f.strokeOnPage(0, {{100, 300}, {300, 300}});
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(2));

        // A layer below the current one, and all layers hidden and shown at once
        c->setCurrentLayer(1);
        c->addLayer(true);
        QCOMPARE(c->layers().size(), 3);
        QCOMPARE(c->currentLayer(), 1);
        QVERIFY(layersOf(f)[1].elements.empty());
        QCOMPARE(layersOf(f)[2].elements.size(), size_t(1));
        c->setAllLayersVisible(false);
        for (const QVariant& layer: c->layers()) {
            QVERIFY(!layer.toMap().value(QStringLiteral("visible")).toBool());
        }
        c->setAllLayersVisible(true);
        for (const QVariant& layer: c->layers()) {
            QVERIFY(layer.toMap().value(QStringLiteral("visible")).toBool());
        }
        c->undo();
        QCOMPARE(c->layers().size(), 2);
        c->setCurrentLayer(0);

        // A hidden layer is not shown, also not in the previews
        c->setLayerVisible(1, false);
        QCOMPARE(c->layers()[1].toMap().value(QStringLiteral("visible")).toBool(), false);
        QImage image = f.grab();
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 200)).toPoint()), Qt::white);
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 100)).toPoint()), PEN_COLOR);
        QImage preview(200, 283, QImage::Format_RGB32);
        preview.fill(Qt::gray);
        {
            QPainter p(&preview);
            c->paintPage(&p, 0, preview.size());
        }
        const double scale = 200 / c->pageSize(0).width();
        COMPARE_COLOR(preview.pixelColor((QPointF(200, 200) * scale).toPoint()), Qt::white);
        QVERIFY(!similar(preview.pixelColor((QPointF(200, 100) * scale).toPoint()), Qt::white));

        // Drawing on a hidden layer shows it
        c->setCurrentLayer(1);
        QCOMPARE(c->layers()[1].toMap().value(QStringLiteral("visible")).toBool(), true);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 200))), PEN_COLOR);

        // Without the background a checkerboard shows that there is nothing
        c->setBackgroundVisible(false);
        QVERIFY(!c->backgroundVisible());
        image = f.grab();
        QVERIFY(!similar(image.pixelColor(f.onPage(0, QPointF(404, 404)).toPoint()), Qt::white));
        COMPARE_COLOR(image.pixelColor(f.onPage(0, QPointF(200, 200)).toPoint()), PEN_COLOR);
        c->setBackgroundVisible(true);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(404, 404))), Qt::white);

        // Invalid layers are ignored
        c->setCurrentLayer(7);
        c->setLayerVisible(-1, false);
        c->deleteLayer(9);
        c->mergeLayerDown(0);
        c->moveLayer(0, 5);
        QCOMPARE(c->currentLayer(), 1);
        QCOMPARE(c->layers().size(), 2);
    }

    void arrangeLayers() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        c->addLayer();
        f.strokeOnPage(0, {{100, 200}, {300, 200}});
        f.strokeOnPage(0, {{100, 220}, {300, 220}});

        c->renameLayer(1, QStringLiteral("Notes"));
        QCOMPARE(layerName(f, 1), QStringLiteral("Notes"));
        QCOMPARE(layerName(f, 0), QStringLiteral("Layer 1"));

        // The copy goes above the original and is the current layer
        c->duplicateLayer(1);
        QCOMPARE(c->layers().size(), 3);
        QCOMPARE(c->currentLayer(), 2);
        QCOMPARE(layersOf(f)[2].elements.size(), size_t(2));
        QCOMPARE(layerName(f, 2), QStringLiteral("Notes"));

        c->moveLayer(2, 0);
        QCOMPARE(c->currentLayer(), 0);
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(2));
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(1));

        // Merging puts the elements on top of those of the layer below
        c->mergeLayerDown(1);
        QCOMPARE(c->layers().size(), 2);
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(3));
        QCOMPARE(std::get<Stroke>(layersOf(f)[0].elements[2]).points[0].y(), 100.0);
        QCOMPARE(c->currentLayer(), 0);

        c->deleteLayer(1);
        QCOMPARE(c->layers().size(), 1);
        // The last layer stays
        c->deleteLayer(0);
        QCOMPARE(c->layers().size(), 1);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(200, 100))), PEN_COLOR);

        // Everything can be undone: delete, merge, move, duplicate, rename
        c->undo();
        QCOMPARE(c->layers().size(), 2);
        QCOMPARE(layerName(f, 1), QStringLiteral("Notes"));
        c->undo();
        QCOMPARE(c->layers().size(), 3);
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(2));
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(1));
        c->undo();
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(1));
        c->undo();
        QCOMPARE(c->layers().size(), 2);
        c->undo();
        QCOMPARE(layerName(f, 1), QStringLiteral("Layer 2"));
        for (int i = 0; i < 5; ++i) {
            c->redo();
        }
        QCOMPARE(c->layers().size(), 1);
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(3));

        // Layers and their names are stored in the file; what is hidden is not a property of the file
        c->addLayer();
        c->renameLayer(1, QStringLiteral("Top"));
        c->setLayerVisible(1, false);
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("layers.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document reloaded;
        QVERIFY(loadXopp(path, reloaded, nullptr));
        QCOMPARE(reloaded.pages[0].layers.size(), size_t(2));
        QCOMPARE(reloaded.pages[0].layers[1].name, QStringLiteral("Top"));
        QCOMPARE(reloaded.pages[0].layers[0].name, QStringLiteral("Notes"));
        QVERIFY(reloaded.pages[0].layers[1].visible);
    }

    void toolsWorkOnTheCurrentLayer() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->input()->setProperty("snapGrid", false);
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        c->addLayer();
        f.strokeOnPage(0, {{100, 200}, {300, 200}});

        // The eraser leaves the other layer alone
        c->setTool(PageCanvas::Eraser);
        c->setEraserType(PageCanvas::EraseStrokes);
        f.stroke(f.onPage(0, QPointF(200, 60)), f.onPage(0, QPointF(200, 240)));
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(1));
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(0));
        c->undo();

        // The selection tools as well, unless all layers are included; hidden layers never are
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(200, 100)));
        QVERIFY(!c->hasSelection());
        c->setSelectAllLayers(true);
        f.tap(f.onPage(0, QPointF(200, 100)));
        QVERIFY(c->hasSelection());
        c->clearSelection();
        c->setLayerVisible(0, false);
        f.tap(f.onPage(0, QPointF(200, 100)));
        QVERIFY(!c->hasSelection());
        c->setLayerVisible(0, true);
        c->setSelectAllLayers(false);

        // The selection can be moved to another layer, and stays selected there
        f.tap(f.onPage(0, QPointF(200, 200)));
        QVERIFY(c->hasSelection());
        c->moveSelectionToLayer(0);
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(2));
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(0));
        QVERIFY(c->hasSelection());
        QCOMPARE(c->currentLayer(), 0);
        c->deleteSelection();
        QCOMPARE(layersOf(f)[0].elements.size(), size_t(1));
        c->undo();
        c->undo();
        QCOMPARE(layersOf(f)[1].elements.size(), size_t(1));

        // The vertical space tool moves the current layer only
        c->setCurrentLayer(1);
        c->setTool(PageCanvas::VerticalSpace);
        f.stroke(f.onPage(0, QPointF(50, 50)), f.onPage(0, QPointF(50, 90)));
        QCOMPARE(std::get<Stroke>(layersOf(f)[0].elements[0]).points[0].y(), 100.0);
        QVERIFY(near(std::get<Stroke>(layersOf(f)[1].elements[0]).points[0].y(), 240.0));

        // Every page has its own layers and its own current layer
        c->insertPage(1);
        QCOMPARE(c->currentPage(), 1);
        QCOMPARE(c->layers().size(), 1);
        QCOMPARE(c->currentLayer(), 0);
        c->setCurrentPage(0);
        QCOMPARE(c->layers().size(), 2);
        QCOMPARE(c->currentLayer(), 1);
    }

    void layersOfAFile() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->openFile(dataFile(QStringLiteral("layers.xopp")));
        QVERIFY(c->layers().size() > 1);
        // The top layer is the one that is drawn on
        QCOMPARE(c->currentLayer(), static_cast<int>(c->layers().size()) - 1);
        const size_t before = layersOf(f).back().elements.size();
        f.strokeOnPage(0, {{100, 100}, {300, 100}});
        QCOMPARE(layersOf(f).back().elements.size(), before + 1);
    }

    void pdfTextSelection() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("text.pdf"));
        writeTextPdf(path);
        Fixture f;
        PageCanvas* c = f.canvas;
        c->openFile(QUrl::fromLocalFile(path));
        QCOMPARE(c->pageCount(), 2);
        QSignalSpy pdfSelectionChanged(c, &PageCanvas::pdfSelectionChanged);

        // From the beginning to the end of the first line
        c->setTool(PageCanvas::SelectPdfTextLinear);
        f.stroke(f.onPage(0, QPointF(95, 193)), f.onPage(0, QPointF(300, 195)));
        QVERIFY(c->hasPdfSelection());
        QVERIFY(pdfSelectionChanged.count() >= 1);
        QVERIFY2(c->pdfSelectionText().contains(QStringLiteral("Hello PDF world")), qPrintable(c->pdfSelectionText()));
        QVERIFY(!c->pdfSelectionText().contains(QStringLiteral("Second")));
        const QRectF rect = c->pdfSelectionRect();
        QVERIFY(rect.contains(f.onPage(0, QPointF(150, 194))));
        QVERIFY(rect.height() < 40 * c->zoom());
        // The selected text is marked on the screen
        const QImage selected = f.grab();
        int marked = 0;
        for (int y = 0; y < selected.height(); ++y) {
            for (int x = 0; x < selected.width(); ++x) {
                const QColor color = selected.pixelColor(x, y);
                marked += color.blue() > color.red() + 40 && color.blue() > 200;
            }
        }
        QVERIFY2(marked > 500, qPrintable(QString::number(marked)));

        c->copyPdfSelection();
        QVERIFY(QGuiApplication::clipboard()->text().contains(QStringLiteral("Hello PDF world")));

        // Highlighting draws a highlighter stroke as high as the line
        c->highlightPdfSelection();
        QVERIFY(!c->hasPdfSelection());
        QCOMPARE(f.strokes().size(), 1);
        Stroke stroke = f.strokes()[0];
        QCOMPARE(stroke.tool, Stroke::Tool::Highlighter);
        QCOMPARE(stroke.color.alpha(), 0x7f);
        QVERIFY(stroke.width > 12 && stroke.width < 40);
        QVERIFY(stroke.points[0].x() < 105 && stroke.points[1].x() > 220);
        QVERIFY(near(stroke.points[0].y(), 193, 8));

        // Down to the second line: two lines, two strokes under them
        f.stroke(f.onPage(0, QPointF(95, 193)), f.onPage(0, QPointF(200, 235)));
        QVERIFY(c->pdfSelectionText().contains(QStringLiteral("Hello PDF world")));
        QVERIFY(c->pdfSelectionText().contains(QStringLiteral("Second")));
        c->underlinePdfSelection();
        QCOMPARE(f.strokes().size(), 3);
        QCOMPARE(f.strokes()[1].tool, Stroke::Tool::Pen);
        QVERIFY(f.strokes()[1].points[0].y() > 195 && f.strokes()[1].points[0].y() < 215);
        QVERIFY(f.strokes()[2].points[0].y() > 235);
        c->undo();
        QCOMPARE(f.strokes().size(), 1);

        // The opacity of the marker can be chosen
        c->setPdfMarkerAlpha(200);
        f.stroke(f.onPage(0, QPointF(95, 193)), f.onPage(0, QPointF(200, 195)));
        c->highlightPdfSelection();
        QCOMPARE(f.strokes().size(), 2);
        QCOMPARE(f.strokes()[1].color.alpha(), 200);
        c->setPdfMarkerAlpha(0);
        QCOMPARE(c->pdfMarkerAlpha(), 1);
        c->undo();

        // A rectangle selects the characters in it, of both lines
        c->setTool(PageCanvas::SelectPdfTextRect);
        f.stroke(f.onPage(0, QPointF(95, 175)), f.onPage(0, QPointF(165, 250)));
        QVERIFY(c->hasPdfSelection());
        const QStringList lines = c->pdfSelectionText().split(u'\n');
        QVERIFY2(lines.size() == 2 && lines[0].startsWith(QStringLiteral("Hello")) &&
                         lines[1].startsWith(QStringLiteral("Second")) && !lines[0].contains(QStringLiteral("world")),
                 qPrintable(c->pdfSelectionText()));
        c->strikeThroughPdfSelection();
        QCOMPARE(f.strokes().size(), 3);
        QVERIFY(f.strokes()[1].points[1].x() < 175);

        // A rectangle next to the text selects nothing, and a press drops the selection
        f.stroke(f.onPage(0, QPointF(300, 350)), f.onPage(0, QPointF(400, 400)));
        QVERIFY(!c->hasPdfSelection());
        f.stroke(f.onPage(0, QPointF(95, 175)), f.onPage(0, QPointF(165, 250)));
        QVERIFY(c->hasPdfSelection());
        f.tap(f.onPage(0, QPointF(300, 400)));
        QVERIFY(!c->hasPdfSelection());

        // On a page without PDF the tools do nothing
        c->newDocument();
        f.stroke(f.onPage(0, QPointF(95, 175)), f.onPage(0, QPointF(165, 250)));
        QVERIFY(!c->hasPdfSelection());
        QVERIFY(c->pdfOutline().isEmpty());
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void search() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("text.pdf"));
        writeTextPdf(path);
        Fixture f;
        PageCanvas* c = f.canvas;
        c->openFile(QUrl::fromLocalFile(path));
        QSignalSpy searchChanged(c, &PageCanvas::searchChanged);

        // A text on the second page, next to the text of the PDF
        c->setCurrentPage(1);
        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(1, QPointF(100, 400)));
        c->setTextEditText(QStringLiteral("A note about the World\nand a second world"));
        c->finishTextEdit();
        c->setCurrentPage(0);

#ifdef HAVE_QTPDF
        // From the current page on: first the result in the PDF of the first page
        QVERIFY(c->search(QStringLiteral("world")));
        QVERIFY(c->searching());
        QVERIFY(searchChanged.count() >= 1);
        QCOMPARE(c->currentPage(), 0);
        QCOMPARE(c->searchResultCount(), 1);
        QCOMPARE(c->searchResultIndex(), 1);
        // The result is marked: orange on the white page
        const QImage image = f.grab();
        int marked = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor color = image.pixelColor(x, y);
                marked += color.red() > 230 && color.green() > 150 && color.blue() < 160;
            }
        }
        QVERIFY2(marked > 200, qPrintable(QString::number(marked)));

        // Then the second page: in its PDF and twice in the text, in reading order
        QVERIFY(c->searchNext());
        QCOMPARE(c->currentPage(), 1);
        QCOMPARE(c->searchResultCount(), 3);
        QCOMPARE(c->searchResultIndex(), 1);
        QVERIFY(c->searchNext());
        QVERIFY(c->searchNext());
        QCOMPARE(c->searchResultIndex(), 3);
        // Around the end of the document
        QVERIFY(c->searchNext());
        QCOMPARE(c->currentPage(), 0);
        QVERIFY(c->searchPrevious());
        QCOMPARE(c->currentPage(), 1);
        QCOMPARE(c->searchResultIndex(), 3);
        const int pdfResults = 1;
#else
        QVERIFY(c->search(QStringLiteral("world")));
        QCOMPARE(c->currentPage(), 1);
        const int pdfResults = 0;
#endif

        // Changes of the texts are found
        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(1, QPointF(100, 500)));
        c->setTextEditText(QStringLiteral("world again"));
        c->finishTextEdit();
        QVERIFY(c->search(QStringLiteral("WORLD")));
        c->setCurrentPage(1);
        QVERIFY(c->search(QStringLiteral("WORLD")));
        QCOMPARE(c->searchResultCount(), pdfResults + 3);

        // What is not there
        QVERIFY(!c->search(QStringLiteral("xyzzy")));
        QCOMPARE(c->searchResultCount(), 0);
        QVERIFY(!c->searchNext());
        c->clearSearch();
        QVERIFY(!c->searching());
        QVERIFY(!c->searchNext());
        QVERIFY(!c->search(QString()));
    }

    void rangesOfATextBlock() {
        const TextStyle style{QStringLiteral("Sans"), 20};
        const TextBlock block(QStringLiteral("first line\nsecond"), style);
        const QList<QRectF> first = block.rangeRects(0, 5);
        QCOMPARE(first.size(), 1);
        QCOMPARE(first[0].left(), 0.0);
        QCOMPARE(first[0].top(), 0.0);
        QCOMPARE(first[0].height(), block.lineHeight());
        QVERIFY(first[0].width() > 20 && first[0].width() < 60);
        // On the second line, and across the line break
        const QList<QRectF> second = block.rangeRects(11, 6);
        QCOMPARE(second.size(), 1);
        QCOMPARE(second[0].top(), block.lineHeight());
        QCOMPARE(block.rangeRects(6, 8).size(), 2);
        QVERIFY(block.rangeRects(3, 0).isEmpty());
        QVERIFY(block.rangeRects(100, 5).isEmpty());
    }
};

QTEST_MAIN(TestLayers)
#include "tst_layers.moc"
