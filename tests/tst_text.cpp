/*
 * Qournal
 *
 * Tests for the layout of texts and for the LaTeX template. The layout follows Pango, which Xournal++ uses;
 * the values that depend on the installed fonts are only compared with each other.
 *
 * @license GNU GPLv2 or later
 */

#include <cmath>

#include <QImage>
#include <QPainter>
#include <QTest>

#include "LatexTemplate.h"
#include "TextBlock.h"

namespace {

TextStyle style(double size = 12, const QString& align = {}, double wrap = -1, bool justify = false) {
    return TextStyle{QStringLiteral("Sans"), size, align, wrap, justify, false};
}

const QString LOREM = QStringLiteral("Xournal++ is an open-source and cross-platform note-taking software that is "
                                     "fast, flexible, and functional.");

/// Paints a block on white and returns the columns that contain ink: first and last
std::pair<int, int> inkColumns(const TextBlock& block, int width) {
    QImage image(width, 200, QImage::Format_RGB32);
    image.fill(Qt::white);
    {
        QPainter p(&image);
        block.draw(p, Qt::black);
    }
    int first = -1, last = -1;
    for (int x = 0; x < image.width(); ++x) {
        for (int y = 0; y < image.height(); ++y) {
            if (qGray(image.pixel(x, y)) < 128) {
                first = first < 0 ? x : first;
                last = x;
                break;
            }
        }
    }
    return {first, last};
}

}  // namespace

class TestText: public QObject {
    Q_OBJECT

private slots:
    void fontDescriptions_data() {
        QTest::addColumn<QString>("description");
        QTest::addColumn<QString>("family");
        QTest::addColumn<bool>("bold");
        QTest::addColumn<bool>("italic");
        QTest::newRow("family only") << "Sans" << "Sans" << false << false;
        QTest::newRow("several words") << "DejaVu Sans Mono" << "DejaVu Sans Mono" << false << false;
        QTest::newRow("bold") << "Sans Bold" << "Sans" << true << false;
        QTest::newRow("bold italic") << "DejaVu Sans Bold Italic" << "DejaVu Sans" << true << true;
        QTest::newRow("oblique") << "System-ui Oblique" << "System-ui" << false << true;
        // The comma ends the family: "Roman" is part of it, although it is a word for a style as well
        QTest::newRow("comma") << "Times New Roman, Bold" << "Times New Roman" << true << false;
        QTest::newRow("comma without style") << "Times New Roman," << "Times New Roman" << false << false;
        QTest::newRow("style word as family") << "Roman" << "Roman" << false << false;
        QTest::newRow("case") << "sans BOLD italic" << "sans" << true << true;
        QTest::newRow("light is not bold") << "Noto Sans Semi-Condensed Light" << "Noto Sans" << false << false;
        QTest::newRow("empty") << "" << "Sans" << false << false;
    }

    void fontDescriptions() {
        QFETCH(QString, description);
        QFETCH(QString, family);
        QFETCH(bool, bold);
        QFETCH(bool, italic);
        QCOMPARE(TextFont::family(description), family);
        QCOMPARE(TextFont::isBold(description), bold);
        QCOMPARE(TextFont::isItalic(description), italic);
    }

    void fontDescriptionsOfTheUserInterface() {
        QCOMPARE(TextFont::toDescription(QStringLiteral("DejaVu Sans"), false, false), QStringLiteral("DejaVu Sans"));
        QCOMPARE(TextFont::toDescription(QStringLiteral("DejaVu Sans"), true, true),
                 QStringLiteral("DejaVu Sans Bold Italic"));
        QCOMPARE(TextFont::toDescription(QString(), true, false), QStringLiteral("Sans Bold"));
        const QFont font = TextFont::fromDescription(QStringLiteral("Noto Sans Semi-Condensed Light"));
        QCOMPARE(font.weight(), QFont::Light);
        QCOMPARE(font.stretch(), int(QFont::SemiCondensed));
    }

    void linesAndTheirHeight() {
        const TextBlock one(QStringLiteral("Text"), style());
        QCOMPARE(one.lineCount(), 1);
        // Whole units, as Pango measures the font; a line is higher than the letters
        QCOMPARE(one.lineHeight(), std::round(one.lineHeight()));
        QVERIFY(one.lineHeight() >= 12 && one.lineHeight() <= 24);
        QCOMPARE(one.size().height(), one.lineHeight());
        QVERIFY(one.size().width() > 10 && one.size().width() < 40);

        QCOMPARE(TextBlock(QStringLiteral("two\nlines"), style()).size().height(), 2 * one.lineHeight());
        // A line break at the end starts a line, and empty lines count
        QCOMPARE(TextBlock(QStringLiteral("a\n"), style()).lineCount(), 2);
        QCOMPARE(TextBlock(QStringLiteral(" \n odd  whitespace\ttext\n\n"), style()).lineCount(), 4);
        // An empty text is as high as a line
        const TextBlock empty(QString(), style());
        QCOMPARE(empty.size(), QSizeF(0, one.lineHeight()));

        // Twice the size: twice as wide
        const TextBlock large(QStringLiteral("Text"), style(24));
        QVERIFY(std::abs(large.size().width() - 2 * one.size().width()) < 0.01 * one.size().width());
        // Sizes that are not whole numbers
        const TextBlock odd(QStringLiteral("Text"), style(12.34));
        QVERIFY(odd.size().width() > one.size().width() && odd.size().width() < 1.05 * one.size().width());
        // No size: nothing
        QCOMPARE(TextBlock(QStringLiteral("Text"), style(0)).size(), QSizeF());
    }

