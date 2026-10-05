/*
 * Qournal
 *
 * Tests of the clipboard format of Xournal++
 *
 * @license GNU GPLv2 or later
 */

#include <QBuffer>
#include <QImage>
#include <QTest>
#include <cstring>

#include "Document.h"
#include "XournalClipboard.h"

namespace {

/// The pieces of a stream of Xournal++, written here by hand as ObjectOutputStream of Xournal++ 1.3 writes them
QByteArray str(const QByteArray& value, char type = 's') {
    const quint64 length = static_cast<quint64>(value.size());
    return QByteArray("_") + type + QByteArray(reinterpret_cast<const char*>(&length), 8) + value;
}
QByteArray num(double value) { return "_d" + QByteArray(reinterpret_cast<const char*>(&value), 8); }
QByteArray integer(qint32 value) { return "_i" + QByteArray(reinterpret_cast<const char*>(&value), 4); }
QByteArray uinteger(quint32 value) { return "_u" + QByteArray(reinterpret_cast<const char*>(&value), 4); }
QByteArray size(quint64 value) { return "_l" + QByteArray(reinterpret_cast<const char*>(&value), 8); }
QByteArray object(const char* name) { return "_{" + str(name); }
QByteArray doubles(const QList<double>& values, quint64 perItem) {
    const quint64 count = static_cast<quint64>(values.size()) * 8 / perItem;
    return "_b" + QByteArray(reinterpret_cast<const char*>(&count), 8) +
           QByteArray(reinterpret_cast<const char*>(&perItem), 8) +
           QByteArray(reinterpret_cast<const char*>(values.constData()), values.size() * 8);
}
const QByteArray END = "_}";

/// The head of a clipboard of Xournal++ with a selection at the given place
QByteArray head(const QByteArray& version, const QRectF& r, int count) {
    QByteArray rect = num(r.x()) + num(r.y()) + num(r.width()) + num(r.height());
    return str("XojStrm1:") + str(version) + object("EditSelection") + rect + rect + num(0) +
           object("EditSelectionContents") + rect + rect + num(0) + num(-9999999999.0) + num(-9999999999.0) + END +
           END + integer(count);
}

QByteArray pngData() {
    QImage image(4, 2, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return data;
}

}  // namespace

class TestClipboard: public QObject {
    Q_OBJECT

private slots:
    /// A stroke is written byte for byte as Xournal++ 1.3 writes it
    void strokeBytes() {
        Stroke stroke;
        stroke.tool = Stroke::Tool::Highlighter;
        stroke.color = QColor(0x12, 0x34, 0x56, 0x7f);
        stroke.width = 3;
        stroke.fill = 40;
        stroke.cap = Qt::FlatCap;
        stroke.points = {QPointF(10, 20), QPointF(30, 40), QPointF(50, 20)};
        stroke.setStyle(QStringLiteral("dash"));
        stroke.audio = {QStringLiteral("rec.ogg"), 1500};
        stroke.updateBounds();

        const QByteArray expected = head("xournalpp 1.3.8", stroke.bounds, 1) + object("Stroke") +
                                    object("AudioElement") + object("Element") + num(stroke.bounds.x()) +
                                    num(stroke.bounds.y()) + uinteger(0x123456) + END + str("rec.ogg") + size(1500) +
                                    END + num(3) + integer(2) + integer(40) + integer(1) +
                                    doubles({10, 20, -1, 30, 40, -1, 50, 20, -1}, 24) + object("LineStyle") +
                                    doubles({6, 3}, 8) + END + END;
        QCOMPARE(XournalClipboard::write({Element(stroke)}), expected);
    }

    /// What Xournal++ 1.3 puts on the clipboard is read: built here from the serialize functions of its sources
    void readsXournalpp() {
        const QByteArray png = pngData();
        const QByteArray data =
                head("xournalpp 1.3.8", QRectF(0, 0, 100, 100), 4)
                // a pen stroke with pressure: the last point has none
                + object("Stroke") + object("AudioElement") + object("Element") + num(1) + num(2) + uinteger(0x0000ff) +
                END + str("") + size(0) + END + num(1.41) + integer(0) + integer(-1) + integer(0) +
                doubles({1, 2, 0.8, 3, 4, 1.2, 5, 6, -1}, 24) + object("LineStyle") + doubles({}, 8) + END +
                END
                // a text
                + object("Text") + object("AudioElement") + object("Element") + num(50) + num(60) + uinteger(0xff0000) +
                END + str("a.ogg") + size(7) + END + str("Größe") + object("XojFont") + str("Sans") + num(14) + END +
                END
                // an image
                + object("Image") + object("Element") + num(5) + num(6) + uinteger(0) + END + num(40) + num(20) +
                str(png, 'm') +
                END
                // a formula; its PDF is not one, which must not matter for reading
                + object("TexImage") + object("Element") + num(7) + num(8) + uinteger(0) + END + num(30) + num(10) +
                str("x^2") + str("no pdf") + END;

        std::vector<Element> elements;
        QString error;
        QVERIFY2(XournalClipboard::read(data, elements, &error), qPrintable(error));
        QCOMPARE(elements.size(), size_t(4));

        const auto& stroke = std::get<Stroke>(elements[0]);
        QCOMPARE(stroke.tool, Stroke::Tool::Pen);
        QCOMPARE(stroke.color, QColor(0, 0, 255));
        QCOMPARE(stroke.points, (QList<QPointF>{{1, 2}, {3, 4}, {5, 6}}));
        QCOMPARE(stroke.widths, (QList<double>{0.8, 1.2}));
        QVERIFY(stroke.hasPressure());
        QVERIFY(stroke.style.isEmpty());
        QVERIFY(stroke.bounds.contains(QPointF(3, 4)));

        const auto& text = std::get<TextElement>(elements[1]);
        QCOMPARE(text.text, QStringLiteral("Größe"));
        QCOMPARE(text.pos, QPointF(50, 60));
        QCOMPARE(text.color, QColor(255, 0, 0));
        QCOMPARE(text.font, QStringLiteral("Sans"));
        QCOMPARE(text.size, 14.0);
        QCOMPARE(text.audio.filename, QStringLiteral("a.ogg"));
        QCOMPARE(text.audio.timestamp, 7);

        const auto& image = std::get<ImageElement>(elements[2]);
        QVERIFY(!image.tex);
        QCOMPARE(image.rect, QRectF(5, 6, 40, 20));
        QCOMPARE(image.data, png);
        QCOMPARE(image.image.size(), QSize(4, 2));

        const auto& formula = std::get<ImageElement>(elements[3]);
        QVERIFY(formula.tex);
        QCOMPARE(formula.texSource, QStringLiteral("x^2"));
        QCOMPARE(formula.rect, QRectF(7, 8, 30, 10));
    }

