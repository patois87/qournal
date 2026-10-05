/*
 * Qournal
 *
 * Tests for the export: ranges of pages and layers, PDF, PNG and SVG files
 *
 * @license GNU GPLv2 or later
 */

#include <QFile>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTest>
#ifdef HAVE_QTSVG
#include <QSvgRenderer>
#endif

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

#include "Document.h"
#include "Export.h"
#include "PdfFile.h"
#include "XoppLoader.h"

namespace {

const QColor RED(0xd0, 0x10, 0x10);
const QColor GREEN(0x10, 0xa0, 0x10);
const QColor BLUE(0x10, 0x10, 0xd0);

Stroke line(const QColor& color, double y) {
    Stroke s;
    s.color = color;
    s.width = 20;
    s.points = {QPointF(50, y), QPointF(250, y)};
    s.updateBounds();
    return s;
}

Layer layerWith(const QColor& color, double y) {
    Layer layer;
    layer.elements.push_back(line(color, y));
    return layer;
}

/// Two pages. The first has three layers with a line each (red at y = 100, green at 200, blue at 300), of which the
/// green one is hidden; the second is smaller, lined, and has a red line at y = 100
Document testDocument() {
    Document doc;
    Page first;
    first.width = 400;
    first.height = 500;
    first.layers = {layerWith(RED, 100), layerWith(GREEN, 200), layerWith(BLUE, 300)};
    first.layers[1].visible = false;
    Page second;
    second.width = 300;
    second.height = 200;
    second.background.style = QStringLiteral("lined");
    second.layers = {layerWith(RED, 100)};
    doc.pages = {first, second};
    return doc;
}

bool similar(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) < 24 && std::abs(a.green() - b.green()) < 24 &&
           std::abs(a.blue() - b.blue()) < 24 && std::abs(a.alpha() - b.alpha()) < 24;
}

/// A PDF file with these objects, numbered from 1; the first is the catalog
QByteArray makePdf(const QList<QByteArray>& objects) {
    QByteArray file = "%PDF-1.7\n";
    QList<qsizetype> offsets;
    for (qsizetype i = 0; i < objects.size(); ++i) {
        offsets.append(file.size());
        file += QByteArray::number(i + 1) + " 0 obj\n" + objects[i] + "\nendobj\n";
    }
    const qsizetype xref = file.size();
    file += "xref\n0 " + QByteArray::number(objects.size() + 1) + "\n0000000000 65535 f \n";
    for (qsizetype offset: offsets) {
        file += QByteArray::number(offset).rightJustified(10, '0') + " 00000 n \n";
    }
    return file + "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
           QByteArray::number(xref) + "\n%%EOF\n";
}

QByteArray stream(const QByteArray& content, const QByteArray& extra = {}) {
    return "<< /Length " + QByteArray::number(content.size()) + extra + " >>\nstream\n" + content + "\nendstream";
}

QList<int> range(const QString& text, int count) {
    QList<int> result;
    return ElementRange::parse(text, count, result) ? result : QList<int>{-1};
}

}  // namespace

class TestExport: public QObject {
    Q_OBJECT

private slots:
    void ranges() {
        QCOMPARE(range(QStringLiteral("1-3,5,7-"), 8), (QList<int>{0, 1, 2, 4, 6, 7}));
        QCOMPARE(range(QStringLiteral("-2"), 8), (QList<int>{0, 1}));
        QCOMPARE(range(QStringLiteral("-"), 3), (QList<int>{0, 1, 2}));
        QCOMPARE(range(QStringLiteral(" 2 ; 1 : 2 - 3 "), 3), (QList<int>{1, 0, 1, 2}));
        QCOMPARE(range(QStringLiteral("3"), 3), (QList<int>{2}));

        for (const char* wrong: {"0", "4", "2-4", "3-1", "a", "", "1,,2", "1-2-3", "1.5"}) {
            QList<int> result{42};
            QString error;
            QVERIFY2(!ElementRange::parse(QString::fromLatin1(wrong), 3, result, &error), wrong);
            QVERIFY2(!error.isEmpty(), wrong);
            QCOMPARE(result, QList<int>{42});  // untouched
        }
    }

