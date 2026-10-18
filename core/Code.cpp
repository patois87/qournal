#include "Code.h"

#include <QMutex>
#include <QTextDocument>
#include <QTextCharFormat>
#include <algorithm>
#include <memory>

#include "abstracthighlighter.h"
#include "definition.h"
#include "format.h"
#include "repository.h"
#include "state.h"
#include "syntaxhighlighter.h"
#include "theme.h"

namespace {

constexpr int BOX_MIN_CONTRAST = 12;  // of lightness between the box and the paper; less is made more
constexpr double PADDING = 0.4;       // around the code in the box, in heights of lines
constexpr double NUMBER_GAP = 1.5;    // between the numbers of the lines and the code, in widths of digits
constexpr double RADIUS = 0.25;       // of the corners of the box, in heights of lines
constexpr int TAB_SPACES = 4;         // spaces taken away for a tab when lines are indented less

/// The definitions and themes: one set for each thread that draws, as they are not made to be shared
KSyntaxHighlighting::Repository& repository() {
    thread_local std::unique_ptr<KSyntaxHighlighting::Repository> repository;
    if (!repository) {
        repository = std::make_unique<KSyntaxHighlighting::Repository>();
    }
    return *repository;
}

QMutex lookMutex;
Code::Look currentLook;

/// The formats of the highlighter, by their position in the whole text
class Collector: public KSyntaxHighlighting::AbstractHighlighter {
public:
    QList<QTextLayout::FormatRange> highlight(const QString& text) {
        KSyntaxHighlighting::State state;
        for (const QString& line: text.split(u'\n')) {
            state = highlightLine(line, state);
            m_offset += static_cast<int>(line.size()) + 1;
        }
        return m_formats;
    }

protected:
    void applyFormat(int offset, int length, const KSyntaxHighlighting::Format& format) override {
        if (length <= 0 || format.isDefaultTextStyle(theme())) {
            return;
        }
        QTextCharFormat look;
        if (format.hasTextColor(theme())) {
            look.setForeground(format.textColor(theme()));
        }
        if (format.hasBackgroundColor(theme())) {
            look.setBackground(format.backgroundColor(theme()));
        }
        if (format.isBold(theme())) {
            look.setFontWeight(QFont::Bold);
        }
        if (format.isItalic(theme())) {
            look.setFontItalic(true);
        }
        if (format.isUnderline(theme())) {
            look.setFontUnderline(true);
        }
        if (format.isStrikeThrough(theme())) {
            look.setFontStrikeOut(true);
        }
        m_formats.append({m_offset + offset, length, look});
    }

private:
    int m_offset = 0;
    QList<QTextLayout::FormatRange> m_formats;
};

}  // namespace

const QList<Code::Language>& Code::languages() {
    static const QList<Language> languages = {
            {QStringLiteral("bash"), QStringLiteral("Bash"), QStringLiteral("Bash")},
            {QStringLiteral("c"), QStringLiteral("C"), QStringLiteral("C")},
            {QStringLiteral("cpp"), QStringLiteral("ISO C++"), QStringLiteral("C++")},
            {QStringLiteral("csharp"), QStringLiteral("C#"), QStringLiteral("C#")},
            {QStringLiteral("css"), QStringLiteral("CSS"), QStringLiteral("CSS")},
            {QStringLiteral("go"), QStringLiteral("Go"), QStringLiteral("Go")},
            {QStringLiteral("html"), QStringLiteral("HTML"), QStringLiteral("HTML")},
            {QStringLiteral("java"), QStringLiteral("Java"), QStringLiteral("Java")},
            {QStringLiteral("javascript"), QStringLiteral("JavaScript"), QStringLiteral("JavaScript")},
            {QStringLiteral("json"), QStringLiteral("JSON"), QStringLiteral("JSON")},
            {QStringLiteral("kotlin"), QStringLiteral("Kotlin"), QStringLiteral("Kotlin")},
            {QStringLiteral("latex"), QStringLiteral("LaTeX"), QStringLiteral("LaTeX")},
            {QStringLiteral("markdown"), QStringLiteral("Markdown"), QStringLiteral("Markdown")},
            {QStringLiteral("matlab"), QStringLiteral("Matlab"), QStringLiteral("MATLAB")},
            {QStringLiteral("php"), QStringLiteral("PHP/PHP"), QStringLiteral("PHP")},
            {QStringLiteral("powershell"), QStringLiteral("PowerShell"), QStringLiteral("PowerShell")},
            {QStringLiteral("python"), QStringLiteral("Python"), QStringLiteral("Python")},
            {QStringLiteral("ruby"), QStringLiteral("Ruby"), QStringLiteral("Ruby")},
            {QStringLiteral("rust"), QStringLiteral("Rust"), QStringLiteral("Rust")},
            {QStringLiteral("sql"), QStringLiteral("SQL"), QStringLiteral("SQL")},
            {QStringLiteral("swift"), QStringLiteral("Swift"), QStringLiteral("Swift")},
            {QStringLiteral("typescript"), QStringLiteral("TypeScript"), QStringLiteral("TypeScript")},
            {QStringLiteral("xml"), QStringLiteral("XML"), QStringLiteral("XML")},
            {QStringLiteral("yaml"), QStringLiteral("YAML"), QStringLiteral("YAML")},
    };
    return languages;
}

