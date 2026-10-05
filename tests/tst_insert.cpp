/*
 * Qournal
 *
 * Tests for creating and editing texts, images, links and LaTeX formulas on the canvas.
 * They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QPainter>
#include <QPdfWriter>
#include <QScreen>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <cmath>

#include "BuiltinLatex.h"
#include "CanvasFixture.h"
#include "LatexRunner.h"
#include "Renderer.h"
#include "ScreenCapture.h"
#include "TextBlock.h"
#include "XoppLoader.h"

namespace {

bool near(double a, double b, double tolerance = 1e-6) { return std::abs(a - b) <= tolerance; }
bool near(const QPointF& a, const QPointF& b, double tolerance = 1e-6) {
    return near(a.x(), b.x(), tolerance) && near(a.y(), b.y(), tolerance);
}

/// The elements of the first layer of a page
const std::vector<Element>& elements(const Fixture& f, int page = 0) {
    return f.canvas->document().pages[static_cast<size_t>(page)].layers[0].elements;
}

/// Number of pixels of an area of the page that are not white
int inkIn(Fixture& f, const QRectF& pageRect) {
    const QImage image = f.grab();
    const QRect rect =
            QRectF(f.onPage(0, pageRect.topLeft()), pageRect.size() * f.canvas->zoom()).toAlignedRect() & image.rect();
    int count = 0;
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        for (int x = rect.left(); x <= rect.right(); ++x) {
            count += qGray(image.pixel(x, y)) < 200;
        }
    }
    return count;
}

/// A PDF with one page of the given size in points, like the ones LaTeX makes of a formula
QByteArray formulaPdf(const QSizeF& size, const QString& path) {
    {
        QPdfWriter writer(path);
        writer.setResolution(72);
        writer.setPageSize(QPageSize(size, QPageSize::Point));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        QPainter p(&writer);
        p.fillRect(QRectF(QPointF(2, 2), size - QSizeF(4, 4)), Qt::black);
    }
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}  // namespace

class TestInsert: public QObject {
    Q_OBJECT

private slots:
    void writeText() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy textEditChanged(c, &PageCanvas::textEditChanged);
        c->setTool(PageCanvas::Text);
        QVERIFY(!c->textEditing());

        // A press with the text tool starts a text there
        f.tap(f.onPage(0, QPointF(100, 100)));
        QVERIFY(c->textEditing());
        QVERIFY(textEditChanged.count() >= 1);
        QCOMPARE(c->textEditText(), QString());
        QVERIFY(elements(f).empty());
        QVERIFY(!c->canUndo());

        // The editor is told where the text is: its origin is at the top left of the first line
        const double lineHeight = TextBlock(QString(), TextStyle{QStringLiteral("Sans"), 12}).lineHeight();
        const QPointF origin = c->textEditMatrix().map(QPointF(0, 0));
        QVERIFY(near(origin, f.onPage(0, QPointF(100, 100 - lineHeight / 2)), 1e-3));
        // It lays out the text at 100 pixels; the matrix scales that to the size on the screen
        QCOMPARE(c->textEditFont().pixelSize(), 100);
        QVERIFY(near(c->textEditMatrix().map(QPointF(100, 0)).x() - origin.x(), 12 * c->zoom(), 1e-3));
        QVERIFY(near(c->textEditLineHeight(), lineHeight * 100 / 12));
        QCOMPARE(c->textEditWrap(), -1.0);
        QVERIFY(c->textEditViewRect().contains(origin));

        // What the editor reports is kept when the editing ends
        c->setTextEditText(QStringLiteral("Hello\nworld"));
        c->finishTextEdit();
        QVERIFY(!c->textEditing());
        QCOMPARE(elements(f).size(), size_t(1));
        const auto& text = std::get<TextElement>(elements(f)[0]);
        QCOMPARE(text.text, QStringLiteral("Hello\nworld"));
        QCOMPARE(text.font, QStringLiteral("Sans"));
        QCOMPARE(text.size, 12.0);
        QCOMPARE(text.color, QColor(Qt::black));
        QVERIFY(near(text.pos, QPointF(100, 100 - lineHeight / 2)));
        QVERIFY(!text.matrix.has_value());
        QVERIFY(c->modified());

        // It is on the page: two lines
        const QRectF bounds = Renderer::elementBounds(elements(f)[0]);
        QCOMPARE(bounds.height(), 2 * lineHeight);
        QVERIFY(inkIn(f, bounds) > 50);
        QCOMPARE(inkIn(f, bounds.translated(0, 3 * lineHeight)), 0);

        c->undo();
        QVERIFY(elements(f).empty());
        QCOMPARE(inkIn(f, bounds), 0);
        c->redo();
        QCOMPARE(elements(f).size(), size_t(1));

        // A text without content is not created
        f.tap(f.onPage(0, QPointF(100, 300)));
        QVERIFY(c->textEditing());
        c->finishTextEdit();
        QCOMPARE(elements(f).size(), size_t(1));
        c->finishTextEdit();  // nothing to finish
    }

    void editText() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(0, QPointF(100, 100)));
        c->setTextEditText(QStringLiteral("First version"));
        c->finishTextEdit();
        const QRectF bounds = Renderer::elementBounds(elements(f)[0]);

        // A press on the text edits it; the editor shows it instead of the page
        f.tap(f.onPage(0, bounds.center()));
        QVERIFY(c->textEditing());
        QCOMPARE(c->textEditText(), QStringLiteral("First version"));
        QCOMPARE(inkIn(f, bounds), 0);
        QCOMPARE(elements(f).size(), size_t(1));

        // A press somewhere else ends the editing, and starts a new text there
        c->setTextEditText(QStringLiteral("Second version"));
        f.tap(f.onPage(0, QPointF(300, 400)));
        QCOMPARE(std::get<TextElement>(elements(f)[0]).text, QStringLiteral("Second version"));
        QVERIFY(c->textEditing());
        QCOMPARE(c->textEditText(), QString());
        c->finishTextEdit();
        QCOMPARE(elements(f).size(), size_t(1));
        QVERIFY(inkIn(f, bounds) > 20);

        c->undo();
        QCOMPARE(std::get<TextElement>(elements(f)[0]).text, QStringLiteral("First version"));
        c->redo();

        // Without a change there is nothing to undo
        f.tap(f.onPage(0, bounds.center()));
        c->finishTextEdit();
        c->undo();
        QCOMPARE(std::get<TextElement>(elements(f)[0]).text, QStringLiteral("First version"));
        c->redo();
        QVERIFY(inkIn(f, bounds) > 20);

        // Changing the tool and saving end the editing as well
        f.tap(f.onPage(0, bounds.center()));
        c->setTextEditText(QStringLiteral("Third"));
        c->setTool(PageCanvas::Pen);
        QVERIFY(!c->textEditing());
        QCOMPARE(std::get<TextElement>(elements(f)[0]).text, QStringLiteral("Third"));
        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(0, QPointF(100, 100)));
        c->setTextEditText(QStringLiteral("Fourth"));
        QTemporaryDir dir;
        QVERIFY(c->saveAs(QUrl::fromLocalFile(dir.filePath(QStringLiteral("text.xopp")))));
        Document reloaded;
        QVERIFY(loadXopp(dir.filePath(QStringLiteral("text.xopp")), reloaded, nullptr));
        QCOMPARE(std::get<TextElement>(reloaded.pages[0].layers[0].elements[0]).text, QStringLiteral("Fourth"));

        // Emptying a text removes it
        f.tap(f.onPage(0, QPointF(100, 100)));
        QVERIFY(c->textEditing());
        c->setTextEditText(QString());
        c->finishTextEdit();
        QVERIFY(elements(f).empty());
        c->undo();
        QCOMPARE(elements(f).size(), size_t(1));
    }

    void textStyle() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy textStyleChanged(c, &PageCanvas::textStyleChanged);
        c->setTool(PageCanvas::Text);

        // The style of new texts
        c->setTextSize(20);
        c->setTextBold(true);
        c->setTextAlign(QStringLiteral("center"));
        c->setColor(Qt::red);
        QCOMPARE(textStyleChanged.count(), 3);
        QCOMPARE(c->textSize(), 20.0);
        QVERIFY(c->textBold());
        QVERIFY(!c->textItalic());
        QCOMPARE(c->textFamily(), QStringLiteral("Sans"));

        f.tap(f.onPage(0, QPointF(100, 100)));
        QCOMPARE(c->textEditColor(), QColor(Qt::red));
        QCOMPARE(c->textEditAlign(), QStringLiteral("center"));
        QVERIFY(c->textEditFont().bold());
        c->setTextEditText(QStringLiteral("Styled"));

        // While editing, changes of the style change the text
        c->setTextItalic(true);
        c->setTextFamily(QStringLiteral("Serif"));
        c->setTextSize(30);
        c->setColor(Qt::blue);
        QVERIFY(c->textEditFont().italic());
        QCOMPARE(c->textEditFont().families().value(0), QStringLiteral("Serif"));
        // The width at which the lines wrap: 500 units of the editor are 150 pt at this size
        c->setTextEditWrap(500);
        QCOMPARE(c->textEditWrap(), 500.0);
        c->finishTextEdit();

        const auto& text = std::get<TextElement>(elements(f)[0]);
        QCOMPARE(text.font, QStringLiteral("Serif Bold Italic"));
        QCOMPARE(text.size, 30.0);
        QCOMPARE(text.align, QStringLiteral("center"));
        QCOMPARE(text.color, QColor(Qt::blue));
        QVERIFY(near(text.wrap, 150));
        const QRectF bounds = Renderer::elementBounds(elements(f)[0]);
        QVERIFY(near(bounds.width(), 150));
        QVERIFY(inkIn(f, bounds) > 50);

        // The style of a selected text can be changed as well, in steps that can be undone
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, bounds.center()));
        QVERIFY(c->hasSelection());
        c->setTextSize(12);
        c->setTextBold(false);
        c->setTextAlign(QStringLiteral("right"));
        QCOMPARE(text.size, 12.0);
        QCOMPARE(text.font, QStringLiteral("Serif Italic"));
        QCOMPARE(text.align, QStringLiteral("right"));
        c->undo();
        c->undo();
        c->undo();
        QCOMPARE(text.size, 30.0);
        QCOMPARE(text.font, QStringLiteral("Serif Bold Italic"));

        // Strokes are not affected, and a selection without texts adds no step
        c->setTool(PageCanvas::Pen);
        f.strokeOnPage(0, {{100, 400}, {200, 400}});
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, QPointF(150, 400)));
        QVERIFY(c->hasSelection());
        c->setTextSize(40);
        c->undo();
        QCOMPARE(elements(f).size(), size_t(1));
    }

    void editTransformedText() {
        Fixture f;
        PageCanvas* c = f.canvas;
        c->openFile(dataFile(QStringLiteral("text-fileversion-5.xopp")));
        const Document& doc = c->document();
        // The text with a matrix that rotates and scales it
        int index = -1;
        for (size_t i = 0; i < doc.pages[0].layers[0].elements.size(); ++i) {
            const auto* text = std::get_if<TextElement>(&doc.pages[0].layers[0].elements[i]);
            if (text && text->matrix && (*text->matrix)[1] != 0) {
                index = static_cast<int>(i);
            }
        }
        QVERIFY(index >= 0);
        const TextElement before = std::get<TextElement>(doc.pages[0].layers[0].elements[static_cast<size_t>(index)]);
        const QRectF bounds = Renderer::elementBounds(doc.pages[0].layers[0].elements[static_cast<size_t>(index)]);

        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(0, bounds.center()));
        QVERIFY(c->textEditing());
        QCOMPARE(c->textEditText(), before.text);
        // The editor is turned like the text
        const QPointF origin = c->textEditMatrix().map(QPointF(0, 0));
        const QPointF right = c->textEditMatrix().map(QPointF(100, 0));
        QVERIFY(near(origin, f.onPage(0, QPointF((*before.matrix)[4], (*before.matrix)[5])), 1e-3));
        QVERIFY(std::abs(right.y() - origin.y()) > 1);

        c->setTextEditText(QStringLiteral("changed"));
        c->finishTextEdit();
        const auto& after = std::get<TextElement>(doc.pages[0].layers[0].elements[static_cast<size_t>(index)]);
        QCOMPARE(after.text, QStringLiteral("changed"));
        QVERIFY(after.matrix == before.matrix);
    }

    void builtinFormulas() {
        // Without LaTeX the application sets formulas itself
        if (!BuiltinLatex::available()) {
            QSKIP("Built without MicroTeX");
        }
#ifdef HAVE_QTPDF
        QString error;
        const QByteArray fraction =
                BuiltinLatex::render(QStringLiteral("\\frac{a}{b} + \\sqrt{x^2+1} = \\int_0^\\infty e^{-x^2}\\,dx"),
                                     QColor(0, 0, 160), &error);
        QVERIFY2(fraction.startsWith("%PDF"), qPrintable(error));
        // An environment that takes the whole line is not wider than what it shows
        const QByteArray aligned = BuiltinLatex::render(
                QStringLiteral("\\begin{align} a &= b + c \\\\ \\sum_{i=1}^n i &= \\frac{n(n+1)}{2} \\end{align}"),
                Qt::black, &error);
        QVERIFY2(aligned.startsWith("%PDF"), qPrintable(error));
        // Glyphs are outlines: the PDF has no fonts, which a reader would have to know
        QVERIFY(!fraction.contains("/FontFile"));

        Fixture f;
        PageCanvas* c = f.canvas;
        c->setTool(PageCanvas::Latex);
        f.tap(f.onPage(0, QPointF(100, 100)));
        c->applyLatex(QStringLiteral("\\frac{a}{b}"), fraction);
        QCOMPARE(elements(f).size(), size_t(1));
        const auto& formula = std::get<ImageElement>(elements(f)[0]);
        QVERIFY(formula.tex);
        // 10 pt, with a border of 5 pt: about what LaTeX makes of it
        QVERIFY2(formula.rect.width() > 110 && formula.rect.width() < 150,
                 qPrintable(QString::number(formula.rect.width())));
        QVERIFY2(formula.rect.height() > 28 && formula.rect.height() < 40,
                 qPrintable(QString::number(formula.rect.height())));
        QVERIFY(inkIn(f, formula.rect) > 150);
        // Nothing in the border
        QCOMPARE(inkIn(f, QRectF(formula.rect.topLeft(), QSizeF(formula.rect.width(), 3))), 0);
        QCOMPARE(inkIn(f, QRectF(formula.rect.topLeft(), QSizeF(3, formula.rect.height()))), 0);
        c->undo();
        f.tap(f.onPage(0, QPointF(100, 100)));
        c->applyLatex(QStringLiteral("align"), aligned);
        const auto& lines = std::get<ImageElement>(elements(f)[0]);
        QVERIFY2(lines.rect.width() < 120, qPrintable(QString::number(lines.rect.width())));

        // What is not a formula
        QVERIFY(BuiltinLatex::render(QStringLiteral("  "), Qt::black, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        error.clear();
        QVERIFY(BuiltinLatex::render(QStringLiteral("\\nosuchcommand{x}"), Qt::black, &error).isEmpty());
        QVERIFY(!error.isEmpty());

        // The runner uses it when the program of LaTeX is not installed
        LatexRunner runner;
        QSignalSpy finished(&runner, &LatexRunner::finished);
        QSignalSpy failed(&runner, &LatexRunner::failed);
        runner.setCommand(QStringLiteral("no-such-latex-program '{}'"));
        QVERIFY(runner.available() && runner.builtin() && !runner.installed());
        runner.run(QStringLiteral("x^2"), Qt::black);
        QCOMPARE(finished.count(), 0);  // answered after the call, as LaTeX does
        QVERIFY(finished.wait(1000));
        QVERIFY(finished[0][0].toByteArray().startsWith("%PDF"));
        runner.run(QStringLiteral("\\nosuchcommand"), Qt::black);
        QVERIFY(failed.wait(1000));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void regionOfTheScreen() {
        // The screens as they were are shown, and what the user drags over goes onto the page
        Fixture f;
        PageCanvas* c = f.canvas;
        ScreenCapture capture;
        QSignalSpy captured(&capture, &ScreenCapture::captured);
        QScreen* screen = QGuiApplication::primaryScreen();
        // A picture with twice the pixels of the screen, as on a screen of high density: left red, right blue
        QImage shot(screen->size() * 2, QImage::Format_RGB32);
        shot.fill(Qt::red);
        {
            QPainter painter(&shot);
            painter.fillRect(QRect(shot.width() / 2, 0, shot.width() / 2, shot.height()), Qt::blue);
        }
        capture.showSelection({shot});
        QVERIFY(capture.running());
        QCOMPARE(capture.selectionWindows().size(), 1);
        QWindow* selection = capture.selectionWindows().first();
        QVERIFY(QTest::qWaitForWindowExposed(selection));

        // A click selects nothing; Esc ends it without a picture
        QTest::mouseClick(selection, Qt::LeftButton, {}, QPoint(50, 50));
        QVERIFY(capture.running());
        QTest::keyClick(selection, Qt::Key_Escape);
        QVERIFY(!capture.running());
        QVERIFY(capture.selectionWindows().isEmpty());
        QCOMPARE(captured.size(), 0);

        capture.showSelection({shot});
        selection = capture.selectionWindows().first();
        QVERIFY(QTest::qWaitForWindowExposed(selection));
        const int middle = selection->width() / 2;
        QTest::mousePress(selection, Qt::LeftButton, {}, QPoint(middle + 60, 140));
        QTest::mouseMove(selection, QPoint(middle, 100));
        QTest::mouseMove(selection, QPoint(middle - 40, 40));
        QTest::mouseRelease(selection, Qt::LeftButton, {}, QPoint(middle - 40, 40));
        QVERIFY(!capture.running());
        QCOMPARE(captured.size(), 1);
        const QImage image = captured.first().first().value<QImage>();
        QCOMPARE(image.size(), QSize(200, 200));
        QCOMPARE(image.devicePixelRatio(), 2.0);
        QCOMPARE(image.pixelColor(10, 100), QColor(Qt::red));
        QCOMPARE(image.pixelColor(190, 100), QColor(Qt::blue));

        // Before the screens are captured a bar waits, so that the user can bring another window to the front;
        // the window of the application is hidden meanwhile and comes back when the capture is given up
        f.window.show();
        capture.start(&f.window);
        QVERIFY(capture.running());
        QVERIFY(capture.prepareWindow() && capture.prepareWindow()->isVisible());
        QVERIFY(!f.window.isVisible());
        QVERIFY(capture.selectionWindows().isEmpty());
        QTest::keyClick(capture.prepareWindow(), Qt::Key_Escape);
        QVERIFY(!capture.running());
        QVERIFY(!capture.prepareWindow());
        QVERIFY(f.window.isVisible());

        // On the page it has the size it had on the screen, and is saved as a PNG
        QVERIFY(c->insertPicture(image));
        QCOMPARE(elements(f).size(), size_t(1));
        const auto& element = std::get<ImageElement>(elements(f)[0]);
        QCOMPARE(element.rect.size(), QSizeF(100, 100));
        QVERIFY(element.data.startsWith("\x89PNG"));
        QVERIFY(c->hasSelection());
        QVERIFY(!c->insertPicture(QImage()));
    }

    void insertImages() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy loadFailed(c, &PageCanvas::loadFailed);
        QTemporaryDir dir;
        QImage picture(200, 100, QImage::Format_RGB32);
        picture.fill(Qt::darkGreen);
        const QString png = dir.filePath(QStringLiteral("picture.png"));
        const QString jpeg = dir.filePath(QStringLiteral("picture.jpg"));
        const QString bmp = dir.filePath(QStringLiteral("picture.bmp"));
        QVERIFY(picture.save(png) && picture.save(jpeg) && picture.save(bmp));

        // In the middle of the view, selected; the file is embedded as it is
        QVERIFY(c->insertImage(QUrl::fromLocalFile(png)));
        QCOMPARE(elements(f).size(), size_t(1));
        QVERIFY(c->hasSelection());
        const auto* image = std::get_if<ImageElement>(&elements(f)[0]);
        QVERIFY(image);
        QFile file(png);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(image->data, file.readAll());
        QCOMPARE(image->rect.size(), QSizeF(200, 100));
        COMPARE_COLOR(f.pixel(f.onPage(0, image->rect.center())), QColor(Qt::darkGreen));

        // At a position: its middle is there
        QVERIFY(c->insertImageAt(QUrl::fromLocalFile(jpeg), f.onPage(0, QPointF(150, 350))));
        const auto* second = std::get_if<ImageElement>(&elements(f)[1]);
        QVERIFY(second && near(second->rect.center(), QPointF(150, 350)));
        QVERIFY(second->data.startsWith("\xff\xd8"));

        // Other formats are stored as PNG, which every version of Xournal++ reads
        QVERIFY(c->insertImage(QUrl::fromLocalFile(bmp)));
        QVERIFY(std::get<ImageElement>(elements(f)[2]).data.startsWith("\x89PNG"));

        // What is not an image is refused
        QVERIFY(!c->insertImage(dataFile(QStringLiteral("strokes.xopp"))));
        QVERIFY(!c->insertImage(QUrl::fromLocalFile(dir.filePath(QStringLiteral("missing.png")))));
        QCOMPARE(loadFailed.count(), 2);
        QCOMPARE(elements(f).size(), size_t(3));

        // The images are in the file
        const QString path = dir.filePath(QStringLiteral("images.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document reloaded;
        QVERIFY(loadXopp(path, reloaded, nullptr));
        QCOMPARE(reloaded.pages[0].layers[0].elements.size(), size_t(3));
        QCOMPARE(std::get<ImageElement>(reloaded.pages[0].layers[0].elements[1]).image.size(), QSize(200, 100));

        // A dropped text
        c->insertTextAt(QStringLiteral("dropped"), f.onPage(0, QPointF(400, 100)));
        const auto* text = std::get_if<TextElement>(&elements(f)[3]);
        QVERIFY(text && text->text == QStringLiteral("dropped"));
        QVERIFY(near(Renderer::elementBounds(elements(f)[3]).center(), QPointF(400, 100)));
        for (int i = 0; i < 4; ++i) {
            c->undo();
        }
        QVERIFY(elements(f).empty());
    }

    void links() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy linkRequested(c, &PageCanvas::linkRequested);
        QSignalSpy openLinkRequested(c, &PageCanvas::openLinkRequested);
        c->setTool(PageCanvas::Link);

        // On an empty place: a new link
        f.tap(f.onPage(0, QPointF(100, 100)));
        QCOMPARE(linkRequested.count(), 1);
        QCOMPARE(linkRequested[0][2].toBool(), false);
        c->applyLink(QStringLiteral("Xournal++"), QStringLiteral(" https://xournalpp.github.io "));
        QCOMPARE(elements(f).size(), size_t(1));
        const auto& link = std::get<LinkElement>(elements(f)[0]);
        QCOMPARE(link.text, QStringLiteral("Xournal++"));
        QCOMPARE(link.url, QStringLiteral("https://xournalpp.github.io"));
        QCOMPARE(link.matrix, (Matrix{1, 0, 0, 1, 100, 100}));
        const QRectF bounds = Renderer::elementBounds(elements(f)[0]);
        QVERIFY(inkIn(f, bounds) > 20);

        // On the link: it can be changed
        f.tap(f.onPage(0, bounds.center()));
        QCOMPARE(linkRequested.count(), 2);
        QCOMPARE(linkRequested[1][0].toString(), QStringLiteral("Xournal++"));
        QCOMPARE(linkRequested[1][1].toString(), QStringLiteral("https://xournalpp.github.io"));
        QCOMPARE(linkRequested[1][2].toBool(), true);
        // Without a text the address is shown
        c->applyLink(QString(), QStringLiteral("https://example.org"));
        QCOMPARE(link.text, QStringLiteral("https://example.org"));
        QCOMPARE(elements(f).size(), size_t(1));
        c->undo();
        QCOMPARE(link.text, QStringLiteral("Xournal++"));

        // A tap with the hand opens it; dragging does not
        c->setTool(PageCanvas::Hand);
        f.tap(f.onPage(0, bounds.center()));
        QCOMPARE(openLinkRequested.count(), 1);
        QCOMPARE(openLinkRequested[0][0].toString(), QStringLiteral("https://xournalpp.github.io"));
        f.stroke(f.onPage(0, bounds.center()), f.onPage(0, bounds.center()) + QPointF(0, -40));
        f.stroke(f.onPage(0, bounds.center()), f.onPage(0, bounds.center()) + QPointF(0, 40));
        f.tap(f.onPage(0, QPointF(300, 300)));
        QCOMPARE(openLinkRequested.count(), 1);

        // Removing, and what does nothing: no address, or no request before
        c->setTool(PageCanvas::Link);
        f.tap(f.onPage(0, QPointF(300, 300)));
        c->applyLink(QStringLiteral("text"), QStringLiteral("  "));
        c->applyLink(QStringLiteral("text"), QStringLiteral("https://example.org"));
        c->removeLink();
        QCOMPARE(elements(f).size(), size_t(1));
        f.tap(f.onPage(0, Renderer::elementBounds(elements(f)[0]).center()));
        c->removeLink();
        QVERIFY(elements(f).empty());
        c->undo();
        QCOMPARE(elements(f).size(), size_t(1));
    }

    void formulas() {
#ifdef HAVE_QTPDF
        Fixture f;
        PageCanvas* c = f.canvas;
        QSignalSpy latexRequested(c, &PageCanvas::latexRequested);
        QSignalSpy loadFailed(c, &PageCanvas::loadFailed);
        QTemporaryDir dir;
        const QByteArray pdf = formulaPdf(QSizeF(80, 30), dir.filePath(QStringLiteral("a.pdf")));
        const QByteArray wider = formulaPdf(QSizeF(120, 30), dir.filePath(QStringLiteral("b.pdf")));
        QVERIFY(pdf.startsWith("%PDF"));

        // On an empty place: a new formula, with the example of Xournal++ to start from
        c->setTool(PageCanvas::Latex);
        f.tap(f.onPage(0, QPointF(100, 100)));
        QCOMPARE(latexRequested.count(), 1);
        QCOMPARE(latexRequested[0][0].toString(), QStringLiteral("x^2"));
        QCOMPARE(latexRequested[0][1].toBool(), false);
        c->applyLatex(QStringLiteral("a^2+b^2"), pdf);
        QCOMPARE(elements(f).size(), size_t(1));
        const auto& formula = std::get<ImageElement>(elements(f)[0]);
        QVERIFY(formula.tex);
        QCOMPARE(formula.texSource, QStringLiteral("a^2+b^2"));
        QCOMPARE(formula.data, pdf);
        QVERIFY(near(formula.rect.topLeft(), QPointF(100, 100)));
        QVERIFY(near(formula.rect.width(), 80, 0.01) && near(formula.rect.height(), 30, 0.01));
        COMPARE_COLOR(f.pixel(f.onPage(0, formula.rect.center())), Qt::black);

        // Scaled to twice the size with the selection
        c->setTool(PageCanvas::SelectRect);
        f.tap(f.onPage(0, formula.rect.center()));
        c->input()->setProperty("snapGrid", false);
        f.stroke(f.onPage(0, formula.rect.bottomRight()), f.onPage(0, formula.rect.bottomRight() + QPointF(80, 30)));
        QVERIFY(near(formula.rect.width(), 160, 0.01));

        // On the formula: its source can be changed; the new formula keeps place and scale
        c->setTool(PageCanvas::Latex);
        f.tap(f.onPage(0, formula.rect.center()));
        QCOMPARE(latexRequested.count(), 2);
        QCOMPARE(latexRequested[1][0].toString(), QStringLiteral("a^2+b^2"));
        QCOMPARE(latexRequested[1][1].toBool(), true);
        c->applyLatex(QStringLiteral("c^2"), wider);
        QCOMPARE(elements(f).size(), size_t(1));
        QCOMPARE(formula.texSource, QStringLiteral("c^2"));
        QVERIFY(near(formula.rect.topLeft(), QPointF(100, 100)));
        QVERIFY(near(formula.rect.width(), 240, 0.01) && near(formula.rect.height(), 60, 0.01));
        c->undo();
        QCOMPARE(formula.texSource, QStringLiteral("a^2+b^2"));

        // The formula is in the file, with its source
        const QString path = dir.filePath(QStringLiteral("formula.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document reloaded;
        QVERIFY(loadXopp(path, reloaded, nullptr));
        const auto& saved = std::get<ImageElement>(reloaded.pages[0].layers[0].elements[0]);
        QVERIFY(saved.tex && saved.texSource == QStringLiteral("a^2+b^2") && saved.data == pdf);

        // What is not a PDF is refused; an ordinary image is not a formula
        f.tap(f.onPage(0, QPointF(300, 400)));
        c->applyLatex(QStringLiteral("d"), QByteArray("not a pdf"));
        QCOMPARE(loadFailed.count(), 1);
        QCOMPARE(elements(f).size(), size_t(1));
        c->applyLatex(QStringLiteral("d"), pdf);  // no request pending any more
        QCOMPARE(elements(f).size(), size_t(1));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void latexRunner() {
#if defined(Q_OS_UNIX) && QT_CONFIG(process)
        if (QStandardPaths::findExecutable(QStringLiteral("sh")).isEmpty()) {
            QSKIP("No shell to stand in for LaTeX");
        }
        QTemporaryDir dir;
        const QString fixture = dir.filePath(QStringLiteral("formula.pdf"));
        const QByteArray pdf = formulaPdf(QSizeF(80, 30), fixture);

        // A command that behaves like LaTeX: it finds the .tex file and writes tex.pdf next to it
        LatexRunner runner;
        QSignalSpy finished(&runner, &LatexRunner::finished);
        QSignalSpy failed(&runner, &LatexRunner::failed);
        QVERIFY(runner.command().startsWith(QStringLiteral("pdflatex")));
        runner.setCommand(QStringLiteral("sh -c \"grep -q 'x\\^2' '{}' && cp '%1' tex.pdf\"").arg(fixture));
        QVERIFY(runner.available());
        runner.run(QStringLiteral("x^2"), Qt::black);
        QVERIFY(runner.running());
        QVERIFY(finished.wait(10000));
        QVERIFY(!runner.running());
        QCOMPARE(finished[0][0].toByteArray(), pdf);
        QCOMPARE(failed.count(), 0);

        // An error of LaTeX is reported with its output
        runner.setCommand(QStringLiteral("sh -c \"echo Undefined control sequence; exit 1\""));
        runner.run(QStringLiteral("\\oops"), Qt::black);
        QVERIFY(failed.wait(10000));
        QVERIFY(failed[0][0].toString().contains(QStringLiteral("Undefined control sequence")));

        // No PDF although the command succeeded
        runner.setCommand(QStringLiteral("sh -c true"));
        runner.run(QStringLiteral("x"), Qt::black);
        QVERIFY(failed.wait(10000));
        QCOMPARE(failed.count(), 2);

        // A template of one's own: the formula goes where the template says
        const QString templatePath = dir.filePath(QStringLiteral("template.tex"));
        {
            QFile file(templatePath);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("MYTEMPLATE %%XPP_TOOL_INPUT%% END");
        }
        QSignalSpy templateChanged(&runner, &LatexRunner::templateFileChanged);
        runner.setTemplateFile(QUrl::fromLocalFile(templatePath).toString());  // as a file dialog gives it
        QCOMPARE(runner.templateFile(), templatePath);
        QCOMPARE(templateChanged.count(), 1);
        runner.setCommand(
                QStringLiteral("sh -c \"grep -q 'MYTEMPLATE a+b END' '{}' && cp '%1' tex.pdf\"").arg(fixture));
        runner.run(QStringLiteral("a+b"), Qt::black);
        QVERIFY(finished.wait(10000));
        QCOMPARE(finished.count(), 2);
        // One that cannot be read is reported
        runner.setTemplateFile(dir.filePath(QStringLiteral("missing.tex")));
        runner.run(QStringLiteral("a+b"), Qt::black);
        QCOMPARE(failed.count(), 3);
        QVERIFY(failed[2][0].toString().contains(QStringLiteral("missing.tex")));
        runner.setTemplateFile(QString());

        // A program that is not installed: without the formulas of the application itself (see builtinFormulas)
        // nothing can render
        runner.setCommand(QStringLiteral("no-such-latex-program '{}'"));
        QVERIFY(!runner.installed());
        if (!BuiltinLatex::available()) {
            QVERIFY(!runner.available());
            runner.run(QStringLiteral("x"), Qt::black);
            QCOMPARE(failed.count(), 4);
            QVERIFY(failed[3][0].toString().contains(QStringLiteral("no-such-latex-program")));
            QCOMPARE(finished.count(), 2);
        }
#else
        QSKIP("Needs a shell");
#endif
    }
};

QTEST_MAIN(TestInsert)
#include "tst_insert.moc"
