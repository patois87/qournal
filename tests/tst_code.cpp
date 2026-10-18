/*
 * Qournal
 *
 * Tests for code in texts: the languages and themes of KSyntaxHighlighting that are built in, the highlighting,
 * and how code is drawn
 *
 * @license GNU GPLv2 or later
 */

#include <QPainter>
#include <QTest>
#include <QTextBlock>
#include <QTextDocument>

#include "Code.h"
#include "Document.h"
#include "Renderer.h"
#include "abstracthighlighter.h"
#include "definition.h"
#include "format.h"
#include "repository.h"
#include "state.h"
#include "theme.h"

namespace {

/// The parts of the lines and their formats, as the highlighter gives them
class Collector: public KSyntaxHighlighting::AbstractHighlighter {
public:
    struct Part {
        QString text;
        KSyntaxHighlighting::Format format;
    };

    QList<Part> highlight(const QString& code) {
        KSyntaxHighlighting::State state;
        for (const QString& line: code.split(u'\n')) {
            m_line = line;
            state = highlightLine(line, state);
        }
        return m_parts;
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override {
        m_parts.append({m_line.mid(offset, length), format});
    }

private:
    QString m_line;
    QList<Part> m_parts;
};

}  // namespace

class TestCode: public QObject {
    Q_OBJECT

private slots:
    void languagesAreBuiltIn() {
        KSyntaxHighlighting::Repository repository;
        for (const char* name: {"Python", "C", "ISO C++", "C#", "Java", "JavaScript", "TypeScript", "Kotlin", "Swift",
                                "Go", "Rust", "PHP/PHP", "Ruby", "SQL", "Bash", "PowerShell", "HTML", "XML", "CSS",
                                "JSON", "YAML", "Matlab", "Markdown", "LaTeX"}) {
            QVERIFY2(repository.definitionForName(QString::fromLatin1(name)).isValid(), name);
        }
        // Not built in, see THIRD-PARTY.md
        QVERIFY(!repository.definitionForName(QStringLiteral("R Script")).isValid());
        QVERIFY(repository.theme(QStringLiteral("GitHub Light")).isValid());
        QVERIFY(repository.theme(QStringLiteral("GitHub Dark")).isValid());
        QVERIFY(repository.themes().size() >= 20);
    }

    void offeredLanguages() {
        KSyntaxHighlighting::Repository repository;
        QCOMPARE(Code::languages().size(), 24);
        QStringList ids;
        for (const Code::Language& language: Code::languages()) {
            QVERIFY2(repository.definitionForName(language.definition).isValid(), qPrintable(language.id));
            ids.append(language.id);
        }
        ids.removeDuplicates();
        QCOMPARE(ids.size(), 24);
        QCOMPARE(Code::language(QStringLiteral("cpp"))->name, QStringLiteral("C++"));
        QVERIFY(!Code::language(QStringLiteral("r")));
        QVERIFY(Code::themes().contains(QStringLiteral("Solarized Light")));
    }

    void colorsForThePaper() {
        const QString code = QStringLiteral("def f(x):\n    return x  # twice");
        Code::Colors light = Code::colors(code, QStringLiteral("python"), Qt::white);
        QCOMPARE(light.text, QColor(0x24, 0x29, 0x2e));  // GitHub Light
        // The theme has the colour of the paper: the box is a little darker
        QVERIFY(light.background != QColor(Qt::white));
        QVERIFY(light.background.lightness() > 230);
        // "def" in the first line, the comment in the second, by their positions in the whole text
        QVERIFY(!light.formats.isEmpty());
        QCOMPARE(light.formats.first().start, 0);
        QCOMPARE(light.formats.first().length, 3);
        QCOMPARE(light.formats.last().start, code.indexOf(u'#'));

        const Code::Colors dark = Code::colors(code, QStringLiteral("python"), QColor(0x20, 0x20, 0x20));
        QVERIFY(dark.text.lightness() > 200);  // GitHub Dark
        QVERIFY(dark.background.lightness() < 100);

        // Not offered: the colours of the theme, without highlighting
        const Code::Colors unknown = Code::colors(code, QStringLiteral("a-language-from-later"), Qt::white);
        QVERIFY(unknown.formats.isEmpty());
        QCOMPARE(unknown.text, light.text);
    }

