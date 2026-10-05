#include "TextBlock.h"

#include <QFontMetricsF>
#include <QTextOption>
#include <cmath>

namespace {

/// Texts are laid out at a fixed large size and scaled down: fonts have no sizes of fractions of a pixel
constexpr double LAYOUT_SIZE = 100;

struct StyleWord {
    const char* word;
    enum { Weight, Style, Stretch, Ignored } kind;
    int value;
};

// The words Pango accepts after the family (pango_font_description_from_string)
const StyleWord STYLE_WORDS[] = {
        {"thin", StyleWord::Weight, QFont::Thin},
        {"ultra-light", StyleWord::Weight, QFont::ExtraLight},
        {"extra-light", StyleWord::Weight, QFont::ExtraLight},
        {"light", StyleWord::Weight, QFont::Light},
        {"semi-light", StyleWord::Weight, 350},
        {"demi-light", StyleWord::Weight, 350},
        {"book", StyleWord::Weight, 380},
        {"regular", StyleWord::Weight, QFont::Normal},
        {"normal", StyleWord::Ignored, 0},
        {"medium", StyleWord::Weight, QFont::Medium},
        {"semi-bold", StyleWord::Weight, QFont::DemiBold},
        {"demi-bold", StyleWord::Weight, QFont::DemiBold},
        {"bold", StyleWord::Weight, QFont::Bold},
        {"ultra-bold", StyleWord::Weight, QFont::ExtraBold},
        {"extra-bold", StyleWord::Weight, QFont::ExtraBold},
        {"heavy", StyleWord::Weight, QFont::Black},
        {"black", StyleWord::Weight, QFont::Black},
        {"ultra-heavy", StyleWord::Weight, QFont::Black},
        {"extra-heavy", StyleWord::Weight, QFont::Black},
        {"ultra-black", StyleWord::Weight, QFont::Black},
        {"extra-black", StyleWord::Weight, QFont::Black},
        {"roman", StyleWord::Style, QFont::StyleNormal},
        {"oblique", StyleWord::Style, QFont::StyleOblique},
        {"italic", StyleWord::Style, QFont::StyleItalic},
        {"small-caps", StyleWord::Ignored, 0},
        {"ultra-condensed", StyleWord::Stretch, QFont::UltraCondensed},
        {"extra-condensed", StyleWord::Stretch, QFont::ExtraCondensed},
        {"condensed", StyleWord::Stretch, QFont::Condensed},
        {"semi-condensed", StyleWord::Stretch, QFont::SemiCondensed},
        {"semi-expanded", StyleWord::Stretch, QFont::SemiExpanded},
        {"expanded", StyleWord::Stretch, QFont::Expanded},
        {"extra-expanded", StyleWord::Stretch, QFont::ExtraExpanded},
        {"ultra-expanded", StyleWord::Stretch, QFont::UltraExpanded},
};

const StyleWord* findStyleWord(QStringView word) {
    for (const StyleWord& style: STYLE_WORDS) {
        if (word.compare(QLatin1StringView(style.word), Qt::CaseInsensitive) == 0) {
            return &style;
        }
    }
    return nullptr;
}

}  // namespace

QFont TextFont::fromDescription(const QString& description) {
    // The style words are at the end, separated by spaces. A comma ends the list of families: "Times New Roman, Bold"
    // has the family "Times New Roman", although "Roman" is a style word as well
    QString familyPart = description.trimmed();
    QList<const StyleWord*> styles;
    while (!familyPart.endsWith(u',')) {
        const qsizetype cut = std::max(familyPart.lastIndexOf(u' '), familyPart.lastIndexOf(u','));
        const StyleWord* style = findStyleWord(QStringView(familyPart).mid(cut + 1));
        if (!style || cut < 0) {
            break;
        }
        styles.append(style);
        familyPart.truncate(familyPart[cut] == u',' ? cut + 1 : cut);
        familyPart = familyPart.trimmed();
    }

    QFont font;
    // Several families are a list of alternatives
    QStringList families;
    for (const QString& entry: familyPart.split(u',', Qt::SkipEmptyParts)) {
        families.append(entry.trimmed());
    }
    font.setFamilies(families.isEmpty() ? QStringList{QStringLiteral("Sans")} : families);
    for (const StyleWord* style: std::as_const(styles)) {
        switch (style->kind) {
            case StyleWord::Weight:
                font.setWeight(static_cast<QFont::Weight>(style->value));
                break;
            case StyleWord::Style:
                font.setStyle(static_cast<QFont::Style>(style->value));
                break;
            case StyleWord::Stretch:
                font.setStretch(style->value);
                break;
            case StyleWord::Ignored:
                break;
        }
    }
    return font;
}

QString TextFont::toDescription(const QString& family, bool bold, bool italic) {
    QString description = family.trimmed().isEmpty() ? QStringLiteral("Sans") : family.trimmed();
    if (bold) {
        description += QStringLiteral(" Bold");
    }
    if (italic) {
        description += QStringLiteral(" Italic");
    }
    return description;
}

QString TextFont::family(const QString& description) { return fromDescription(description).families().value(0); }

bool TextFont::isBold(const QString& description) { return fromDescription(description).weight() >= QFont::DemiBold; }

bool TextFont::isItalic(const QString& description) {
    return fromDescription(description).style() != QFont::StyleNormal;
}