    void sheets() {
        const Document doc = testDocument();
        auto visible = [](const Export::Sheet& sheet) {
            QList<bool> result;
            for (const Layer& layer: sheet.content.layers) {
                result.append(layer.visible);
            }
            return result;
        };

        // As shown: the hidden layer stays hidden
        ExportOptions options;
        auto sheets = Export::sheets(doc, options);
        QCOMPARE(sheets.size(), size_t(2));
        QCOMPARE(sheets[0].page, 0);
        QCOMPARE(visible(sheets[0]), (QList<bool>{true, false, true}));
        QCOMPARE(sheets[1].page, 1);

        // A range of layers replaces the visibility; pages in the given order
        options.pages = {1, 0};
        options.layers = {0, 1};
        sheets = Export::sheets(doc, options);
        QCOMPARE(sheets.size(), size_t(2));
        QCOMPARE(sheets[0].page, 1);
        QCOMPARE(visible(sheets[0]), (QList<bool>{true}));
        QCOMPARE(visible(sheets[1]), (QList<bool>{true, true, false}));

        // Progressively: one sheet per shown layer
        options = ExportOptions();
        options.progressiveLayers = true;
        sheets = Export::sheets(doc, options);
        QCOMPARE(sheets.size(), size_t(3));
        QCOMPARE(visible(sheets[0]), (QList<bool>{true, false, false}));
        QCOMPARE(visible(sheets[1]), (QList<bool>{true, false, true}));
        QCOMPARE(sheets[2].page, 1);
    }

    void png() {
        QTemporaryDir dir;
        const Document doc = testDocument();
        ExportOptions options;
        options.dpi = 144;
        QStringList written;
        QString error;
        QVERIFY2(Export::toImages(doc, dir.filePath(QStringLiteral("out.png")), options, &written, &error),
                 qPrintable(error));
        QCOMPARE(written,
                 (QStringList{dir.filePath(QStringLiteral("out-1.png")), dir.filePath(QStringLiteral("out-2.png"))}));

        const QImage first(written[0]);
        QCOMPARE(first.size(), QSize(800, 1000));
        QVERIFY(similar(first.pixelColor(300, 200), RED));
        QVERIFY(similar(first.pixelColor(300, 400), Qt::white));  // the hidden layer
        QVERIFY(similar(first.pixelColor(300, 600), BLUE));
        QCOMPARE(QImage(written[1]).size(), QSize(600, 400));

        // One page gives one file with the name as it is; the width sets the size
        options.pages = {0};
        options.layers = {1};
        options.width = 200;
        written.clear();
        QVERIFY(Export::toImages(doc, dir.filePath(QStringLiteral("one.png")), options, &written));
        QCOMPARE(written, QStringList{dir.filePath(QStringLiteral("one.png"))});
        const QImage one(written[0]);
        QCOMPARE(one.size(), QSize(200, 250));
        QVERIFY(similar(one.pixelColor(75, 50), Qt::white));
        QVERIFY(similar(one.pixelColor(75, 100), GREEN));

        options = ExportOptions();
        options.pages = {0};
        options.height = 250;
        QVERIFY(Export::toImages(doc, dir.filePath(QStringLiteral("height.png")), options));
        QCOMPARE(QImage(dir.filePath(QStringLiteral("height.png"))).size(), QSize(200, 250));

        QVERIFY(!Export::toImages(doc, dir.filePath(QStringLiteral("missing/out.png")), options, nullptr, &error));
        QVERIFY(!error.isEmpty());
    }

