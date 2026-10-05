/*
 * Qournal
 *
 * Tests for reading and writing .xopp files. The files in data/ come from the Xournal++ test suite.
 *
 * @license GNU GPLv2 or later
 */

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QXmlStreamReader>
#include <cmath>

#include <zlib.h>

#include "Document.h"
#include "Format5Document.h"
#include "Renderer.h"
#include "XoppCompat.h"
#include "XoppLoader.h"
#include "XoppWriter.h"

namespace {

QString dataFile(const QString& name) { return QStringLiteral(TEST_DATA_DIR "/") + name; }

/// Numbers are written with 8 significant digits
bool near(double a, double b) { return std::abs(a - b) <= 1e-6 * std::max({1.0, std::abs(a), std::abs(b)}); }
bool near(const QPointF& a, const QPointF& b) { return near(a.x(), b.x()) && near(a.y(), b.y()); }
bool near(const QRectF& a, const QRectF& b) {
    return near(a.topLeft(), b.topLeft()) && near(a.bottomRight(), b.bottomRight());
}
bool near(const std::optional<Matrix>& a, const std::optional<Matrix>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a) {
        return true;
    }
    for (size_t i = 0; i < 6; ++i) {
        if (!near((*a)[i], (*b)[i])) {
            return false;
        }
    }
    return true;
}
bool same(const AudioRef& a, const AudioRef& b) { return a.filename == b.filename && a.timestamp == b.timestamp; }

bool same(const Stroke& a, const Stroke& b) {
    if (a.tool != b.tool || a.color != b.color || !near(a.width, b.width) || a.fill != b.fill || a.cap != b.cap ||
        a.style != b.style || !same(a.audio, b.audio) || a.points.size() != b.points.size() ||
        a.widths.size() != b.widths.size()) {
        return false;
    }
    for (qsizetype i = 0; i < a.points.size(); ++i) {
        if (!near(a.points[i], b.points[i])) {
            return false;
        }
    }
    for (qsizetype i = 0; i < a.widths.size(); ++i) {
        if (!near(a.widths[i], b.widths[i])) {
            return false;
        }
    }
    return true;
}

bool same(const TextElement& a, const TextElement& b) {
    return a.text == b.text && a.font == b.font && near(a.size, b.size) && near(a.pos, b.pos) &&
           near(a.matrix, b.matrix) && a.color == b.color && near(a.wrap, b.wrap) && a.justify == b.justify &&
           (a.align.isEmpty() ? QStringLiteral("left") : a.align) ==
                   (b.align.isEmpty() ? QStringLiteral("left") : b.align) &&
           same(a.audio, b.audio);
}

bool same(const ImageElement& a, const ImageElement& b) {
    return a.tex == b.tex && a.texSource == b.texSource && a.data == b.data && near(a.rect, b.rect) &&
           near(a.matrix, b.matrix);
}

bool same(const LinkElement& a, const LinkElement& b) {
    return a.text == b.text && a.url == b.url && a.font == b.font && near(a.size, b.size) &&
           near(std::optional<Matrix>(a.matrix), std::optional<Matrix>(b.matrix)) && a.color == b.color &&
           a.align == b.align;
}

bool same(const Element& a, const Element& b) {
    if (a.index() != b.index()) {
        return false;
    }
    return std::visit([&b](const auto& value) { return same(value, std::get<std::decay_t<decltype(value)>>(b)); }, a);
}

bool same(const Background& a, const Background& b) {
    return a.type == b.type && a.name == b.name && a.name.isNull() == b.name.isNull() && a.color == b.color &&
           a.style == b.style && a.config == b.config && a.pdfPage == b.pdfPage && a.domain == b.domain &&
           a.filename == b.filename && a.pixmap.size() == b.pixmap.size();
}

