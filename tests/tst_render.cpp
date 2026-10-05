/*
 * Qournal
 *
 * Tests for the rendering of page backgrounds. The expected positions follow the background views of Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#include <QImage>
#include <QPainter>
#include <QTest>

#include "Document.h"
#include "Renderer.h"

namespace {

constexpr int SCALE = 4;

const QColor DODGER_BLUE(0x40, 0xa0, 0xff);
const QColor DEEP_PINK(0xff, 0x00, 0x80);
const QColor SILVER(0xbd, 0xbd, 0xbd);

Page pageWith(const QString& style, const QString& config = {}, const QColor& color = Qt::white) {
    Page page;
    page.width = 600;
    page.height = 800;
    page.background.style = style;
    page.background.config = config;
    page.background.color = color;
    return page;
}

QImage render(const Page& page) {
    QImage image(QSizeF(page.width * SCALE, page.height * SCALE).toSize(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::magenta);
    QPainter p(&image);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(SCALE, SCALE);
    Renderer::renderBackground(p, page, nullptr);
    return image;
}

QColor at(const QImage& image, double x, double y) {
    return image.pixelColor(static_cast<int>(x * SCALE), static_cast<int>(y * SCALE));
}

bool similar(const QColor& a, const QColor& b) {
    return std::abs(a.red() - b.red()) < 24 && std::abs(a.green() - b.green()) < 24 &&
           std::abs(a.blue() - b.blue()) < 24;
}

int countPixels(const QImage& image, const QColor& color) {
    int count = 0;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            count += image.pixelColor(x, y) == color;
        }
    }
    return count;
}

}  // namespace

#define COMPARE_COLOR(actual, expected) \
    do { \
        const QColor actualColor = (actual); \
        const QColor expectedColor = (expected); \
        QVERIFY2(similar(actualColor, expectedColor), \
                 qPrintable(QStringLiteral("%1 instead of %2").arg(actualColor.name(), expectedColor.name()))); \
    } while (false)

class TestRender: public QObject {
    Q_OBJECT

private slots:
    void plain() {
        const QImage image = render(pageWith(QStringLiteral("plain"), {}, QColor(0xfe, 0xf8, 0xc9)));
        QCOMPARE(countPixels(image, QColor(0xfe, 0xf8, 0xc9)), image.width() * image.height());
    }

    void unknownStyleIsPlain() {
        const QImage image = render(pageWith(QStringLiteral("no-such-style")));
        QCOMPARE(countPixels(image, Qt::white), image.width() * image.height());
    }

    void ruled() {
        const QImage image = render(pageWith(QStringLiteral("ruled")));
        // Lines every 24 pt from 80 pt on, down to the footer of 60 pt
        COMPARE_COLOR(at(image, 300, 80), DODGER_BLUE);
        COMPARE_COLOR(at(image, 300, 104), DODGER_BLUE);
        COMPARE_COLOR(at(image, 300, 728), DODGER_BLUE);
        COMPARE_COLOR(at(image, 300, 92), Qt::white);
        COMPARE_COLOR(at(image, 300, 56), Qt::white);
        COMPARE_COLOR(at(image, 300, 752), Qt::white);
        COMPARE_COLOR(at(image, 72, 92), Qt::white);  // no margin line
    }

    void lined() {
        const QImage image = render(pageWith(QStringLiteral("lined")));
        COMPARE_COLOR(at(image, 300, 80), DODGER_BLUE);
        COMPARE_COLOR(at(image, 72, 92), DEEP_PINK);
        COMPARE_COLOR(at(image, 72, 10), DEEP_PINK);
    }

    void linedWithMarginOnTheRight() {
        const QImage image = render(pageWith(QStringLiteral("lined"), QStringLiteral("m1=-72")));
        COMPARE_COLOR(at(image, 600 - 72, 92), DEEP_PINK);
        COMPARE_COLOR(at(image, 72, 92), Qt::white);
    }

    void configuredSpacingAndColour() {
        const QImage image = render(pageWith(QStringLiteral("ruled"), QStringLiteral("r1=40,f1=00ff00,lw=2")));
        COMPARE_COLOR(at(image, 300, 120), QColor(0x00, 0xff, 0x00));
        COMPARE_COLOR(at(image, 300, 104), Qt::white);
    }

    void darkPaperUsesAlternativeColour() {
        const QImage image = render(pageWith(QStringLiteral("ruled"), {}, Qt::black));
        COMPARE_COLOR(at(image, 300, 80), QColor(0x43, 0x43, 0x43));
        COMPARE_COLOR(at(image, 300, 92), Qt::black);
    }

    void graph() {
        const QImage image = render(pageWith(QStringLiteral("graph")));
        COMPARE_COLOR(at(image, 14.17, 7), SILVER);
        COMPARE_COLOR(at(image, 7, 14.17 * 3), SILVER);
        COMPARE_COLOR(at(image, 7, 7), Qt::white);
    }

    void graphWithBorder() {
        // The file strokes.xopp of Xournal++ uses this configuration
        const QImage image = render(pageWith(QStringLiteral("graph"), QStringLiteral("m1=40,rm=1")));
        // No lines in the margin; the first line is the first multiple of the raster inside of it
        COMPARE_COLOR(at(image, 14.17 * 2, 400), Qt::white);
        COMPARE_COLOR(at(image, 300, 14.17 * 2), Qt::white);
        COMPARE_COLOR(at(image, 14.17 * 3, 400), SILVER);
        COMPARE_COLOR(at(image, 300, 14.17 * 3), SILVER);
        // Lines end at the outermost line, not at the margin
        COMPARE_COLOR(at(image, 14.17 * 4, 41), Qt::white);
    }

    void graphWithBoldLines() {
        const QImage image =
                render(pageWith(QStringLiteral("graph"), QStringLiteral("m1=20,rm=1,bli=5,blw=3,f1=000000")));
        // Every fifth line is 3 pt wide instead of 0.5 pt
        COMPARE_COLOR(at(image, 14.17 * 5 + 1.2, 400), Qt::black);
        COMPARE_COLOR(at(image, 14.17 * 6 + 1.2, 400), Qt::white);
    }

    void dotted() {
        const QImage image = render(pageWith(QStringLiteral("dotted")));
        COMPARE_COLOR(at(image, 14.17 * 3, 14.17 * 5), SILVER);
        COMPARE_COLOR(at(image, 14.17 * 3.5, 14.17 * 5), Qt::white);
    }

    void staves() {
        const QImage image = render(pageWith(QStringLiteral("staves")));
        // Five lines 5 pt apart, between the margins of 50 pt
        for (int line = 0; line < 5; ++line) {
            COMPARE_COLOR(at(image, 300, 80 + 5 * line), Qt::black);
        }
        COMPARE_COLOR(at(image, 300, 82.5), Qt::white);
        COMPARE_COLOR(at(image, 300, 110), Qt::white);
        COMPARE_COLOR(at(image, 40, 80), Qt::white);
        COMPARE_COLOR(at(image, 50, 90), Qt::black);
        // The next staff: 40 + (0.5 + 20) + 4 * 0.5 further down
        COMPARE_COLOR(at(image, 300, 80 + 62.5), Qt::black);
    }

    void isometric() {
        const Page dots = pageWith(QStringLiteral("isodotted"));
        const Page lines = pageWith(QStringLiteral("isograph"));
        const QImage dotImage = render(dots);
        const QImage lineImage = render(lines);

        // The grid is centered and keeps a margin of the triangle size
        const int dotPixels = dotImage.width() * dotImage.height() - countPixels(dotImage, Qt::white);
        const int linePixels = lineImage.width() * lineImage.height() - countPixels(lineImage, Qt::white);
        QVERIFY(dotPixels > 1000);
        QVERIFY(linePixels > 10 * dotPixels);
        COMPARE_COLOR(at(lineImage, 5, 400), Qt::white);
        COMPARE_COLOR(at(lineImage, 300, 5), Qt::white);

        // The dots are on the crossings of the lines: every pixel of a dot centre is on a line
        const double xstep = std::sqrt(3.0) / 2 * 14.17;
        const int cols = static_cast<int>((600 - 2 * 14.17) / xstep);
        const int rows = static_cast<int>((800 - 2 * 14.17) / 7.085);
        const QPointF offset((600 - cols * xstep) / 2, (800 - rows * 7.085) / 2);
        const QPointF crossing = offset + QPointF(2 * xstep, 7.085 + 4 * 7.085);
        COMPARE_COLOR(at(dotImage, crossing.x(), crossing.y()), SILVER);
        COMPARE_COLOR(at(lineImage, crossing.x(), crossing.y()), SILVER);
        // The middle of a triangle is empty
        COMPARE_COLOR(at(lineImage, crossing.x() + xstep / 3, crossing.y()), Qt::white);
    }

    void brokenConfigIsIgnored() {
        const QImage image = render(pageWith(QStringLiteral("graph"), QStringLiteral("r1=0,,=,x,m1=abc")));
        QCOMPARE(image.pixelColor(0, 0), QColor(Qt::white));
    }
};

QTEST_MAIN(TestRender)
#include "tst_render.moc"