    void backgrounds() {
        QTemporaryDir dir;
        const Document doc = testDocument();
        auto exported = [&](ExportOptions::Background background) {
            ExportOptions options;
            options.pages = {1};
            options.dpi = 72;
            options.background = background;
            const QString path = dir.filePath(QStringLiteral("background.png"));
            return Export::toImages(doc, path, options) ? QImage(path) : QImage();
        };
        auto count = [](const QImage& image, auto&& matches) {
            int n = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    n += matches(image.pixelColor(x, y)) ? 1 : 0;
                }
            }
            return n;
        };
        auto isPaper = [](const QColor& c) { return similar(c, Qt::white); };

        const QImage all = exported(ExportOptions::Background::All);
        const QImage noRuling = exported(ExportOptions::Background::NoRuling);
        const QImage none = exported(ExportOptions::Background::None);
        QCOMPARE(all.size(), QSize(300, 200));

        // Without the ruling only the stroke is not white; without any background the rest is transparent
        const int strokePixels = count(noRuling, [&](const QColor& c) { return !isPaper(c); });
        QVERIFY(strokePixels > 3000 && strokePixels < 5000);
        QVERIFY(count(all, [&](const QColor& c) { return !isPaper(c); }) > strokePixels + 500);
        QCOMPARE(count(none, [](const QColor& c) { return c.alpha() == 0; }),
                 count(noRuling, [](const QColor& c) { return c == QColor(Qt::white); }));
        QVERIFY(similar(none.pixelColor(150, 100), RED));
        QCOMPARE(none.pixelColor(150, 20).alpha(), 0);
    }

    void pdf() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const Document doc = testDocument();
        const QString path = dir.filePath(QStringLiteral("out.pdf"));
        QString error;
        QVERIFY2(Export::toPdf(doc, path, ExportOptions(), &error), qPrintable(error));

        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 2);
        // Each page has the size of the page of the document
        QVERIFY(std::abs(pdf.pagePointSize(0).width() - 400) < 1 && std::abs(pdf.pagePointSize(0).height() - 500) < 1);
        QVERIFY(std::abs(pdf.pagePointSize(1).width() - 300) < 1 && std::abs(pdf.pagePointSize(1).height() - 200) < 1);

        const QImage first = pdf.render(0, QSize(400, 500));
        QVERIFY(similar(first.pixelColor(150, 100), RED));
        QVERIFY(similar(first.pixelColor(150, 200), Qt::white));
        QVERIFY(similar(first.pixelColor(150, 300), BLUE));

        // The strokes are vectors: the file holds no image
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(!file.readAll().contains("/Subtype /Image"));

        // A range of pages, layer by layer
        ExportOptions options;
        options.pages = {0};
        options.progressiveLayers = true;
        QVERIFY(Export::toPdf(doc, path, options));
        QPdfDocument steps;
        QCOMPARE(steps.load(path), QPdfDocument::Error::None);
        QCOMPARE(steps.pageCount(), 2);
        QVERIFY(similar(steps.render(0, QSize(400, 500)).pixelColor(150, 300), Qt::white));
        QVERIFY(similar(steps.render(1, QSize(400, 500)).pixelColor(150, 300), BLUE));

        // A file that cannot be written is reported, and an empty selection too
        QVERIFY(!Export::toPdf(doc, dir.filePath(QStringLiteral("missing/out.pdf")), ExportOptions(), &error));
        QVERIFY(!Export::toPdf(Document(), path, ExportOptions(), &error));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void pdfBackground() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("source.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 500), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            p.fillRect(QRectF(0, 350, 400, 150), GREEN);
        }
        Document doc = testDocument();
        doc.pages.resize(1);
        doc.pdfPath = source;
        doc.pages[0].background.type = Background::Type::Pdf;
        doc.pages[0].background.pdfPage = 0;

        ExportOptions options;
        options.dpi = 72;
        const QString png = dir.filePath(QStringLiteral("out.png"));
        QVERIFY(Export::toImages(doc, png, options));
        QVERIFY(similar(QImage(png).pixelColor(150, 400), GREEN));
        QVERIFY(similar(QImage(png).pixelColor(150, 100), RED));

        const QString path = dir.filePath(QStringLiteral("out.pdf"));
        QVERIFY(Export::toPdf(doc, path, options));
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        const QImage page = pdf.render(0, QSize(400, 500));
        QVERIFY(similar(page.pixelColor(150, 400), GREEN));
        QVERIFY(similar(page.pixelColor(150, 100), RED));

        // Without the background the PDF of it is left out
        options.background = ExportOptions::Background::None;
        QVERIFY(Export::toImages(doc, png, options));
        QCOMPARE(QImage(png).pixelColor(150, 400).alpha(), 0);
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void pdfPagesAreKept() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("source.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 500), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            QFont font(QStringLiteral("Sans"));
            font.setPixelSize(20);
            p.setFont(font);
            p.fillRect(QRectF(0, 0, 400, 500), Qt::white);
            p.drawText(QPointF(50, 60), QStringLiteral("First page of the source"));
            writer.newPage();
            p.fillRect(QRectF(0, 0, 400, 500), Qt::white);
            p.drawText(QPointF(50, 60), QStringLiteral("Second page of the source"));
            p.fillRect(QRectF(0, 350, 400, 150), GREEN);
        }
        // The second page of the PDF twice, with different annotations; a page of paper in between; the first one
        Document doc;
        doc.pdfPath = source;
        auto pdfPage = [](int number, const QColor& color, double y) {
            Page page;
            page.width = 400;
            page.height = 500;
            page.background.type = Background::Type::Pdf;
            page.background.pdfPage = number;
            page.layers = {layerWith(color, y)};
            return page;
        };
        Page paper;
        paper.width = 300;
        paper.height = 200;
        paper.layers = {layerWith(BLUE, 100)};
        doc.pages = {pdfPage(1, RED, 100), paper, pdfPage(1, BLUE, 200), pdfPage(0, RED, 300)};
        Stroke highlighter = line(QColor(0xff, 0xff, 0x00, 0x7f), 420);
        highlighter.tool = Stroke::Tool::Highlighter;
        doc.pages[0].layers[0].elements.push_back(highlighter);
        TextElement note;
        note.text = QStringLiteral("Annotation written as text");
        note.font = QStringLiteral("Sans");
        note.size = 14;
        note.pos = QPointF(50, 250);
        doc.pages[0].layers[0].elements.push_back(note);

        const QString path = dir.filePath(QStringLiteral("out.pdf"));
        QString error;
        QVERIFY2(Export::toPdf(doc, path, ExportOptions(), &error), qPrintable(error));

        // The file is the source with something appended
        QFile sourceFile(source);
        QFile outFile(path);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly) && outFile.open(QIODevice::ReadOnly));
        const QByteArray sourceData = sourceFile.readAll();
        const QByteArray outData = outFile.readAll();
        QVERIFY(outData.startsWith(sourceData));
        QVERIFY(!outData.mid(sourceData.size()).contains("/Subtype /Image"));
        QVERIFY(!outData.mid(sourceData.size()).contains("/Subtype/Image"));

        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 4);
        // The text of the pages is still text
        QVERIFY(pdf.getAllText(0).text().contains(QStringLiteral("Second page of the source")));
        QVERIFY(pdf.getAllText(2).text().contains(QStringLiteral("Second page of the source")));
        QVERIFY(pdf.getAllText(3).text().contains(QStringLiteral("First page of the source")));
        // ... and so are the texts of the annotations, as in Xournal++: they can be found and copied
        QVERIFY2(pdf.getAllText(0).text().contains(QStringLiteral("Annotation written as text")),
                 qPrintable(pdf.getAllText(0).text()));
        QVERIFY(std::abs(pdf.pagePointSize(1).width() - 300) < 1 && std::abs(pdf.pagePointSize(1).height() - 200) < 1);

        // Each page has its own annotations, on top of what the PDF shows
        const QImage first = pdf.render(0, QSize(400, 500));
        QVERIFY(similar(first.pixelColor(150, 100), RED));
        QVERIFY(similar(first.pixelColor(150, 200), Qt::white));
        QVERIFY(similar(first.pixelColor(150, 470), GREEN));
        // The highlighter lets the page shine through
        const QColor highlighted = first.pixelColor(150, 420);
        QVERIFY2(highlighted.green() > 120 && highlighted.blue() < 60 && highlighted.red() < 240,
                 qPrintable(highlighted.name()));
        const QImage third = pdf.render(2, QSize(400, 500));
        QVERIFY(similar(third.pixelColor(150, 200), BLUE));
        QVERIFY(similar(third.pixelColor(150, 100), Qt::white));
        QVERIFY(similar(pdf.render(1, QSize(300, 200)).pixelColor(150, 100), BLUE));
        const QImage last = pdf.render(3, QSize(400, 500));
        QVERIFY(similar(last.pixelColor(150, 300), RED));
        QVERIFY(similar(last.pixelColor(150, 470), Qt::white));

        // Exported once more with the result as the source: an update of an update
        Document again;
        again.pdfPath = path;
        again.pages = {pdfPage(0, BLUE, 250)};
        const QString second = dir.filePath(QStringLiteral("second.pdf"));
        QVERIFY(Export::toPdf(again, second, ExportOptions(), &error));
        QPdfDocument twice;
        QCOMPARE(twice.load(second), QPdfDocument::Error::None);
        QCOMPARE(twice.pageCount(), 1);
        const QImage both = twice.render(0, QSize(400, 500));
        QVERIFY(similar(both.pixelColor(150, 100), RED));
        QVERIFY(similar(both.pixelColor(150, 250), BLUE));

        // As images, if that is asked for or if the PDF cannot be taken apart
        ExportOptions raster;
        raster.rasterizePdfPages = true;
        QVERIFY(Export::toPdf(doc, path, raster, &error));
        QPdfDocument images;
        QCOMPARE(images.load(path), QPdfDocument::Error::None);
        QCOMPARE(images.pageCount(), 4);
        QVERIFY(!images.getAllText(0).text().contains(QStringLiteral("Second page")));
        QVERIFY(similar(images.render(0, QSize(400, 500)).pixelColor(150, 470), GREEN));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void rotatedAndCroppedPdfPages() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
        {
            QPdfWriter writer(plain);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 600), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            p.fillRect(QRectF(0, 0, 400, 600), Qt::white);
        }
        QFile plainFile(plain);
        QVERIFY(plainFile.open(QIODevice::ReadOnly));
        const QByteArray plainData = plainFile.readAll();

        for (const int rotation: {0, 90, 180, 270}) {
            // The source: the plain page, turned and cropped, made with the same code that the export uses
            Pdf::Reader reader;
            QVERIFY2(reader.load(plainData), qPrintable(reader.error()));
            std::vector<Pdf::PageInfo> pages = reader.pages();
            QCOMPARE(pages.size(), size_t(1));
            Pdf::Update update(reader);
            Pdf::Dict dict = pages[0].dict;
            dict.set("Rotate", Pdf::Value::number(rotation));
            dict.set("CropBox", Pdf::Value::array({Pdf::Value::number(50), Pdf::Value::number(100),
                                                   Pdf::Value::number(350), Pdf::Value::number(500)}));
            update.add(pages[0].ref, Pdf::Value::dict(dict));
            const QString source = dir.filePath(QStringLiteral("rotated%1.pdf").arg(rotation));
            {
                QFile file(source);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(update.finish());
            }
            QPdfDocument shown;
            QCOMPARE(shown.load(source), QPdfDocument::Error::None);
            const QSizeF size = shown.pagePointSize(0);
            QCOMPARE(size.toSize(), rotation % 180 == 0 ? QSize(300, 400) : QSize(400, 300));

            // A line near the top left corner of the page as it is shown
            Document doc;
            doc.pdfPath = source;
            Page page;
            page.width = size.width();
            page.height = size.height();
            page.background.type = Background::Type::Pdf;
            page.background.pdfPage = 0;
            Stroke stroke = line(RED, 40);
            stroke.points = {QPointF(20, 40), QPointF(120, 40)};
            stroke.updateBounds();
            page.layers.emplace_back();
            page.layers[0].elements.push_back(stroke);
            doc.pages = {page};
            const QString path = dir.filePath(QStringLiteral("out%1.pdf").arg(rotation));
            QString error;
            QVERIFY2(Export::toPdf(doc, path, ExportOptions(), &error), qPrintable(error));

            QPdfDocument pdf;
            QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
            QCOMPARE(pdf.pagePointSize(0).toSize(), size.toSize());
            const QImage image = pdf.render(0, size.toSize());
            const QByteArray where = QByteArray::number(rotation);
            QVERIFY2(similar(image.pixelColor(70, 40), RED), where.constData());
            QVERIFY2(similar(image.pixelColor(70, 80), Qt::white), where.constData());
            QVERIFY2(similar(image.pixelColor(200, 40), Qt::white), where.constData());
        }
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    void pdfFiles() {
        // What cannot be read is refused, with a reason
        Pdf::Reader reader;
        QVERIFY(!reader.load("not a pdf"));
        QVERIFY(!reader.error().isEmpty());
        QVERIFY(!reader.load("%PDF-1.4\nnothing else"));
        QVERIFY(!reader.load("%PDF-1.4\nstartxref\n999999\n%%EOF"));

        // Values are written as they are read
        Pdf::Dict dict;
        dict.set("Type", Pdf::Value::name("Page"));
        dict.set("Box", Pdf::Value::array({Pdf::Value::number(0), Pdf::Value::number(1.5), Pdf::Value::number(-2)}));
        dict.set("Parent", Pdf::Value::ref({12, 0}));
        dict.set("On", Pdf::Value::boolean(true));
        QCOMPARE(Pdf::Value::dict(dict).serialize(),
                 QByteArray("<</Type /Page /Box [0 1.5 -2] /Parent 12 0 R /On true >>"));

        // A file with compressed object and cross-reference streams, as LaTeX writes them
        QFile file(QStringLiteral("/usr/share/doc/shared-mime-info/shared-mime-info-spec.pdf"));
        if (!file.open(QIODevice::ReadOnly)) {
            QSKIP("No PDF with cross-reference streams on this machine");
        }
        QVERIFY2(reader.load(file.readAll()), qPrintable(reader.error()));
        QVERIFY(reader.usesXrefStream());
        const std::vector<Pdf::PageInfo> pages = reader.pages();
        QVERIFY(pages.size() > 10);
        for (const Pdf::PageInfo& page: pages) {
            QCOMPARE(page.dict.find("Type")->text(), QByteArray("Page"));
            QVERIFY(page.dict.find("MediaBox"));  // inherited or its own
            QVERIFY(page.dict.find("Resources"));
        }
#ifdef HAVE_QTPDF
        // An update of it is a file Qt PDF reads, with the same pages
        QTemporaryDir dir;
        Pdf::Update update(reader);
        const Pdf::Ref extra = update.newRef();
        update.addStream(extra, Pdf::Dict(), "q Q");
        const QString path = dir.filePath(QStringLiteral("updated.pdf"));
        QFile out(path);
        QVERIFY(out.open(QIODevice::WriteOnly));
        out.write(update.finish());
        out.close();
        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), static_cast<int>(pages.size()));