/// @return a description of the first difference, empty if the documents are equal
QString difference(const Document& a, const Document& b) {
    if (a.pages.size() != b.pages.size()) {
        return QStringLiteral("page count %1 != %2").arg(a.pages.size()).arg(b.pages.size());
    }
    if (a.pdfDomain != b.pdfDomain || a.pdfFilename != b.pdfFilename) {
        return QStringLiteral("background PDF differs");
    }
    for (size_t p = 0; p < a.pages.size(); ++p) {
        const Page& pa = a.pages[p];
        const Page& pb = b.pages[p];
        if (!near(pa.width, pb.width) || !near(pa.height, pb.height)) {
            return QStringLiteral("size of page %1 differs").arg(p);
        }
        if (!same(pa.background, pb.background)) {
            return QStringLiteral("background of page %1 differs").arg(p);
        }
        if (pa.layers.size() != pb.layers.size()) {
            return QStringLiteral("layer count of page %1 differs").arg(p);
        }
        for (size_t l = 0; l < pa.layers.size(); ++l) {
            const Layer& la = pa.layers[l];
            const Layer& lb = pb.layers[l];
            if (la.name != lb.name || la.name.isNull() != lb.name.isNull()) {
                return QStringLiteral("name of layer %1 on page %2 differs").arg(l).arg(p);
            }
            if (la.elements.size() != lb.elements.size()) {
                return QStringLiteral("element count of layer %1 on page %2: %3 != %4")
                        .arg(l)
                        .arg(p)
                        .arg(la.elements.size())
                        .arg(lb.elements.size());
            }
            for (size_t e = 0; e < la.elements.size(); ++e) {
                if (!same(la.elements[e], lb.elements[e])) {
                    return QStringLiteral("element %1 of layer %2 on page %3 differs").arg(e).arg(l).arg(p);
                }
            }
        }
    }
    return {};
}

template <typename T>
std::vector<const T*> elementsOf(const Document& doc) {
    std::vector<const T*> result;
    for (const Page& page: doc.pages) {
        for (const Layer& layer: page.layers) {
            for (const Element& element: layer.elements) {
                if (const auto* value = std::get_if<T>(&element)) {
                    result.push_back(value);
                }
            }
        }
    }
    return result;
}

/// The XML of a gzipped file
QByteArray gunzip(const QByteArray& data) {
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 16) != Z_OK) {
        return {};
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    QByteArray out;
    char buffer[1 << 16];
    int ret = Z_OK;
    while (ret == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(buffer);
        zs.avail_out = sizeof(buffer);
        ret = inflate(&zs, Z_NO_FLUSH);
        out.append(buffer, static_cast<qsizetype>(sizeof(buffer) - zs.avail_out));
    }
    inflateEnd(&zs);
    return ret == Z_STREAM_END ? out : QByteArray();
}

/// The elements of a file with their attributes and texts, one per line; numbers as numbers. Left out is what
/// differs between programs without meaning: the preview, the name of the program and the natural size of images
QStringList xmlLines(const QByteArray& xml) {
    QStringList lines;
    QXmlStreamReader reader(xml);
    QStringList path;
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            path.append(reader.name().toString());
            if (reader.name() == u"preview") {
                reader.skipCurrentElement();
                path.removeLast();
                continue;
            }
            QStringList attributes;
            for (const QXmlStreamAttribute& attribute: reader.attributes()) {
                // natural_size is written as by later versions of Xournal++, which 1.3.8 ignores
                if (attribute.name() == u"creator" || attribute.name() == u"natural_size") {
                    continue;
                }
                QStringList values = attribute.value().toString().split(u' ');
                for (QString& value: values) {
                    bool number = false;
                    const double d = value.toDouble(&number);
                    if (number) {
                        value = QString::number(d, 'g', 6);
                    }
                }
                attributes.append(attribute.name().toString() + u'=' + values.join(u' '));
            }
            attributes.sort();
            lines.append(path.join(u'/') + u' ' + attributes.join(u' '));
        } else if (reader.isEndElement()) {
            path.removeLast();
        } else if (reader.isCharacters() && !reader.isWhitespace()) {
            QString text = reader.text().toString();
            // Coordinates as numbers, other texts as they are
            QStringList values = text.split(u' ');
            bool numbers = true;
            for (QString& value: values) {
                bool number = false;
                const double d = value.toDouble(&number);
                numbers = numbers && number;
                value = QString::number(d, 'g', 6);
            }
            lines.append(path.join(u'/') + QStringLiteral(" : ") + (numbers ? values.join(u' ') : text));
        }
    }
    return lines;
}

}  // namespace

class TestXopp: public QObject {
    Q_OBJECT

private slots:
    void roundTrip_data() {
        QTest::addColumn<QString>("file");
        const QStringList files =
                QDir(QStringLiteral(TEST_DATA_DIR)).entryList({QStringLiteral("*.xopp")}, QDir::Files);
        QVERIFY(!files.isEmpty());
        for (const QString& file: files) {
            QTest::newRow(qPrintable(file)) << file;
        }
    }

