/*
 * Qournal
 *
 * Tests of the drawing of PDF pages as vectors: compared with the images Qt PDF makes of the same pages
 *
 * @license GNU GPLv2 or later
 */

#include <QFile>
#include <QFont>
#include <QFontInfo>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

#include "PdfFile.h"
#include "PdfFunction.h"
#include "PdfPage.h"

namespace {

QByteArray readFile(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

/// How much two images of a page differ: the share of pixels that are clearly different, after both were made
/// smaller (edges of shapes and letters are smoothed differently)
double difference(const QImage& a, const QImage& b) {
    const QImage x = a.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    const QImage y = b.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    qint64 different = 0;
    for (int row = 0; row < x.height(); ++row) {
        const auto* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
        const auto* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
        for (int col = 0; col < x.width(); ++col) {
            const int d = std::max({std::abs(qRed(p[col]) - qRed(q[col])), std::abs(qGreen(p[col]) - qGreen(q[col])),
                                    std::abs(qBlue(p[col]) - qBlue(q[col]))});
            different += d > 80;
        }
    }
    return static_cast<double>(different) / (x.width() * x.height());
}

/**
 * Whether this system has fonts with the widths of Helvetica and Times. A PDF may use these without having them
 * inside; they are drawn with what the system has for "Arial" and "Times New Roman" then, and the drawing only
 * matches the one of Qt PDF, which has such fonts of its own, if those are of the same widths
 */
bool hasStandardFonts() {
    static const QStringList SANS = {QStringLiteral("Arial"),     QStringLiteral("Liberation Sans"),
                                     QStringLiteral("Arimo"),     QStringLiteral("Nimbus Sans"),
                                     QStringLiteral("Helvetica"), QStringLiteral("TeX Gyre Heros")};
    static const QStringList SERIF = {QStringLiteral("Times New Roman"), QStringLiteral("Liberation Serif"),
                                      QStringLiteral("Tinos"),           QStringLiteral("Nimbus Roman"),
                                      QStringLiteral("Times"),           QStringLiteral("TeX Gyre Termes")};
    return SANS.contains(QFontInfo(QFont(QStringLiteral("Arial"))).family()) &&
           SERIF.contains(QFontInfo(QFont(QStringLiteral("Times New Roman"))).family());
}

/// Draws every page of a file as vectors and compares it with Qt PDF. Returns the worst difference
double compareAll(const QString& path, QString* why = nullptr, QList<QImage>* drawn = nullptr) {
#ifdef HAVE_QTPDF
    Pdf::Reader reader;
    if (!reader.load(readFile(path))) {
        return 1;
    }
    QPdfDocument pdf;
    pdf.load(path);
    Pdf::PageDrawer drawer(reader);
    double worst = 0;
    const auto pages = reader.pages();
    for (size_t i = 0; i < pages.size(); ++i) {
        const QSizeF size = pdf.pagePointSize(static_cast<int>(i));
        const QSize pixels = (size * 2).toSize();
        QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter p(&image);
            p.scale(2, 2);
            if (!drawer.draw(p, pages[i], size)) {
                if (why) {
                    *why = drawer.why();
                }
                return 1;
            }
        }
        QPdfDocumentRenderOptions options;
        options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
        QImage reference = pdf.render(static_cast<int>(i), pixels, options);
        QImage flat(pixels, QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::white);
        {
            QPainter p(&flat);
            p.drawImage(0, 0, reference);
        }
        if (drawn) {
            drawn->append(image);
            drawn->append(flat);
        }
        worst = std::max(worst, difference(image, flat));
    }
    return worst;
#else
    Q_UNUSED(path)
    Q_UNUSED(why)
    Q_UNUSED(drawn)
    return 0;
#endif
}

/// A PDF file of the given objects (numbered from 1), with its cross-reference table; object 1 is the catalog
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
    file += "trailer\n<< /Size " + QByteArray::number(objects.size() + 1) + " /Root 1 0 R >>\nstartxref\n" +
            QByteArray::number(xref) + "\n%%EOF\n";
    return file;
}

QByteArray stream(const QByteArray& content, const QByteArray& extra = {}) {
    return "<< /Length " + QByteArray::number(content.size()) + extra + " >>\nstream\n" + content + "\nendstream";
}

