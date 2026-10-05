/*
 * Qournal
 *
 * Tests for moving through the document and for pages that come from elsewhere: the history of visited places,
 * the pages of a PDF that are appended, an image as background. They run without a display
 * (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QDate>
#include <QFile>
#include <QPainter>
#include <QPdfWriter>
#include <QSignalSpy>
#include <QTemporaryDir>

#include "CanvasFixture.h"
#include "XoppLoader.h"

namespace {

void writePdf(const QString& path, int pages) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(300, 400), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter p(&writer);
    for (int i = 0; i < pages; ++i) {
        if (i > 0) {
            writer.newPage();
        }
        p.drawText(QPointF(50, 100), QStringLiteral("Page %1").arg(i + 1));
    }
}

}  // namespace

class TestJournal: public QObject {
    Q_OBJECT

private slots:
    /// The pages that have something on them, before and after the current one
    void annotatedPages() {
        Fixture f;
        PageCanvas* c = f.canvas;
        for (int i = 1; i < 6; ++i) {
            c->insertPage(i);
        }
        c->setCurrentPage(1);
        f.strokeOnPage(1, {{100, 100}, {200, 100}});
        c->setCurrentPage(4);
        f.strokeOnPage(4, {{100, 100}, {200, 100}});

        c->setCurrentPage(0);
        QCOMPARE(c->previousAnnotatedPage(), -1);
        QCOMPARE(c->nextAnnotatedPage(), 1);
        c->setCurrentPage(1);
        QCOMPARE(c->nextAnnotatedPage(), 4);
        c->setCurrentPage(3);
        QCOMPARE(c->previousAnnotatedPage(), 1);
        c->setCurrentPage(5);
        QCOMPARE(c->previousAnnotatedPage(), 4);
        QCOMPARE(c->nextAnnotatedPage(), -1);
    }

    void visitedPlaces() {
        Fixture f;
        PageCanvas* c = f.canvas;
        for (int i = 1; i < 10; ++i) {
            c->insertPage(i);
        }
        // Back to the first page in steps, which are not jumps
        for (int i = c->currentPage(); i >= 0; --i) {
            c->setCurrentPage(i);
        }
        QVERIFY(!c->canNavigateBack());
        QSignalSpy changed(c, &PageCanvas::navigationChanged);

        // A step to the next page is not a jump
        c->setCurrentPage(1);
        QVERIFY(!c->canNavigateBack());
        const QPointF first = c->pageViewRect(1).topLeft();

        c->setCurrentPage(6);
        QVERIFY(c->canNavigateBack());
        QVERIFY(!c->canNavigateForward());
        QVERIFY(changed.count() > 0);
        c->setCurrentPage(9);
        const QPointF last = c->pageViewRect(9).topLeft();

        c->navigateBack();
        QCOMPARE(c->currentPage(), 6);
        QVERIFY(c->canNavigateForward());
        c->navigateBack();
        QCOMPARE(c->currentPage(), 1);
        QCOMPARE(c->pageViewRect(1).topLeft(), first);  // the same place, not only the same page
        QVERIFY(!c->canNavigateBack());
        c->navigateBack();  // nothing to go back to
        QCOMPARE(c->currentPage(), 1);

        c->navigateForward();
        c->navigateForward();
        QCOMPARE(c->currentPage(), 9);
        QCOMPARE(c->pageViewRect(9).topLeft(), last);
        QVERIFY(!c->canNavigateForward());

        // A new jump drops what was ahead
        c->navigateBack();
        c->setCurrentPage(3);
        QVERIFY(!c->canNavigateForward());

        // Another document starts anew
        c->newDocument();
        QVERIFY(!c->canNavigateBack());
    }

    void appendPdfPages() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
        writePdf(pdf, 4);
        Fixture f;
        PageCanvas* c = f.canvas;
        QCOMPARE(c->appendNewPdfPages(), 0);  // no PDF
        c->openFile(QUrl::fromLocalFile(pdf));
        QCOMPARE(c->pageCount(), 4);
        QCOMPARE(c->appendNewPdfPages(), 0);  // all there

        c->deletePage(1);
        c->deletePage(2);  // the pages 2 and 4 of the PDF are gone
        QCOMPARE(c->pageCount(), 2);
        QCOMPARE(c->appendNewPdfPages(), 2);
        QCOMPARE(c->pageCount(), 4);
        const auto& pages = c->document().pages;
        QCOMPARE(pages[2].background.pdfPage, 1);
        QCOMPARE(pages[3].background.pdfPage, 3);
        QCOMPARE(c->pageSize(3), QSizeF(300, 400));
        QCOMPARE(pages[3].layers.size(), size_t(1));
        // One step of undo for all of them
        c->undo();
        QCOMPARE(c->pageCount(), 2);
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void imageAsBackground() {
        QTemporaryDir dir;
        const QString picture = dir.filePath(QStringLiteral("picture.jpg"));
        QImage image(320, 200, QImage::Format_RGB32);
        image.fill(QColor(0x20, 0x80, 0x40));
        QVERIFY(image.save(picture));

        Fixture f;
        PageCanvas* c = f.canvas;
        c->insertPage(1);
        QVERIFY(c->setPageBackgroundImage(0, QUrl::fromLocalFile(picture)));
        QVERIFY(c->setPageBackgroundImage(1, QUrl::fromLocalFile(picture)));
        // The page takes the size of the image
        QCOMPARE(c->pageSize(0), QSizeF(320, 200));
        const Background& background = c->document().pages[0].background;
        QCOMPARE(background.type, Background::Type::Pixmap);
        QCOMPARE(background.domain, QStringLiteral("attach"));
        QVERIFY(background.filename != c->document().pages[1].background.filename);
        COMPARE_COLOR(f.pixel(f.onPage(0, QPointF(100, 100))), QColor(0x20, 0x80, 0x40));
        QCOMPARE(c->pageProperties(0).value(QStringLiteral("type")).toString(), QStringLiteral("pixmap"));

        QSignalSpy failed(c, &PageCanvas::loadFailed);
        QVERIFY(!c->setPageBackgroundImage(0, QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.png")))));
        QCOMPARE(failed.count(), 1);

        // The image is saved next to the document and found again
        const QString path = dir.filePath(QStringLiteral("doc.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        QVERIFY(QFile::exists(path + u'.' + background.filename));
        Document loaded;
        QVERIFY(loadXopp(path, loaded, nullptr));
        QCOMPARE(loaded.pages[0].background.type, Background::Type::Pixmap);
        QCOMPARE(loaded.pages[0].background.pixmap.size(), QSize(320, 200));
        QCOMPARE(loaded.pages[0].width, 320.0);
        // Saved elsewhere, the images go along
        const QString copy = dir.filePath(QStringLiteral("copy.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(copy)));
        QVERIFY(QFile::exists(copy + u'.' + background.filename));
        QVERIFY(QFile::exists(copy + u'.' + c->document().pages[1].background.filename));

        // Undo gives the paper back
        c->undo();
        c->undo();
        QCOMPARE(c->document().pages[0].background.type, Background::Type::Solid);
        QCOMPARE(c->pageSize(0), QSizeF(595.27559, 841.88976));
    }

    void pairedPagesOffset() {
        Fixture f;
        PageCanvas* c = f.canvas;
        for (int i = 1; i < 4; ++i) {
            c->insertPage(i);
        }
        c->setPairedPages(true);
        QCOMPARE(c->pairedPagesOffset(), 1);
        // The first page alone: the second and third are next to each other
        QCOMPARE(c->pageViewRect(1).top(), c->pageViewRect(2).top());
        QVERIFY(c->pageViewRect(0).top() < c->pageViewRect(1).top());

        QSignalSpy changed(c, &PageCanvas::layoutChanged);
        c->setPairedPagesOffset(0);
        QCOMPARE(changed.count(), 1);
        QCOMPARE(c->pageViewRect(0).top(), c->pageViewRect(1).top());
        QCOMPARE(c->pageViewRect(2).top(), c->pageViewRect(3).top());
        QVERIFY(c->pageViewRect(1).top() < c->pageViewRect(2).top());
    }

    /// Scroll bars: the visible part, and scrolling to a place
    void scrollBars() {
        Fixture f;
        PageCanvas* c = f.canvas;
        for (int i = 1; i < 5; ++i) {
            c->insertPage(i);
        }
        c->zoomTo(1.0);
        QSignalSpy scrolled(c, &PageCanvas::scrollChanged);
        const QRectF view = c->scrollView();
        QVERIFY(view.height() > 0 && view.height() < 0.5);
        c->scrollToFraction(view.x(), 0.5);
        QVERIFY(scrolled.count() >= 1);
        QVERIFY(std::abs(c->scrollView().y() - 0.5) < 0.01);
        QVERIFY(c->currentPage() >= 2);
        c->scrollToFraction(0, 2);  // beyond the end: as far as it goes
        QVERIFY(std::abs(c->scrollView().bottom() - 1) < 0.01);
    }

    /// Space around the pages and unlimited scrolling, as in Xournal++
    void spaceAroundThePages() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->insertPage(1);
        c->zoomTo(1.0);
        c->scrollToFraction(0, 0);
        const QPointF top = c->pageViewRect(0).topLeft();
        c->setSpaceAbove(200);
        c->scrollToFraction(0, 0);
        QVERIFY2(std::abs(c->pageViewRect(0).top() - (top.y() + 200)) < 1,
                 qPrintable(QStringLiteral("%1 %2").arg(c->pageViewRect(0).top()).arg(top.y())));
        c->setSpaceAbove(0);

        // Unlimited: the page can be scrolled until its edge is at the other side of the view
        c->setUnlimitedScrolling(true);
        c->scrollToFraction(0, 0);
        QVERIFY(c->pageViewRect(0).top() > c->height() - 20);
        c->setUnlimitedScrolling(false);
    }

    /// At 100 % with the resolution of the screen set, a page has its real size
    void resolutionOfTheScreen() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->zoomTo(1.0);
        const double width = c->pageViewRect(0).width();
        c->setDisplayDpi(144);
        QCOMPARE(c->zoom(), 1.0);
        QVERIFY(std::abs(c->pageViewRect(0).width() - 2 * width) < 1);
        c->setDisplayDpi(72);
    }

    /// A page is appended when the last one is written on, or when it is scrolled to its end
    void appendedPages() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setAppendPage(PageCanvas::AppendWhenWritten);
        QCOMPARE(c->pageCount(), 1);
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(200, 100)});
        QCOMPARE(c->pageCount(), 2);
        // Not again: the last page is empty
        f.strokeOnPage(0, {QPointF(100, 150), QPointF(200, 150)});
        QCOMPARE(c->pageCount(), 2);
        // Undoing the writing takes the page away again
        c->undo();
        c->undo();
        QCOMPARE(c->pageCount(), 1);

        c->setAppendPage(PageCanvas::AppendWhenScrolledToEnd);
        c->zoomTo(2.0);
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(200, 100)});
        QCOMPARE(c->pageCount(), 1);
        c->scrollToFraction(0, 1);
        QCOMPARE(c->pageCount(), 2);
        c->setAppendPage(PageCanvas::AppendNever);
    }

    void namesOfFiles() {
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("lecture.pdf"));
        writePdf(pdf, 1);
        Fixture f;
        PageCanvas* c = f.canvas;
        QVERIFY(c->annotationFileFor(QUrl::fromLocalFile(pdf)).isEmpty());
        // Xournal++ looks for "lecture.pdf.xopp" first, then "lecture.xopp"
        const auto touch = [](const QString& path) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly);
        };
        QVERIFY(touch(dir.filePath(QStringLiteral("lecture.xopp"))));
        QCOMPARE(c->annotationFileFor(QUrl::fromLocalFile(pdf)),
                 QUrl::fromLocalFile(dir.filePath(QStringLiteral("lecture.xopp"))));
        QVERIFY(touch(dir.filePath(QStringLiteral("lecture.pdf.xopp"))));
        QCOMPARE(c->annotationFileFor(QUrl::fromLocalFile(pdf)),
                 QUrl::fromLocalFile(dir.filePath(QStringLiteral("lecture.pdf.xopp"))));

        const QString today = QDate::currentDate().toString(Qt::ISODate);
        QVERIFY(c->nameFromPattern(QStringLiteral("%F-Note")).startsWith(today));
        QCOMPARE(c->nameFromPattern(QStringLiteral("100%% %Y")),
                 QStringLiteral("100% ") + QString::number(QDate::currentDate().year()));
        // The name of the PDF, without what file names cannot have
#ifdef HAVE_QTPDF
        c->openFile(QUrl::fromLocalFile(pdf));
        QCOMPARE(c->nameFromPattern(QStringLiteral("%{name}_annotated")), QStringLiteral("lecture_annotated"));
#endif
        QCOMPARE(c->nameFromPattern(QStringLiteral("a/b:c")), QStringLiteral("a-b-c"));
    }

    void imageTool() {
        QTemporaryDir dir;
        const QString picture = dir.filePath(QStringLiteral("picture.png"));
        QImage image(60, 40, QImage::Format_RGB32);
        image.fill(QColor(0x20, 0x80, 0x40));
        QVERIFY(image.save(picture));

        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::Image);
        QSignalSpy requested(c, &PageCanvas::imageRequested);
        const QPointF pos = f.onPage(0, QPointF(200, 150));
        f.tap(pos);
        // The tool asks for an image for this place; nothing is drawn
        QCOMPARE(requested.count(), 1);
        QCOMPARE(requested[0][0].toPointF(), pos);
        QVERIFY(c->document().pages[0].layers[0].elements.empty());

        QVERIFY(c->insertImageAt(QUrl::fromLocalFile(picture), pos));
        const auto& elements = c->document().pages[0].layers[0].elements;
        QCOMPARE(elements.size(), size_t(1));
        const QRectF rect = std::get<ImageElement>(elements[0]).rect;
        QVERIFY2(rect.contains(QPointF(200, 150)), qPrintable(QStringLiteral("%1 %2").arg(rect.x()).arg(rect.y())));
    }

    void justifiedText() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::Text);
        QSignalSpy changed(c, &PageCanvas::textStyleChanged);
        c->setTextJustify(true);
        QCOMPARE(changed.count(), 1);
        f.tap(f.onPage(0, QPointF(100, 100)));
        QVERIFY(c->textEditing());
        QVERIFY(c->textEditJustify());
        c->setTextEditText(QStringLiteral("some words that are long enough to wrap"));
        c->finishTextEdit();
        const auto& elements = c->document().pages[0].layers[0].elements;
        QVERIFY(std::get<TextElement>(elements[0]).justify);

        // A selected text takes the setting, undoable
        c->setTool(PageCanvas::SelectObject);
        f.tap(f.onPage(0, QPointF(110, 105)));
        QVERIFY(c->hasSelection());
        c->setTextJustify(false);
        QVERIFY(!std::get<TextElement>(elements[0]).justify);
        c->undo();
        QVERIFY(std::get<TextElement>(elements[0]).justify);
    }

    void positionOfThePointer() {
        Fixture f;
        PageCanvas* c = f.canvas;
        const QPointF pos = f.onPage(0, QPointF(200, 200));
        f.tablet(QEvent::TabletMove, pos, 0);
        COMPARE_COLOR(f.pixel(pos + QPointF(10, 10)), Qt::white);

        c->setHighlightPosition(true);
        f.tablet(QEvent::TabletMove, pos, 0);
        // A translucent yellow disc around the pointer
        const QColor highlighted = f.pixel(pos + QPointF(10, 10));
        QVERIFY2(highlighted.blue() < 160 && highlighted.red() > 240 && highlighted.green() > 240,
                 qPrintable(highlighted.name()));
        COMPARE_COLOR(f.pixel(pos + QPointF(60, 0)), Qt::white);
        // It follows the pointer
        f.tablet(QEvent::TabletMove, pos + QPointF(100, 0), 0);
        COMPARE_COLOR(f.pixel(pos + QPointF(10, 10)), Qt::white);
        QVERIFY(f.pixel(pos + QPointF(100, 0)).blue() < 160);

        c->setHighlightPosition(false);
        COMPARE_COLOR(f.pixel(pos + QPointF(100, 0)), Qt::white);
    }
};

QTEST_MAIN(TestJournal)
#include "tst_journal.moc"