    /// Loading a saved file gives the same document, and saving that again gives the same bytes
    void roundTrip() {
        QFETCH(QString, file);

        Document original;
        QString error;
        QVERIFY2(loadXopp(dataFile(file), original, &error), qPrintable(error));

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString copy = dir.filePath(file);
        QVERIFY2(saveXopp(copy, original, &error), qPrintable(error));

        Document reloaded;
        QVERIFY2(loadXopp(copy, reloaded, &error), qPrintable(error));
        QCOMPARE(difference(original, reloaded), QString());
        QCOMPARE(writeXoppXml(reloaded), writeXoppXml(original));
    }

    void strokes() {
        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("strokes.xopp")), doc, nullptr));
        QCOMPARE(doc.pages.size(), size_t(1));
        const Background& bg = doc.pages[0].background;
        QCOMPARE(bg.type, Background::Type::Solid);
        QCOMPARE(bg.style, QStringLiteral("graph"));
        QCOMPARE(bg.config, QStringLiteral("m1=40,rm=1"));

        const auto strokes = elementsOf<Stroke>(doc);
        QCOMPARE(strokes.size(), size_t(12));

        QVERIFY(strokes[0]->hasPressure());
        QCOMPARE(strokes[0]->color, QColor(0x00, 0x2e, 0x99));
        QCOMPARE(strokes[0]->width, 2.26);

        QVERIFY(!strokes[1]->hasPressure());
        QCOMPARE(strokes[1]->style, QStringLiteral("dash"));
        QCOMPARE(strokes[1]->points.size(), 5);
        QCOMPARE(strokes[1]->points[0], QPointF(396.76, 226.72));

        QCOMPARE(strokes[5]->tool, Stroke::Tool::Eraser);
        QCOMPARE(strokes[6]->fill, 128);
        QCOMPARE(strokes[7]->tool, Stroke::Tool::Highlighter);
        QCOMPARE(strokes[7]->color.alpha(), 0x7f);
        QCOMPARE(strokes[10]->cap, Qt::FlatCap);
    }

    void links() {
        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("links-fileversion-5.xopp")), doc, nullptr));
        const auto links = elementsOf<LinkElement>(doc);
        QVERIFY(links.size() >= 4);
        QCOMPARE(links[0]->text, QStringLiteral("Simple Link"));
        QCOMPARE(links[0]->url, QStringLiteral("https://xournalpp.github.io"));
        QCOMPARE(links[0]->matrix[4], 32.0);
        QCOMPARE(links[1]->align, QStringLiteral("center"));
        QVERIFY(links[1]->url.contains(QStringLiteral("p1=A&p2=B")));
        QVERIFY(writeXoppXml(doc).contains("fileversion=\"5\""));
    }

    void latex() {
        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("latex-fileversion-5.xopp")), doc, nullptr));
        const auto images = elementsOf<ImageElement>(doc);
        QCOMPARE(images.size(), size_t(2));
        QVERIFY(images[0]->tex);
        QCOMPARE(images[0]->texSource, QStringLiteral("x^2"));
        QVERIFY(images[0]->data.startsWith("%PDF"));
        QVERIFY(images[1]->matrix.has_value());
        QCOMPARE((*images[1]->matrix)[1], -0.29072843);
#ifdef HAVE_QTPDF
        QVERIFY(!images[0]->image.isNull());
        QVERIFY(images[0]->naturalSize.isValid());