const Code::Language* Code::language(const QString& id) {
    const auto& all = languages();
    const auto found = std::find_if(all.begin(), all.end(), [&id](const Language& l) { return l.id == id; });
    return found == all.end() ? nullptr : &*found;
}

QStringList Code::themes() {
    QStringList names;
    for (const KSyntaxHighlighting::Theme& theme: repository().themes()) {
        names.append(theme.name());
    }
    names.sort(Qt::CaseInsensitive);
    names.removeDuplicates();
    return names;
}

Code::Look Code::look() {
    const QMutexLocker locker(&lookMutex);
    return currentLook;
}

void Code::setLook(const Look& look) {
    const QMutexLocker locker(&lookMutex);
    currentLook = look;
}

namespace {

/// Where the line with the position starts. Not with lastIndexOf(..., -1), which searches from the end
int lineStart(const QString& text, int position) {
    return position <= 0 ? 0 : static_cast<int>(text.lastIndexOf(u'\n', position - 1)) + 1;
}

/// The theme of the look for light or dark paper, or the default one if it is not known
KSyntaxHighlighting::Theme themeFor(const QColor& paper) {
    const bool dark = paper.lightness() < 128;
    const Code::Look current = Code::look();
    KSyntaxHighlighting::Repository& repo = repository();
    KSyntaxHighlighting::Theme theme = repo.theme(dark ? current.darkTheme : current.lightTheme);
    if (!theme.isValid()) {
        theme = repo.defaultTheme(dark ? KSyntaxHighlighting::Repository::DarkTheme :
                                         KSyntaxHighlighting::Repository::LightTheme);
    }
    return theme;
}

}  // namespace

Code::Colors Code::colors(const QString& text, const QString& language, const QColor& paper) {
    KSyntaxHighlighting::Repository& repo = repository();
    const KSyntaxHighlighting::Theme theme = themeFor(paper);

    Colors colors;
    colors.text = QColor::fromRgba(theme.textColor(KSyntaxHighlighting::Theme::Normal));
    colors.lineNumbers = QColor::fromRgba(theme.editorColor(KSyntaxHighlighting::Theme::LineNumbers));
    // The box stands out from the paper, also where the theme has the colour of the paper (white, as GitHub Light)
    colors.background = QColor::fromRgba(theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor));
    if (std::abs(colors.background.lightness() - paper.lightness()) < BOX_MIN_CONTRAST) {
        colors.background = colors.background.lightness() < 128 ? colors.background.lighter(130)
                                                                 : colors.background.darker(104);
    }

    if (const Language* offered = Code::language(language)) {
        Collector collector;
        collector.setDefinition(repo.definitionForName(offered->definition));
        collector.setTheme(theme);
        colors.formats = collector.highlight(text);
    }
    return colors;
}