    void framesTheCode() {
        Code::Look look;
        Code::Frame frame = Code::frame(QSizeF(100, 40), 10, 5, 12, look);
        // Two digits and the gap left of the text, a padding around all
        QCOMPARE(frame.numbersRight, -7.5);
        QCOMPARE(frame.box, QRectF(-4 - 17.5, -4, 100 + 8 + 17.5, 48));
        QVERIFY(frame.radius > 0);
        look.lineNumbers = false;
        QCOMPARE(Code::frame(QSizeF(100, 40), 10, 5, 12, look).box, QRectF(-4, -4, 108, 48));
        look.box = false;
        QCOMPARE(Code::frame(QSizeF(100, 40), 10, 5, 12, look).box, QRectF(0, 0, 100, 40));
    }

    void drawsCode() {
        Page page;
        page.width = 300;
        page.height = 200;
        page.layers.emplace_back();
        TextElement text;
        text.text = QStringLiteral("for i in range(10):\n    print(i)  # count\n\nreturn");
        text.font = QStringLiteral("Monospace");
        text.size = 12;
        text.pos = QPointF(60, 40);
        text.code = QStringLiteral("python");
        page.layers.front().elements.emplace_back(text);

        // The box and the numbers are left of the text, and are part of what it covers
        const QRectF bounds = Renderer::elementBounds(page.layers.front().elements.front());
        QVERIFY(bounds.left() < 50);
        QVERIFY(bounds.top() < 40);
        TextElement plain = text;
        plain.code.clear();
        QVERIFY(Renderer::elementBounds(plain).left() == 60);

        QImage image(600, 400, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        {
            QPainter p(&image);
            p.setRenderHint(QPainter::Antialiasing);
            p.scale(2, 2);
            Renderer::renderLayers(p, page, QRectF(0, 0, 300, 200));
        }
        if (qEnvironmentVariableIsSet("CODE_IMAGES")) {
            image.save(qEnvironmentVariable("CODE_IMAGES") + QStringLiteral("/code.png"));
        }
        const Code::Colors colors = Code::colors(text.text, text.code, Qt::white);
        // Inside the box, between its edge and the numbers: its colour; outside: the paper
        const QPoint inBox = (QPointF(bounds.left() + 1.5, bounds.center().y()) * 2).toPoint();
        QCOMPARE(image.pixelColor(inBox).rgb(), colors.background.rgb());
        QCOMPARE(image.pixelColor((QPointF(bounds.left() - 3, bounds.center().y()) * 2).toPoint()).rgb(),
                 QColor(Qt::white).rgb());
        // Highlighted: colours other than the text, the box and the paper
        bool highlighted = false;
        for (int y = 80; y < 120 && !highlighted; ++y) {
            for (int x = 120; x < 400 && !highlighted; ++x) {
                const QColor c = image.pixelColor(x, y);
                highlighted = c.saturation() > 100 && c.value() > 80;
            }
        }
        QVERIFY(highlighted);
    }

    void indentsLines() {
        auto apply = [](const QString& text, const Code::Change& change) {
            QString result = text;
            result.replace(change.from, change.to - change.from, change.replacement);
            return result;
        };
        const QString text = QStringLiteral("a\n  b\n\nc");
        const QString four = QStringLiteral("    ");

        // The line of the cursor; the cursor moves with it
        Code::Change change = Code::indent(text, 1, 1, four, false);
        QCOMPARE(apply(text, change), QStringLiteral("    a\n  b\n\nc"));
        QCOMPARE(change.selectionStart, 5);
        QCOMPARE(change.selectionEnd, 5);

        // All lines of the selection, not the empty one, and the selection grows with them
        change = Code::indent(text, 0, static_cast<int>(text.size()), four, false);
        QCOMPARE(apply(text, change), QStringLiteral("    a\n      b\n\n    c"));
        QCOMPARE(change.selectionStart, 4);
        QCOMPARE(change.selectionEnd, static_cast<int>(text.size()) + 12);  // the end of the text

        // A selection that ends at the start of a line does not take that line
        change = Code::indent(text, 0, 2, four, false);
        QCOMPARE(apply(text, change), QStringLiteral("    a\n  b\n\nc"));

        // Out: a unit, or the spaces there are, or a tab
        change = Code::indent(QStringLiteral("      x\n  y\n\tz"), 0, 15, four, true);
        QCOMPARE(apply(QStringLiteral("      x\n  y\n\tz"), change), QStringLiteral("  x\ny\nz"));
        change = Code::indent(QStringLiteral("x"), 0, 0, four, true);
        QCOMPARE(change.replacement, QStringLiteral("x"));
        QCOMPARE(change.selectionStart, 0);
        // With tabs as the unit: a tab in, a tab or up to four spaces out
        QCOMPARE(Code::indent(QStringLiteral("x"), 0, 0, QStringLiteral("\t"), false).replacement, QStringLiteral("\tx"));
        QCOMPARE(Code::indent(QStringLiteral("      x"), 0, 0, QStringLiteral("\t"), true).replacement,
                 QStringLiteral("  x"));
    }

    void newLinesKeepTheIndentation() {
        const QString text = QStringLiteral("def f():\n    if x:\n\treturn");
        QCOMPARE(Code::newLine(text, 8), QStringLiteral("\n"));
        QCOMPARE(Code::newLine(text, static_cast<int>(text.indexOf(u':', 10)) + 1), QStringLiteral("\n    "));
        QCOMPARE(Code::newLine(text, static_cast<int>(text.size())), QStringLiteral("\n\t"));
        // In the indentation itself: only what is before the cursor
        QCOMPARE(Code::newLine(text, 11), QStringLiteral("\n  "));
    }

    void highlightsWhileEditing() {
        QTextDocument document;
        document.setPlainText(QStringLiteral("def f(x):\n    return x"));
        Code::highlight(&document, QStringLiteral("python"), Qt::white);
        auto keywordColor = [&document]() {
            const QTextLayout* layout = document.firstBlock().layout();
            for (const QTextLayout::FormatRange& range: layout->formats()) {
                if (range.start == 0 && range.length == 3) {
                    return range.format.foreground().color();
                }
            }
            return QColor();
        };
        QVERIFY(keywordColor().isValid());
        QCOMPARE(keywordColor(), QColor(0xd7, 0x3a, 0x49));  // GitHub Light
        // On dark paper the theme for it; without a language no highlighting
        Code::highlight(&document, QStringLiteral("python"), Qt::black);
        QCOMPARE(keywordColor(), QColor(0xf9, 0x75, 0x83));  // GitHub Dark
        Code::highlight(&document, QString(), Qt::white);
        QVERIFY(!keywordColor().isValid());
    }

    void highlightsPython() {
        KSyntaxHighlighting::Repository repository;
        Collector collector;
        collector.setDefinition(repository.definitionForName(QStringLiteral("Python")));
        const KSyntaxHighlighting::Theme theme = repository.theme(QStringLiteral("GitHub Light"));
        collector.setTheme(theme);
        const auto parts = collector.highlight(QStringLiteral("def f(x):\n    return 'a'  # comment"));

        auto find = [&parts](const QString& text) {
            for (const auto& part: parts) {
                if (part.text == text) {
                    return part.format;
                }
            }
            return KSyntaxHighlighting::Format();
        };
        const QColor normal = theme.textColor(KSyntaxHighlighting::Theme::Normal);
        QVERIFY(find(QStringLiteral("def")).textColor(theme) != normal);
        QCOMPARE(find(QStringLiteral("return")).textStyle(), KSyntaxHighlighting::Theme::ControlFlow);
        // A string comes in parts: its quotes and what is between them
        QCOMPARE(find(QStringLiteral("a")).textStyle(), KSyntaxHighlighting::Theme::String);
        QCOMPARE(find(QStringLiteral("# comment")).textStyle(), KSyntaxHighlighting::Theme::Comment);
    }
};

QTEST_MAIN(TestCode)
#include "tst_code.moc"