#endif
    }

    void svg() {
        QTemporaryDir dir;
        const Document doc = testDocument();
        ExportOptions options;
        options.pages = {0};
        const QString path = dir.filePath(QStringLiteral("out.svg"));
        QString error;
        if (!Export::svgSupported()) {
            QVERIFY(!Export::toImages(doc, path, options, nullptr, &error));
            QVERIFY(!error.isEmpty());
            QSKIP("Built without Qt SVG");
        }
        QVERIFY2(Export::toImages(doc, path, options, nullptr, &error), qPrintable(error));
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray svg = file.readAll();
        QVERIFY(svg.contains("<svg"));
        QVERIFY(svg.contains("viewBox=\"0 0 400 500\""));
        // The lines of the two visible layers, as vectors in their colours
        QVERIFY(svg.contains(RED.name().toLatin1()));
        QVERIFY(svg.contains(BLUE.name().toLatin1()));
        QVERIFY(!svg.contains(GREEN.name().toLatin1()));
        QVERIFY(!svg.contains("<image"));
    }

    /// PDF files with what other files do not have: the export keeps them right (found with the test files of pdf.js)
    void unusualPdfFiles() {
#ifdef HAVE_QTPDF
        const QByteArray content = "0 0 0 rg 20 20 60 60 re f";
        const QByteArray contentStream =
                "<< /Length " + QByteArray::number(content.size()) + " >>\nstream\n" + content + "\nendstream";
        const QByteArray plain[] = {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
                                    "/Resources << >> >>"};
        // The content saves the state of the graphics and does not restore it (scaled and clipped)
        const QByteArray unclosed = "q 0.1 0 0 0.1 0 0 cm 200 200 1000 1000 re W n " + content;
        const QByteArray unclosedStream =
                "<< /Length " + QByteArray::number(unclosed.size()) + " >>\nstream\n" + unclosed + "\nendstream";
        const QList<QList<QByteArray>> files = {
                // The catalog names the page itself as the page tree
                {"<< /Type /Catalog /Pages 3 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources << >> >>",
                 contentStream},
                // The content is an array that is an object of its own
                {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 5 0 R /Resources << >> >>",
                 contentStream, "[4 0 R]"},
                // The crop box is larger than the media box: what is shown is the media box
                {"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                 "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /CropBox [0 0 900 900] /Contents 4 0 R "
                 "/Resources << >> >>",
                 contentStream},
                {plain[0], plain[1], plain[2], unclosedStream},
                // Damaged files, see below
                {plain[0], plain[1], plain[2], contentStream},
                {plain[0], plain[1], plain[2], contentStream},
                {plain[0], plain[1], plain[2], contentStream},
                // A page without a size, which viewers show as US Letter: the content is in its lower left corner
                {plain[0], plain[1], "<< /Type /Page /Parent 2 0 R /Contents 4 0 R /Resources << >> >>", contentStream},
        };
        QTemporaryDir dir;
        for (qsizetype i = 0; i < files.size(); ++i) {
            const QString source = dir.filePath(QStringLiteral("source%1.pdf").arg(i));
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QByteArray data = makePdf(files[i]);
            if (i == 4) {
                // The offsets of the table are wrong: viewers look for the objects
                data.replace(" 00000 n \n", "1 00000 n \n");
            } else if (i == 5) {
                // No table at all
                data.truncate(data.indexOf("xref"));
            } else if (i == 6) {
                // Something before the header, which the offsets do not count; and a length that is wrong
                data = "garbage\n" + data.replace("/Length 25", "/Length 99");
            }
            file.write(data);
            file.close();
            if (i >= 4 && i <= 5) {
                Pdf::Reader reader;
                QVERIFY2(reader.load(data), qPrintable(reader.error()));
                QVERIFY(reader.repaired());
            }
            Document doc;
            doc.pdfPath = source;
            Page page;
            page.width = 200;
            page.height = 200;
            page.background.type = Background::Type::Pdf;
            page.background.pdfPage = 0;
            page.layers = {layerWith(RED, 150)};
            doc.pages = {page};
            const QString out = dir.filePath(QStringLiteral("out%1.pdf").arg(i));
            QString error;
            QVERIFY2(Export::toPdf(doc, out, ExportOptions(), &error), qPrintable(error));
            QPdfDocument exported;
            QCOMPARE(exported.load(out), QPdfDocument::Error::None);
            QCOMPARE(exported.pageCount(), 1);
            const QImage image = exported.render(0, QSize(200, 200));
            // The page (scaled down to nothing where its content leaves the scale, on a page of 612 by 792 where it
            // has no size), and the line over it
            const QPoint square = i == 7 ? QPoint(15, 187) : QPoint(30, 170);
            QVERIFY2(i == 3 || similar(image.pixelColor(square), Qt::black), qPrintable(QString::number(i)));
            QVERIFY2(similar(image.pixelColor(150, 150), RED), qPrintable(QString::number(i)));
        }
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// An encrypted PDF that opens without a password keeps its pages, text and links: what is appended is
    /// encrypted with its key, so that the file stays what it was
    void encryptedPdf_data() {
        QTest::addColumn<QString>("name");
        for (const char* name: {"rc4-40", "rc4-128", "rc4-128-r4", "aes-128", "aes-128-plain-metadata", "aes-256"}) {
            QTest::newRow(name) << QString::fromLatin1(name);
        }
    }

    void encryptedPdf() {
#ifdef HAVE_QTPDF
        QFETCH(QString, name);
        const QString source = QStringLiteral(TEST_DATA_DIR "/encrypted/") + name + QStringLiteral(".pdf");
        Document doc;
        doc.pdfPath = source;
        Page page;
        page.width = 200;
        page.height = 200;
        page.background.type = Background::Type::Pdf;
        page.background.pdfPage = 0;
        page.layers = {layerWith(RED, 100)};
        TextElement text;
        text.text = QStringLiteral("Annotation");
        text.font = QStringLiteral("Sans");
        text.pos = QPointF(20, 120);
        page.layers[0].elements.emplace_back(text);
        doc.pages = {page};
        QTemporaryDir dir;
        const QString out = dir.filePath(QStringLiteral("out.pdf"));
        QString error;
        QVERIFY2(Export::toPdf(doc, out, ExportOptions(), &error), qPrintable(error));

        QFile in(source);
        QVERIFY(in.open(QIODevice::ReadOnly));
        QFile written(out);
        QVERIFY(written.open(QIODevice::ReadOnly));
        const QByteArray data = written.readAll();
        QVERIFY(data.startsWith(in.readAll()));  // the pages are kept, not made images
        Pdf::Reader reader;
        QVERIFY2(reader.load(data), qPrintable(reader.error()));
        QVERIFY(reader.crypt());

        QPdfDocument exported;
        QCOMPARE(exported.load(out), QPdfDocument::Error::None);
        const QString allText = exported.getAllText(0).text();
        QVERIFY2(allText.contains(QStringLiteral("Encrypted text")), qPrintable(allText));
        QVERIFY2(allText.contains(QStringLiteral("Annotation")), qPrintable(allText));
        const QImage image = exported.render(0, QSize(200, 200));
        QVERIFY(similar(image.pixelColor(150, 100), RED));                      // the line
        QVERIFY(similar(image.pixelColor(30, 170), QColor(0x1a, 0x4d, 0xcc)));  // the square of the page
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// Annotations of the PDF that only show something are below the strokes, as in Xournal++; links stay links
    void annotationsOfThePdf() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("source.pdf"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources << >> "
                "/Annots [5 0 R 7 0 R 8 0 R] >>",
                stream("0 0 0 rg 20 20 60 60 re f"),
                // A blue square drawn with a pen, with a note
                "<< /Type /Annot /Subtype /Ink /Rect [100 20 190 80] /F 4 /AP << /N 6 0 R >> /Popup 8 0 R >>",
                stream("0 0 1 rg 0 0 45 30 re f", " /Type /XObject /Subtype /Form /BBox [0 0 45 30]"),
                "<< /Type /Annot /Subtype /Link /Rect [10 150 60 190] /A << /S /URI /URI (https://xournalpp.org) >> >>",
                "<< /Type /Annot /Subtype /Popup /Parent 5 0 R /Rect [120 100 190 140] /Open true >>",
        }));
        file.close();
        Document doc;
        doc.pdfPath = source;
        Page page;
        page.width = 200;
        page.height = 200;
        page.background.type = Background::Type::Pdf;
        page.background.pdfPage = 0;
        page.layers = {layerWith(RED, 150)};
        doc.pages = {page};
        const QString out = dir.filePath(QStringLiteral("out.pdf"));
        QString error;
        QVERIFY2(Export::toPdf(doc, out, ExportOptions(), &error), qPrintable(error));

        QPdfDocument exported;
        QCOMPARE(exported.load(out), QPdfDocument::Error::None);
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        const QImage image = exported.render(0, QSize(200, 200), options);
        QVERIFY(similar(image.pixelColor(30, 170), Qt::black));  // the page
        QVERIFY(similar(image.pixelColor(150, 125), Qt::blue));  // the square, scaled into its rectangle
        QVERIFY(similar(image.pixelColor(150, 150), RED));       // the line over it

        // The link and nothing else is still an annotation
        QFile written(out);
        QVERIFY(written.open(QIODevice::ReadOnly));
        Pdf::Reader reader;
        QVERIFY(reader.load(written.readAll()));
        const std::vector<Pdf::PageInfo> pages = reader.pages();
        const Pdf::Value* annots = pages[0].dict.find("Annots");
        QVERIFY(annots);
        const Pdf::Array list = reader.resolve(*annots).toArray();
        QCOMPARE(list.size(), size_t(1));
        const Pdf::Value link = reader.resolve(list[0]);
        const Pdf::Value* subtype = link.toDict().find("Subtype");
        QVERIFY(subtype);
        QCOMPARE(subtype->text(), QByteArray("Link"));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// LaTeX formulas are vectors in PDF and SVG files, as in Xournal++: drawn from their PDF
    void formulasAsVectors() {
        Document doc;
        QString error;
        QVERIFY2(loadXopp(QStringLiteral(TEST_DATA_DIR "/latex-fileversion-4.xopp"), doc, &error), qPrintable(error));
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("formulas.pdf"));
        QVERIFY2(Export::toPdf(doc, pdf, ExportOptions(), &error), qPrintable(error));
        QFile file(pdf);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray data = file.readAll();
        QVERIFY(!data.contains("/Subtype /Image") && !data.contains("/Subtype/Image"));
        if (Export::svgSupported()) {
            const QString svg = dir.filePath(QStringLiteral("formulas.svg"));
            QStringList written;
            QVERIFY2(Export::toImages(doc, svg, ExportOptions(), &written, &error), qPrintable(error));
            QFile svgFile(written.first());
            QVERIFY(svgFile.open(QIODevice::ReadOnly));
            QVERIFY(!svgFile.readAll().contains("<image"));
        }