    /// Everything Xournal++ 1.3 knows of the elements survives being written and read
    void roundTrip() {
        Stroke pen;
        pen.color = QColor(10, 20, 30);
        pen.width = 2;
        pen.points = {QPointF(0, 0), QPointF(5, 5), QPointF(9, 2)};
        pen.widths = {1.5, 2.5};
        pen.setStyle(QStringLiteral("cust: 2 4"));
        pen.cap = Qt::SquareCap;
        pen.updateBounds();

        Stroke highlighter;
        highlighter.tool = Stroke::Tool::Highlighter;
        highlighter.color = QColor(255, 255, 0, 0x7f);
        highlighter.fill = 100;
        highlighter.points = {QPointF(1, 1), QPointF(2, 2)};
        highlighter.updateBounds();

        TextElement text;
        text.text = QStringLiteral("two\nlines");
        text.font = QStringLiteral("Serif Bold");
        text.size = 9.5;
        text.pos = QPointF(12, 34);
        text.color = QColor(1, 2, 3);

        ImageElement image;
        image.data = pngData();
        image.rect = QRectF(3, 4, 8, 4);

        LinkElement link;
        link.text = QStringLiteral("left out");

        const std::vector<Element> written = {pen, highlighter, text, link, image};
        std::vector<Element> read;
        QVERIFY(XournalClipboard::read(XournalClipboard::write(written), read));
        QCOMPARE(read.size(), size_t(4));  // not the link

        const auto& pen2 = std::get<Stroke>(read[0]);
        QCOMPARE(pen2.color, pen.color);
        QCOMPARE(pen2.width, pen.width);
        QCOMPARE(pen2.points, pen.points);
        QCOMPARE(pen2.widths, pen.widths);
        QCOMPARE(pen2.style, pen.style);
        QCOMPARE(pen2.dashes, pen.dashes);
        QCOMPARE(pen2.cap, pen.cap);
        QCOMPARE(pen2.bounds, pen.bounds);

        const auto& highlighter2 = std::get<Stroke>(read[1]);
        QCOMPARE(highlighter2.tool, Stroke::Tool::Highlighter);
        QCOMPARE(highlighter2.color, highlighter.color);
        QCOMPARE(highlighter2.fill, 100);
        QVERIFY(!highlighter2.hasPressure());

        const auto& text2 = std::get<TextElement>(read[2]);
        QCOMPARE(text2.text, text.text);
        QCOMPARE(text2.font, text.font);
        QCOMPARE(text2.size, text.size);
        QCOMPARE(text2.pos, text.pos);
        QCOMPARE(text2.color, text.color);

        const auto& image2 = std::get<ImageElement>(read[3]);
        QCOMPARE(image2.data, image.data);
        QCOMPARE(image2.rect, image.rect);
    }

    /// Other versions of Xournal++ and broken data are refused with a message, and nothing is pasted
    void refuses() {
        std::vector<Element> elements = {Element(Stroke())};
        QString error;

        QVERIFY(!XournalClipboard::read(head("xournalpp 1.3.8+dev", QRectF(), 0), elements, &error));
        QVERIFY(error.contains(QStringLiteral("1.3.8+dev")));
        QVERIFY(!XournalClipboard::read(head("xournalpp 1.1.0", QRectF(), 0), elements, &error));

        Stroke stroke;
        stroke.points = {QPointF(0, 0), QPointF(1, 1)};
        const QByteArray good = XournalClipboard::write({Element(stroke)});
        for (qsizetype length: {qsizetype(0), qsizetype(5), good.size() / 2, good.size() - 1}) {
            error.clear();
            QVERIFY(!XournalClipboard::read(good.left(length), elements, &error));
            QVERIFY(!error.isEmpty());
        }
        // A length that points beyond the data
        QByteArray broken = good;
        const qsizetype pos = broken.indexOf("_b") + 2;
        const quint64 huge = ~quint64(0) / 2;
        std::memcpy(broken.data() + pos, &huge, 8);
        QVERIFY(!XournalClipboard::read(broken, elements, &error));

        QVERIFY(!XournalClipboard::read(head("xournalpp 1.3.8", QRectF(), 1) + object("Link") + END, elements, &error));
        QCOMPARE(elements.size(), size_t(1));  // untouched
    }
};

QTEST_MAIN(TestClipboard)
#include "tst_clipboard.moc"