#endif
    }

    void layers() {
        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("layers.xopp")), doc, nullptr));
        QVERIFY(doc.pages[0].layers.size() > 1);
    }

    void attachedBackgroundIsCopied() {
        Document doc;
        QVERIFY(loadXopp(dataFile(QStringLiteral("pages.xopp")), doc, nullptr));

        bool hasAttachedImage = false;
        for (const Page& page: doc.pages) {
            const Background& bg = page.background;
            if (bg.type == Background::Type::Pixmap && bg.domain == QStringLiteral("attach")) {
                hasAttachedImage = true;
                QVERIFY(!bg.pixmap.isNull());
            }
        }
        QVERIFY(hasAttachedImage);

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString copy = dir.filePath(QStringLiteral("copy.xopp"));
        QVERIFY(saveXopp(copy, doc, nullptr));
        QVERIFY(QFile::exists(copy + QStringLiteral(".bg_1.png")));
    }

    void newDocument() {
        const Document doc = Document::createEmpty();
        const QByteArray xml = writeXoppXml(doc);
        QVERIFY(xml.contains("fileversion=\"4\""));
        QVERIFY(xml.contains("<background type=\"solid\" color=\"#ffffffff\" style=\"plain\"/>"));
        QVERIFY(xml.contains("<layer"));
    }

    /// A file written by Xournal++ 1.3.8 itself (tools/xournalpp-file) is written here as Xournal++ wrote it
    void sameAsXournalpp() {
        const QString file = dataFile(QStringLiteral("written-by-xournalpp-1.3.8.xopp"));
        QFile in(file);
        QVERIFY(in.open(QIODevice::ReadOnly));
        const QByteArray theirs = gunzip(in.readAll());
        QVERIFY(!theirs.isEmpty());
        Document doc;
        QString error;
        QVERIFY2(loadXopp(file, doc, &error), qPrintable(error));
        const QStringList expected = xmlLines(theirs);
        const QStringList actual = xmlLines(writeXoppXml(doc));
        for (qsizetype i = 0; i < std::min(expected.size(), actual.size()); ++i) {
            QCOMPARE(actual[i], expected[i]);
        }
        QCOMPARE(actual.size(), expected.size());
    }

    /// A copy for Xournal++ 1.3.8 is format 4 and looks as the document does
    void copyForXournalpp13() {
        const Document doc = format5Document();
        QVERIFY(XoppCompat::needsFormat5(doc));
        QVERIFY(writeXoppXml(doc).contains("fileversion=\"5\""));
        // The link, the turned text and the turned image
        QCOMPARE(XoppCompat::format4Changes(doc).size(), 3);

        const Document old = XoppCompat::toFormat4(doc);
        QVERIFY(!XoppCompat::needsFormat5(old));
        QVERIFY(XoppCompat::format4Changes(old).isEmpty());
        const QByteArray xml = writeXoppXml(old);
        QVERIFY(xml.contains("fileversion=\"4\""));
        QVERIFY(!xml.contains("matrix="));
        const auto& elements = old.pages[0].layers[0].elements;
        QVERIFY(std::holds_alternative<TextElement>(elements[0]));   // the link
        QVERIFY(std::holds_alternative<ImageElement>(elements[1]));  // the turned text
        QCOMPARE(std::get<TextElement>(elements[2]).size, 24.0);
        QCOMPARE(std::get<ImageElement>(elements[3]).rect, QRectF(40, 220, 120, 80));
        QVERIFY(std::holds_alternative<ImageElement>(elements[4]));

        // Drawn, both look the same
        const auto draw = [](const Document& d) {
            QImage image(800, 1000, QImage::Format_ARGB32_Premultiplied);
            QPainter p(&image);
            p.scale(2, 2);
            Renderer::renderPage(p, d.pages[0], nullptr, QRectF(0, 0, 400, 500));
            return image;
        };
        const QImage a = draw(doc);
        const QImage b = draw(old);
        qint64 different = 0;
        for (int y = 0; y < a.height(); ++y) {
            for (int x = 0; x < a.width(); ++x) {
                const QRgb p = a.pixel(x, y);
                const QRgb q = b.pixel(x, y);
                different += std::max({std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)),
                                       std::abs(qBlue(p) - qBlue(q))}) > 80;
            }
        }
        // The turned text is an image in the old format: its edges differ a little, more with a heavy font
        QVERIFY2(different < a.width() * a.height() / 200, qPrintable(QString::number(different)));

        // Saved and loaded again, it stays format 4
        QTemporaryDir dir;
        QString error;
        QVERIFY2(saveXopp(dir.filePath(QStringLiteral("old.xopp")), old, &error), qPrintable(error));
        Document again;
        QVERIFY2(loadXopp(dir.filePath(QStringLiteral("old.xopp")), again, &error), qPrintable(error));
        QVERIFY(!XoppCompat::needsFormat5(again));
        QCOMPARE(again.pages[0].layers[0].elements.size(), size_t(5));
    }

    void invalidFiles() {
        Document doc;
        QString error;
        QVERIFY(!loadXopp(dataFile(QStringLiteral("does-not-exist.xopp")), doc, &error));
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!loadXopp(dataFile(QStringLiteral("pages.xopp.bg_1.png")), doc, &error));
        QVERIFY(!error.isEmpty());

        error.clear();
        QVERIFY(!saveXopp(QStringLiteral("/nonexistent-directory/file.xopp"), Document::createEmpty(), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestXopp)
#include "tst_xopp.moc"