    void wrapping() {
        const TextBlock unwrapped(LOREM, style());
        QCOMPARE(unwrapped.lineCount(), 1);

        const TextBlock wide(LOREM, style(12, {}, 300));
        const TextBlock narrow(LOREM, style(12, {}, 130));
        QVERIFY(wide.lineCount() >= 2);
        QVERIFY(narrow.lineCount() > wide.lineCount());
        QVERIFY(wide.size().width() <= 300 && wide.size().width() > 200);
        QVERIFY(narrow.size().width() <= 130);
        QCOMPARE(narrow.size().height(), narrow.lineCount() * narrow.lineHeight());

        // Justified, centered and right aligned lines use the whole width
        QCOMPARE(TextBlock(LOREM, style(12, {}, 130, true)).size().width(), 130.0);
        QCOMPARE(TextBlock(LOREM, style(12, QStringLiteral("center"), 130)).size().width(), 130.0);
        QCOMPARE(TextBlock(LOREM, style(12, QStringLiteral("right"), 130)).size().width(), 130.0);
        // The breaks are the same
        QCOMPARE(TextBlock(LOREM, style(12, QStringLiteral("right"), 130)).lineCount(), narrow.lineCount());

        // A word that does not fit sticks out
        const TextBlock word(QStringLiteral("incomprehensibilities"), style(12, QStringLiteral("center"), 20));
        QCOMPARE(word.lineCount(), 1);
        QVERIFY(word.size().width() > 60);
    }

    void alignment() {
        const QString text = QStringLiteral("a long first line\nshort");
        const double width = TextBlock(text, style(20)).size().width();

        // Without a wrap width the lines are aligned within the longest one
        const auto left = inkColumns(TextBlock(QStringLiteral("short"), style(20)), 300);
        QVERIFY(left.first >= 0 && left.first < 4);
        const TextBlock right(text, style(20, QStringLiteral("right")));
        QCOMPARE(right.size().width(), width);
        // Both lines end at the right border now
        QImage image(300, 200, QImage::Format_RGB32);
        image.fill(Qt::white);
        {
            QPainter p(&image);
            right.draw(p, Qt::black);
        }
        auto lastInk = [&](int fromY, int toY) {
            for (int x = image.width() - 1; x >= 0; --x) {
                for (int y = fromY; y < toY; ++y) {
                    if (qGray(image.pixel(x, y)) < 128) {
                        return x;
                    }
                }
            }
            return -1;
        };
        const int lineHeight = static_cast<int>(right.lineHeight());
        QVERIFY(std::abs(lastInk(0, lineHeight) - lastInk(lineHeight, 2 * lineHeight)) <= 2);
        QVERIFY(std::abs(lastInk(0, lineHeight) - width) <= 3);

        // With a wrap width within that width
        const TextBlock centeredBlock(QStringLiteral("short"), style(20, QStringLiteral("center"), 250));
        const auto centered = inkColumns(centeredBlock, 300);
        const int inkWidth = left.second - left.first;
        QVERIFY(std::abs((centered.first + centered.second) / 2.0 - 125) <= 2);
        QVERIFY(std::abs((centered.second - centered.first) - inkWidth) <= 2);
    }

    void drawingScalesWithThePainter() {
        // The text is laid out once and looks the same at every zoom
        const TextBlock block(QStringLiteral("Zoom"), style(20));
        QImage image(400, 200, QImage::Format_RGB32);
        image.fill(Qt::white);
        {
            QPainter p(&image);
            p.scale(3, 3);
            block.draw(p, Qt::black);
        }
        int right = 0, bottom = 0;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qGray(image.pixel(x, y)) < 128) {
                    right = std::max(right, x);
                    bottom = std::max(bottom, y);
                }
            }
        }
        QVERIFY(std::abs(right - 3 * block.size().width()) <= 6);
        QVERIFY(bottom > 1.5 * block.lineHeight() && bottom <= 3 * block.lineHeight());
    }

    void latexTemplate() {
        const QString templ = QStringLiteral("color=%%XPP_TEXT_COLOR%%\n\\(%%XPP_TOOL_INPUT%%\\)\n"
                                             "%%XPP_PREAMBLE%%|%%XPP_UNKNOWN%%|");
        QCOMPARE(Latex::substitute(QStringLiteral("x^2"), templ, QColor(0x12, 0xab, 0xff)),
                 QStringLiteral("color=12abff\n\\(x^2\\)\n||"));

        // Directives define variables and are not part of the formula; the reserved ones cannot be set
        const QString formula = QStringLiteral("%xpp:preamble= \\usepackage{x} \na\n%xpp:TOOL_INPUT=hack\nb");
        QCOMPARE(Latex::substitute(formula, templ, Qt::black),
                 QStringLiteral("color=000000\n\\(a\nb\\)\n\\usepackage{x}||"));

        // The template of Xournal++ takes the formula and the colour
        const QString document = Latex::substitute(QStringLiteral("\\frac{1}{2}"), Latex::defaultTemplate(), Qt::red);
        QVERIFY(document.contains(QStringLiteral("\\frac{1}{2}")));
        QVERIFY(document.contains(QStringLiteral("{HTML}{ff0000}")));
        QVERIFY(!document.contains(QStringLiteral("%%XPP_")));
        QVERIFY(document.contains(QStringLiteral("\\documentclass")));
        QVERIFY(Latex::defaultCommand().contains(QStringLiteral("{}")));
    }
};

QTEST_MAIN(TestText)
#include "tst_text.moc"