/// Draws the first page of a file; false if it was refused
bool drawFirstPage(const QByteArray& data, QImage& image, QString* why = nullptr) {
    Pdf::Reader reader;
    if (!reader.load(data)) {
        return false;
    }
    Pdf::PageDrawer drawer(reader);
    image = QImage(200, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter p(&image);
    const bool drawn = drawer.draw(p, reader.pages()[0], QSizeF(200, 200));
    if (why) {
        *why = drawer.why();
    }
    return drawn;
}

}  // namespace

class TestPdfPage: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
#ifndef HAVE_QTPDF
        QSKIP("Built without Qt PDF: nothing to compare with");
#endif
    }

    /// A PDF of Qt: paths, dashes, transparency, an image, text in embedded fonts
    void pdfOfQt() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("qt.pdf"));
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 500), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            p.setRenderHint(QPainter::Antialiasing);
            p.fillRect(QRectF(20, 20, 150, 80), QColor(30, 120, 200));
            p.setPen(QPen(Qt::red, 4, Qt::DashLine));
            QPainterPath curve;
            curve.moveTo(30, 150);
            curve.cubicTo(100, 80, 200, 220, 360, 140);
            p.drawPath(curve);
            p.fillRect(QRectF(100, 60, 150, 80), QColor(0, 200, 0, 120));
            QImage image(60, 40, QImage::Format_RGB32);
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    image.setPixel(x, y, qRgb(x * 4, y * 6, 128));
                }
            }
            p.drawImage(QRectF(250, 20, 120, 80), image);
            p.setPen(Qt::black);
            QFont sans(QStringLiteral("DejaVu Sans"));
            sans.setPixelSize(24);
            p.setFont(sans);
            p.drawText(QPointF(30, 260), QStringLiteral("Hello vectors äöü €"));
            QFont serif(QStringLiteral("DejaVu Serif"));
            serif.setPixelSize(18);
            p.setFont(serif);
            p.save();
            p.translate(60, 420);
            p.rotate(-20);
            p.drawText(QPointF(0, 0), QStringLiteral("Turned text, ffi and fl"));
            p.restore();
            p.setClipRect(QRectF(250, 300, 80, 80));
            p.setBrush(QColor(200, 100, 0));
            p.drawEllipse(QRectF(220, 280, 140, 140));
        }
        QString why;
        const double worst = compareAll(path, &why);
        QVERIFY2(worst < 0.01, qPrintable(QStringLiteral("difference %1 %2").arg(worst).arg(why)));
    }

    /// A PDF of Ghostscript: Type 1 fonts in CFF form, the standard encoding, line joins, clipping, an image
    void pdfOfGhostscript() {
        const QString gs = QStandardPaths::findExecutable(QStringLiteral("gs"));
        if (gs.isEmpty()) {
            QSKIP("Ghostscript is not installed");
        }
        QTemporaryDir dir;
        const QString ps = dir.filePath(QStringLiteral("page.ps"));
        QFile file(ps);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("%!PS\n<< /PageSize [400 500] >> setpagedevice\n"
                   "/Times-Roman findfont 28 scalefont setfont 30 440 moveto (Ghostscript fi \\(Times\\)) show\n"
                   "/Helvetica-Bold findfont 20 scalefont setfont 30 400 moveto (Bold Helvetica: 1234) show\n"
                   "/Courier findfont 16 scalefont setfont 30 370 moveto (Courier, quoted `x') show\n"
                   "0.8 0.2 0.2 setrgbcolor 10 setlinewidth 1 setlinejoin 1 setlinecap\n"
                   "newpath 40 300 moveto 150 340 lineto 260 280 lineto stroke\n"
                   "gsave newpath 200 100 80 0 360 arc clip 0 0.5 0.9 setrgbcolor 100 40 200 140 rectfill grestore\n"
                   "0 0 1 0 setcmykcolor 30 30 100 60 rectfill\n"
                   "gsave 300 200 translate 60 60 scale 8 8 8 [8 0 0 -8 0 8]\n"
                   "{<00204060809fbfdf20406080a0c0e0ff406080a0c0e0ff20>} image grestore\n"
                   "showpage\n");
        file.close();
        const QString pdf = dir.filePath(QStringLiteral("gs.pdf"));
        QProcess process;
        process.start(gs, {QStringLiteral("-q"), QStringLiteral("-dNOPAUSE"), QStringLiteral("-dBATCH"),
                           QStringLiteral("-dSAFER"), QStringLiteral("-sDEVICE=pdfwrite"),
                           QStringLiteral("-sOutputFile=") + pdf, ps});
        QVERIFY(process.waitForFinished(60000));
        QVERIFY2(QFile::exists(pdf), process.readAllStandardError().constData());
        if (!hasStandardFonts()) {
            QSKIP("No fonts with the widths of Helvetica and Times (e.g. Liberation) are installed");
        }
        QString why;
        const double worst = compareAll(pdf, &why);
        QVERIFY2(worst < 0.01, qPrintable(QStringLiteral("difference %1 %2").arg(worst).arg(why)));
    }

    /// Pages that are turned and cropped come out as viewers show them
    void rotatedAndCropped() {
        QTemporaryDir dir;
        const QString plain = dir.filePath(QStringLiteral("plain.pdf"));
        {
            QPdfWriter writer(plain);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 600), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            p.fillRect(QRectF(0, 0, 400, 600), Qt::white);
            p.fillRect(QRectF(60, 110, 100, 50), Qt::red);   // near the top left of the crop box
            p.fillRect(QRectF(250, 400, 80, 80), Qt::blue);  // near its bottom right
        }
        const QByteArray data = readFile(plain);
        for (const int rotation: {0, 90, 180, 270}) {
            Pdf::Reader reader;
            QVERIFY(reader.load(data));
            std::vector<Pdf::PageInfo> pages = reader.pages();
            Pdf::Update update(reader);
            Pdf::Dict dict = pages[0].dict;
            dict.set("Rotate", Pdf::Value::number(rotation));
            dict.set("CropBox", Pdf::Value::array({Pdf::Value::number(50), Pdf::Value::number(100),
                                                   Pdf::Value::number(350), Pdf::Value::number(500)}));
            update.add(pages[0].ref, Pdf::Value::dict(dict));
            const QString path = dir.filePath(QStringLiteral("rotated%1.pdf").arg(rotation));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(update.finish());
            file.close();
            QString why;
            const double worst = compareAll(path, &why);
            QVERIFY2(worst < 0.005, qPrintable(QStringLiteral("rotation %1: %2 %3").arg(rotation).arg(worst).arg(why)));
        }
    }

    /// Encrypted files that open without a password (written by qpdf, see tools/make_encrypted_pdfs.py): every
    /// method of the standard security handler
    void encryptedFiles_data() {
        QTest::addColumn<QString>("name");
        for (const char* name: {"rc4-40", "rc4-128", "rc4-128-r4", "aes-128", "aes-128-plain-metadata", "aes-256"}) {
            QTest::newRow(name) << QString::fromLatin1(name);
        }
    }

    void encryptedFiles() {
        QFETCH(QString, name);
        const QString path = QStringLiteral(TEST_DATA_DIR "/encrypted/") + name + QStringLiteral(".pdf");
        Pdf::Reader reader;
        QVERIFY2(reader.load(readFile(path)), qPrintable(reader.error()));
        QVERIFY(reader.crypt());
        // Strings are decrypted: the address of the link
        const std::vector<Pdf::PageInfo> pages = reader.pages();
        QCOMPARE(pages.size(), size_t(1));
        const Pdf::Value annots = reader.resolve(*pages[0].dict.find("Annots"));
        const Pdf::Value link = reader.resolve(annots.toArray()[0]);
        const Pdf::Value action = reader.resolve(*link.toDict().find("A"));
        QCOMPARE(reader.resolve(*action.toDict().find("URI")).stringBytes(),
                 QByteArray("https://xournalpp.github.io/"));
#ifdef HAVE_QTPDF
        // The files have a text in Helvetica, which is not inside them
        if (hasStandardFonts()) {
            QString why;
            const double d = compareAll(path, &why);
            QVERIFY2(d < 0.01, qPrintable(QStringLiteral("%1 %2").arg(d).arg(why)));
        }
#endif
    }

    void needsPassword() {
        Pdf::Reader reader;
        QVERIFY(!reader.load(readFile(QStringLiteral(TEST_DATA_DIR "/encrypted/needs-password.pdf"))));
        QVERIFY(reader.error().contains(QStringLiteral("password")));
    }

    /// Soft masks of the graphics state, by luminosity and by alpha: what they cover is an image, as Qt PDF draws it
    void softMasks() {
#ifdef HAVE_QTPDF
        const QByteArray content = "0 0 1 rg 0 0 200 200 re f "
                                   // Red through a gradient from black to white: from nothing to all of it
                                   "q /Lum gs 1 0 0 rg 0 100 200 100 re f Q "
                                   // Green through a circle that is opaque
                                   "q /Alpha gs 0 0.8 0 rg 0 0 200 100 re f Q";
        const QByteArray data = makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
                "/Resources << /ExtGState << /Lum 5 0 R /Alpha 6 0 R >> >> >>",
                stream(content),
                "<< /Type /ExtGState /SMask << /Type /Mask /S /Luminosity /G 7 0 R >> >>",
                "<< /Type /ExtGState /SMask << /Type /Mask /S /Alpha /G 8 0 R >> >>",
                stream("/Sh sh", " /Type /XObject /Subtype /Form /BBox [0 0 200 200] /Group << /S /Transparency "
                                 "/CS /DeviceGray >> /Resources << /Shading << /Sh << /ShadingType 2 /ColorSpace "
                                 "/DeviceGray /Coords [0 0 200 0] /Function << /FunctionType 2 /Domain [0 1] /C0 [0] "
                                 "/C1 [1] /N 1 >> /Extend [true true] >> >> >>"),
                stream("0 g 100 50 m 140 50 l 140 90 l 60 90 l 60 50 l f",
                       " /Type /XObject /Subtype /Form /BBox [0 0 200 200] /Group << /S /Transparency >>"),
        });
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("masks.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QString why;
        QList<QImage> drawn;
        const double d = compareAll(path, &why, &drawn);
        QVERIFY2(d < 0.01, qPrintable(QStringLiteral("%1 %2").arg(d).arg(why)));
        const QImage& image = drawn.first();
        QVERIFY(image.pixelColor(10, 100).blue() > 200);    // left: the mask is black, blue shows
        QVERIFY(image.pixelColor(390, 100).red() > 200);    // right: white, red covers it
        QVERIFY(image.pixelColor(200, 260).green() > 150);  // in the shape of the alpha mask
        QVERIFY(image.pixelColor(20, 380).blue() > 200);    // outside it
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// Type 3 fonts, whose glyphs are content streams: one with colours of its own (d0), one in the colour of the
    /// text (d1), whose colour operators do not count
    void type3Fonts() {
#ifdef HAVE_QTPDF
        const QByteArray data = makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
                "/Resources << /Font << /T3 5 0 R >> >> >>",
                stream("BT 0 0 1 rg /T3 40 Tf 20 120 Td (abab) Tj ET BT 1 0 0 rg /T3 20 Tf 20 40 Td (ba) Tj ET"),
                "<< /Type /Font /Subtype /Type3 /FontBBox [0 0 1000 1000] /FontMatrix [0.001 0 0 0.001 0 0] "
                "/FirstChar 97 /LastChar 98 /Widths [1000 800] /Encoding << /Type /Encoding /Differences [97 /square "
                "/triangle] >> /CharProcs << /square 6 0 R /triangle 7 0 R >> /Resources << >> >>",
                // A green square: colours of its own
                stream("1000 0 d0 0 0.6 0 rg 100 0 800 800 re f"),
                // A triangle in the colour of the text: its own colour is not used
                stream("800 0 0 0 700 800 d1 0 1 0 rg 0 0 m 700 0 l 350 800 l f"),
        });
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("type3.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QString why;
        QList<QImage> drawn;
        const double d = compareAll(path, &why, &drawn);
        QVERIFY2(d < 0.01, qPrintable(QStringLiteral("%1 %2").arg(d).arg(why)));
        const QImage& image = drawn.first();
        QVERIFY(image.pixelColor(80, 140).green() > 120);  // the first square, at 2 pixels per point
        QVERIFY(image.pixelColor(155, 150).blue() > 200);  // the first triangle, in the blue of the text
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// The four types of functions
    void functions() {
        const QByteArray samples("\xff\x00\x00\x00\x00\xff", 6);
        Pdf::Reader reader;
        QVERIFY(reader.load(makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [] /Count 0 >>",
                "<< /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >>",
                "<< /FunctionType 3 /Domain [0 1] /Bounds [0.5] /Encode [0 1 0 1] /Functions "
                "[<< /FunctionType 2 /Domain [0 1] /C0 [0] /C1 [1] /N 1 >> 3 0 R] >>",
                stream("{ exch pop dup 0.5 2 copy gt { pop } { exch pop } ifelse }",
                       " /FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1]"),
                stream(samples, " /FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] /BitsPerSample 8"),
        })));
        const auto close = [](const std::vector<double>& a, const std::vector<double>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                if (std::abs(a[i] - b[i]) > 0.01) {
                    return false;
                }
            }
            return true;
        };
        const auto exponential = Pdf::Function::parse(reader, Pdf::Value::ref({3, 0}));
        QVERIFY(exponential);
        QVERIFY(close((*exponential)({0.25}), {0.75, 0, 0.25}));
        const auto stitching = Pdf::Function::parse(reader, Pdf::Value::ref({4, 0}));
        QVERIFY(stitching);
        QVERIFY(close((*stitching)({0.25}), {0.5}));
        QVERIFY(close((*stitching)({0.75}), {0.5, 0, 0.5}));
        const auto calculator = Pdf::Function::parse(reader, Pdf::Value::ref({5, 0}));
        QVERIFY(calculator);
        QVERIFY(close((*calculator)({0.2, 0.8}), {0.8, 0.8}));  // b, max(b, 0.5)
        QVERIFY(close((*calculator)({0.2, 0.1}), {0.1, 0.5}));
        const auto sampled = Pdf::Function::parse(reader, Pdf::Value::ref({6, 0}));
        QVERIFY(sampled);
        QVERIFY(close((*sampled)({0.5}), {0.5, 0, 0.5}));
    }

    /// Gradients (axial and radial, as shadings and as patterns), tiling patterns and colours made by functions, as
    /// Qt PDF draws them
    void gradientsAndPatterns() {
#ifdef HAVE_QTPDF
        const QByteArray content = "q 0 100 100 100 re W n /Sh0 sh Q "
                                   "/Pattern cs /P0 scn 100 100 100 100 re f "
                                   "/Pattern cs /P1 scn 0 0 100 100 re f "
                                   "/Sep cs 0.5 scn 100 50 50 50 re f "
                                   "/DN cs 0.2 0.8 scn 150 50 50 50 re f "
                                   "/Sampled cs 0.5 scn 100 0 100 50 re f";
        const QByteArray samples("\xff\x00\x00\x00\x00\xff", 6);
        const QByteArray data = makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources "
                "<< /Shading << /Sh0 5 0 R >> /Pattern << /P0 6 0 R /P1 7 0 R >> "
                "/ColorSpace << /Sep [/Separation /Spot /DeviceCMYK 8 0 R] "
                "/DN [/DeviceN [/A /B] /DeviceRGB 9 0 R] /Sampled [/Separation /Ink /DeviceRGB 10 0 R] >> >> >>",
                stream(content),
                // Red to blue from left to right
                "<< /ShadingType 2 /ColorSpace /DeviceRGB /Coords [0 150 100 150] /Extend [true true] "
                "/Function << /FunctionType 2 /Domain [0 1] /C0 [1 0 0] /C1 [0 0 1] /N 1 >> >>",
                // Yellow, green, blue from the centre outwards; nothing beyond the circle
                "<< /PatternType 2 /Shading << /ShadingType 3 /ColorSpace /DeviceRGB /Coords [150 150 0 150 150 50] "
                "/Extend [true false] /Function << /FunctionType 3 /Domain [0 1] /Bounds [0.5] /Encode [0 1 0 1] "
                "/Functions [<< /FunctionType 2 /Domain [0 1] /C0 [1 1 0] /C1 [0 1 0] /N 1 >> "
                "<< /FunctionType 2 /Domain [0 1] /C0 [0 1 0] /C1 [0 0 1] /N 1 >>] >> >> >>",
                // Green squares of 10 every 20 points
                stream("0 0.6 0 rg 0 0 10 10 re f",
                       " /PatternType 1 /PaintType 1 /TilingType 1 /BBox [0 0 10 10] /XStep 20 /YStep 20 "
                       "/Resources << >>"),
                // Cyan of the strength of the tint
                "<< /FunctionType 2 /Domain [0 1] /C0 [0 0 0 0] /C1 [1 0 0 0] /N 1 >>",
                // (a, b) -> (b, b, 0.5)
                stream("{ exch pop dup 0.5 }", " /FunctionType 4 /Domain [0 1 0 1] /Range [0 1 0 1 0 1]"),
                // From red to blue in two samples
                stream(samples, " /FunctionType 0 /Domain [0 1] /Range [0 1 0 1 0 1] /Size [2] /BitsPerSample 8"),
        });
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("gradients.pdf"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(data);
        file.close();
        QString why;
        QList<QImage> drawn;
        const double d = compareAll(path, &why, &drawn);
        QVERIFY2(d < 0.01, qPrintable(QStringLiteral("%1 %2").arg(d).arg(why)));
        // A few colours, in pixels of twice the size of points
        const QImage& image = drawn.first();
        QVERIFY(image.pixelColor(4, 100).red() > 230);           // the axial gradient starts red
        QVERIFY(image.pixelColor(196, 100).blue() > 230);        // and ends blue
        QCOMPARE(image.pixelColor(398, 2), QColor(Qt::white));   // beyond the circle of the radial one
        QVERIFY(image.pixelColor(10, 390).green() > 120);        // a cell of the tiling pattern
        QCOMPARE(image.pixelColor(30, 390), QColor(Qt::white));  // between the cells
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// Optional content that is off is not drawn, as in viewers: marked content, objects and annotations
    void optionalContent() {
        const QByteArray content = "/OC /Hidden BDC 1 0 0 rg 0 100 100 100 re f EMC "
                                   "/OC /Shown BDC 0 0 1 rg 100 100 100 100 re f EMC "
                                   "/Form Do";
        const QByteArray data = makePdf({
                "<< /Type /Catalog /Pages 2 0 R /OCProperties << /OCGs [5 0 R 6 0 R] /D << /OFF [5 0 R] >> >> >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R /Resources "
                "<< /Properties << /Hidden 5 0 R /Shown 6 0 R >> /XObject << /Form 7 0 R >> >> "
                "/Annots [8 0 R 9 0 R] >>",
                stream(content),
                "<< /Type /OCG /Name (Off) >>",
                "<< /Type /OCG /Name (On) >>",
                stream("0 1 0 rg 0 0 100 100 re f", " /Type /XObject /Subtype /Form /BBox [0 0 200 200] /OC 5 0 R"),
                // Not to be shown (flag 32), and in the hidden group
                "<< /Type /Annot /Subtype /Square /Rect [100 0 150 50] /F 32 /AP << /N 10 0 R >> >>",
                "<< /Type /Annot /Subtype /Square /Rect [150 0 200 50] /OC 5 0 R /AP << /N 10 0 R >> >>",
                stream("0 0 0 rg 0 0 50 50 re f", " /Type /XObject /Subtype /Form /BBox [0 0 50 50]"),
        });
        QImage image;
        QString why;
        QVERIFY2(drawFirstPage(data, image, &why), qPrintable(why));
        QCOMPARE(image.pixelColor(50, 50), QColor(Qt::white));  // the red square is in the hidden group
        QCOMPARE(image.pixelColor(150, 50), QColor(Qt::blue));
        QCOMPARE(image.pixelColor(50, 150), QColor(Qt::white));  // the green form is hidden
        QCOMPARE(image.pixelColor(125, 175), QColor(Qt::white));
        QCOMPARE(image.pixelColor(175, 175), QColor(Qt::white));
    }

    /// Where the result would be wrong, the page is refused (and drawn as an image by the caller)
    void refusesWhatWouldBeWrong() {
        auto page = [](const QByteArray& resources, const QByteArray& content, const QByteArray& extra = {}) {
            return QList<QByteArray>{"<< /Type /Catalog /Pages 2 0 R >>", "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                                     "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
                                     "/Resources " +
                                             resources + extra + " >>",
                                     stream(content)};
        };
        QImage image;
        // A font that is not embedded and not one of the standard fonts: its characters are not known here
        QList<QByteArray> chinese = page("<< /Font << /F1 5 0 R >> >>", "BT /F1 20 Tf 10 100 Td (abc) Tj ET");
        chinese.append("<< /Type /Font /Subtype /TrueType /BaseFont /SimSun /Encoding /WinAnsiEncoding >>");
        QVERIFY(!drawFirstPage(makePdf(chinese), image));
        // A standard font that is not embedded is drawn with a font of the system
        QList<QByteArray> helvetica = page("<< /Font << /F1 5 0 R >> >>", "BT /F1 20 Tf 10 100 Td (abc) Tj ET");
        helvetica.append("<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>");
        QString why;
        QVERIFY2(drawFirstPage(makePdf(helvetica), image, &why), qPrintable(why));
        // An annotation without an appearance: viewers make one up
        QList<QByteArray> square = page("<< >>", "", " /Annots [5 0 R]");
        square.append("<< /Type /Annot /Subtype /Square /Rect [10 10 100 100] >>");
        QVERIFY(!drawFirstPage(makePdf(square), image));
        // Colours calibrated with a gamma
        QList<QByteArray> calibrated = page("<< /ColorSpace << /C 5 0 R >> >>", "/C cs 1 0 0 sc 0 0 100 100 re f");
        calibrated.append("[/CalRGB << /WhitePoint [0.95 1 1.09] /Gamma [2.2 2.2 2.2] >>]");
        QVERIFY(!drawFirstPage(makePdf(calibrated), image));
    }

    /// The gradients of Qt's PDF writer, which are shadings, as Qt PDF draws them
    void gradientOfQt() {
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shading.pdf"));
        {
            QPdfWriter writer(path);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(200, 200), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            QLinearGradient gradient(0, 0, 200, 0);
            gradient.setColorAt(0, Qt::red);
            gradient.setColorAt(1, Qt::blue);
            p.fillRect(QRectF(0, 0, 200, 100), gradient);
            QRadialGradient radial(QPointF(100, 150), 50);
            radial.setColorAt(0, Qt::yellow);
            radial.setColorAt(1, Qt::darkGreen);
            p.fillRect(QRectF(0, 100, 200, 100), radial);
        }
        QString why;
        const double d = compareAll(path, &why);
        QVERIFY2(d < 0.01, qPrintable(QStringLiteral("%1 %2").arg(d).arg(why)));
#else
        QSKIP("Built without Qt PDF");
#endif
    }

    /// What cannot be drawn is refused, and nothing is drawn, also not what came before it
    void refusesWhatItCannotDraw() {
        const QByteArray data = makePdf({
                "<< /Type /Catalog /Pages 2 0 R >>",
                "<< /Type /Pages /Kids [3 0 R] /Count 1 >>",
                "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 200 200] /Contents 4 0 R "
                "/Resources << /ExtGState << /GS 5 0 R >> >> >>",
                stream("1 0 0 rg 0 0 200 200 re f /GS gs 0 0 1 rg 50 50 100 100 re f"),
                // A blend mode, which the PDF writer of Qt would leave out
                "<< /Type /ExtGState /BM /Multiply >>",
        });
        Pdf::Reader reader;
        QVERIFY(reader.load(data));
        Pdf::PageDrawer drawer(reader);
        QImage image(200, 200, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter p(&image);
        const std::vector<Pdf::PageInfo> pages = reader.pages();
        QVERIFY(!drawer.draw(p, pages[0], QSizeF(200, 200)));
        p.end();
        QVERIFY(!drawer.why().isEmpty());
        QCOMPARE(image.pixelColor(100, 100), QColor(Qt::white));
    }
};

QTEST_MAIN(TestPdfPage)
#include "tst_pdfpage.moc"