#ifdef HAVE_QTPDF
        // And they look as the images of them
        QPdfDocument rendered;
        QCOMPARE(rendered.load(pdf), QPdfDocument::Error::None);
        const QImage page = rendered.render(0, (rendered.pagePointSize(0) * 8).toSize());
        int dark = 0;
        for (int y = 0; y < page.height(); ++y) {
            for (int x = 0; x < page.width(); ++x) {
                dark += qGray(page.pixel(x, y)) < 100;
            }
        }
        QVERIFY2(dark > 300, qPrintable(QString::number(dark)));
#endif
    }

    /// Pages of a background PDF are vectors in SVG files, as in Xournal++; a page that cannot be drawn so is an
    /// image
    void svgWithPdfPages() {
#ifdef HAVE_QTPDF
        if (!Export::svgSupported()) {
            QSKIP("Built without Qt SVG");
        }
        QTemporaryDir dir;
        const QString source = dir.filePath(QStringLiteral("source.pdf"));
        {
            // A square at (50, 50, 100, 100); and a page with a blend mode, which is not drawn as vectors
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(makePdf({
                    "<< /Type /Catalog /Pages 2 0 R >>",
                    "<< /Type /Pages /Kids [3 0 R 4 0 R] /Count 2 >>",
                    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 500] /Contents 5 0 R /Resources << >> >>",
                    "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 400 500] /Contents 6 0 R "
                    "/Resources << /ExtGState << /GS 7 0 R >> >> >>",
                    stream("0.0706 0.2039 0.3373 rg 50 350 100 100 re f"),
                    stream("/GS gs 0 0 1 rg 0 0 400 500 re f"),
                    "<< /Type /ExtGState /BM /Multiply >>",
            }));
        }
        Document doc;
        doc.pdfPath = source;
        for (int number: {0, 1}) {
            Page page;
            page.width = 400;
            page.height = 500;
            page.background.type = Background::Type::Pdf;
            page.background.pdfPage = number;
            page.layers = {layerWith(RED, 300)};
            doc.pages.push_back(page);
        }
        const QString path = dir.filePath(QStringLiteral("out.svg"));
        QStringList written;
        QString error;
        QVERIFY2(Export::toImages(doc, path, ExportOptions(), &written, &error), qPrintable(error));
        QCOMPARE(written.size(), 2);
        auto read = [](const QString& file) {
            QFile in(file);
            return in.open(QIODevice::ReadOnly) ? in.readAll() : QByteArray();
        };
        const QByteArray vectors = read(written[0]);
        QVERIFY(vectors.contains("#123456"));
#ifdef HAVE_QTSVG
        // In its place: the box of the PDF at (50, 50, 100, 100)
        QSvgRenderer renderer(written[0]);
        QImage shown(400, 500, QImage::Format_ARGB32_Premultiplied);
        shown.fill(Qt::white);
        {
            QPainter p(&shown);
            renderer.render(&p);
        }
        QVERIFY(similar(shown.pixelColor(100, 100), QColor(0x12, 0x34, 0x56)));
        QVERIFY(similar(shown.pixelColor(170, 100), Qt::white));
#endif
        QVERIFY(!vectors.contains("<image"));
        QVERIFY(vectors.contains(RED.name().toLatin1()));
        const QByteArray image = read(written[1]);
        QVERIFY(image.contains("<image"));
        QVERIFY(image.contains(RED.name().toLatin1()));
#else
        QSKIP("Built without Qt PDF");
#endif
    }
};

QTEST_MAIN(TestExport)
#include "tst_export.moc"