TextBlock::TextBlock(const QString& text, const TextStyle& style) {
    m_scale = style.size / LAYOUT_SIZE;
    if (!(m_scale > 0)) {
        m_scale = 0;
        return;
    }

    QFont font = TextFont::fromDescription(style.font);
    font.setPixelSize(static_cast<int>(LAYOUT_SIZE));
    // Pango positions the glyphs without rounding
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setUnderline(style.underline);
    const QFontMetricsF metrics(font);

    // Pango measures ascent and descent of the font at its size in whole units
    const double ascent = metrics.ascent() * m_scale;
    const double roundedAscent = std::ceil(ascent - 1e-6);
    m_lineHeight = roundedAscent + std::ceil(metrics.descent() * m_scale - 1e-6);

    const bool wrapped = style.wrap >= 0;
    QTextOption option;
    option.setWrapMode(wrapped ? QTextOption::WordWrap : QTextOption::NoWrap);
    option.setUseDesignMetrics(true);
    option.setTabStopDistance(8 * metrics.horizontalAdvance(u' '));
    if (wrapped && style.justify) {
        option.setAlignment(Qt::AlignJustify);
    }

    // Line breaks within one paragraph, so that the lines can be placed one by one
    QString paragraph = text;
    paragraph.replace(u'\n', QChar::LineSeparator);
    m_layout = std::make_unique<QTextLayout>(paragraph, font);
    m_layout->setTextOption(option);
    m_layout->setCacheEnabled(true);

    const double lineWidth = wrapped ? style.wrap / m_scale : 1e7;
    double maxWidth = 0;
    m_layout->beginLayout();
    while (true) {
        QTextLine line = m_layout->createLine();
        if (!line.isValid()) {
            break;
        }
        line.setLeadingIncluded(false);
        line.setLineWidth(lineWidth);
        maxWidth = std::max(maxWidth, line.naturalTextWidth());
    }
    m_layout->endLayout();

    // The lines are aligned within the wrap width, or else within the longest line
    const bool fullWidth = wrapped && (style.justify || style.align == u"center" || style.align == u"right");
    const double blockWidth = wrapped ? lineWidth : maxWidth;
    m_font = font;
    const QFontMetricsF hyphenMetrics(font);
    for (int i = 0; i < m_layout->lineCount(); ++i) {
        QTextLine line = m_layout->lineAt(i);
        const int end = line.textStart() + line.textLength();
        // The last line of a paragraph: the others fill the width when the text is justified
        const bool lastOfParagraph = end >= paragraph.size() || paragraph.at(end - 1) == QChar::LineSeparator;
        // A break between two letters is within a word: Pango puts a hyphen there
        const bool hyphen =
                wrapped && !lastOfParagraph && end > 0 && end < paragraph.size() &&
                paragraph.at(end - 1).isLetterOrNumber() &&
                (paragraph.at(end).isLetterOrNumber() || paragraph.at(end).category() == QChar::Letter_Modifier ||
                 paragraph.at(end).category() == QChar::Symbol_Modifier);
        if (hyphen) {
            m_hyphenated.append(i);
        }
        const double width = line.naturalTextWidth() + (hyphen ? hyphenMetrics.horizontalAdvance(u'-') : 0);
        double x = 0;
        if (style.justify && wrapped && !lastOfParagraph) {
            x = 0;  // justified: the line fills the width
        } else if (style.align == u"center") {
            x = (blockWidth - width) / 2;
        } else if (style.align == u"right") {
            x = blockWidth - width;
        }
        // The baseline is where Pango puts it: the rounded ascent below the top of the line
        const double top = i * m_lineHeight + roundedAscent - ascent;
        line.setPosition(QPointF(x, top / m_scale));
    }

    const int lines = std::max(m_layout->lineCount(), 1);
    // A word that is longer than the wrap width sticks out
    m_size = QSizeF((fullWidth ? std::max(blockWidth, maxWidth) : maxWidth) * m_scale, lines * m_lineHeight);
}

QList<QRectF> TextBlock::rangeRects(int start, int length) const {
    QList<QRectF> rects;
    if (!m_layout || length <= 0) {
        return rects;
    }
    const int end = start + length;
    for (int i = 0; i < m_layout->lineCount(); ++i) {
        const QTextLine line = m_layout->lineAt(i);
        const int from = std::max(start, line.textStart());
        const int to = std::min(end, line.textStart() + line.textLength());
        if (from >= to) {
            continue;
        }
        const double x1 = line.cursorToX(from);
        const double x2 = line.cursorToX(to);
        rects.append(QRectF(std::min(x1, x2) * m_scale, i * m_lineHeight, std::abs(x2 - x1) * m_scale, m_lineHeight));
    }
    return rects;
}

void TextBlock::draw(QPainter& p, const QColor& color) const {
    if (!m_layout || m_scale <= 0) {
        return;
    }
    p.save();
    p.scale(m_scale, m_scale);
    p.setPen(color);
    m_layout->draw(&p, QPointF(0, 0));
    if (!m_hyphenated.isEmpty()) {
        p.setFont(m_font);
        for (int index: m_hyphenated) {
            const QTextLine line = m_layout->lineAt(index);
            p.drawText(QPointF(line.x() + line.naturalTextWidth(), line.y() + line.ascent()), QStringLiteral("-"));
        }
    }
    p.restore();
}