Code::Frame Code::frame(const QSizeF& size, double lineHeight, double digitWidth, int lines, const Look& look) {
    if (!look.box && !look.lineNumbers) {
        return Frame{QRectF(QPointF(0, 0), size), 0, 0};
    }
    const double padding = look.box ? PADDING * lineHeight : 0;
    double numbers = 0;
    if (look.lineNumbers) {
        const int digits = static_cast<int>(QString::number(std::max(lines, 1)).size());
        numbers = (digits + NUMBER_GAP) * digitWidth;
    }
    const QRectF box(-padding - numbers, -padding, size.width() + 2 * padding + numbers, size.height() + 2 * padding);
    return Frame{box, -NUMBER_GAP * digitWidth, look.box ? RADIUS * lineHeight : 0};
}

void Code::highlight(QTextDocument* document, const QString& language, const QColor& paper) {
    if (!document) {
        return;
    }
    auto* highlighter = document->findChild<KSyntaxHighlighting::SyntaxHighlighter*>(Qt::FindDirectChildrenOnly);
    const Language* offered = Code::language(language);
    if (!offered) {
        delete highlighter;
        return;
    }
    if (!highlighter) {
        highlighter = new KSyntaxHighlighting::SyntaxHighlighter(document);
    }
    const KSyntaxHighlighting::Definition definition = repository().definitionForName(offered->definition);
    const KSyntaxHighlighting::Theme theme = themeFor(paper);
    if (highlighter->definition().name() != definition.name() || highlighter->theme().name() != theme.name()) {
        highlighter->setDefinition(definition);
        highlighter->setTheme(theme);
        highlighter->rehighlight();
    }
}

Code::Change Code::indent(const QString& text, int start, int end, const QString& unit, bool out) {
    start = std::clamp(start, 0, static_cast<int>(text.size()));
    end = std::clamp(end, start, static_cast<int>(text.size()));
    if (end > start && text.at(end - 1) == u'\n') {
        --end;
    }
    const int from = lineStart(text, start);
    int to = static_cast<int>(text.indexOf(u'\n', end));
    if (to < 0) {
        to = static_cast<int>(text.size());
    }

    QStringList lines = text.mid(from, to - from).split(u'\n');
    QList<int> shifts;  // how far each line moves
    for (QString& line: lines) {
        int shift = 0;
        if (!out) {
            // Empty lines stay empty, except the one of the cursor
            if (!line.isEmpty() || lines.size() == 1) {
                line.prepend(unit);
                shift = static_cast<int>(unit.size());
            }
        } else if (!unit.isEmpty() && line.startsWith(unit)) {
            line.remove(0, unit.size());
            shift = -static_cast<int>(unit.size());
        } else {
            // A tab, or the spaces there are, up to the width of a unit (four for a tab)
            int removed = 0;
            if (line.startsWith(u'\t')) {
                removed = 1;
            } else {
                const int width = unit == u"\t" ? TAB_SPACES : static_cast<int>(unit.size());
                while (removed < width && removed < line.size() && line.at(removed) == u' ') {
                    ++removed;
                }
            }
            line.remove(0, removed);
            shift = -removed;
        }
        shifts.append(shift);
    }

    Change change;
    change.from = from;
    change.to = to;
    change.replacement = lines.join(u'\n');
    // The selection keeps its place in the text that moved; it does not go before the start of its line
    int total = 0;
    for (const int shift: shifts) {
        total += shift;
    }
    change.selectionStart = std::max(from, start + shifts.first());
    change.selectionEnd = std::max(change.selectionStart, end + total);
    if (start == end) {
        change.selectionEnd = change.selectionStart;
    }
    return change;
}

QString Code::newLine(const QString& text, int position) {
    position = std::clamp(position, 0, static_cast<int>(text.size()));
    const int from = lineStart(text, position);
    int indent = from;
    while (indent < position && (text.at(indent) == u' ' || text.at(indent) == u'\t')) {
        ++indent;
    }
    return u'\n' + text.mid(from, indent - from);
}
