/*
 * Qournal
 *
 * Layout of texts and links. Xournal++ lays them out with Pango; this follows it as closely as Qt allows, so
 * that the texts of existing documents keep their line breaks and their size.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QFont>
#include <QList>
#include <QPainter>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTextLayout>
#include <memory>

struct TextStyle {
    QString font;      ///< description as Pango writes it, without the size: "Sans", "Times New Roman, Bold"
    double size = 12;  ///< height of the font in page units
    QString align;     ///< "left" (also if empty), "center" or "right"
    double wrap = -1;  ///< width at which lines are broken, negative for no wrapping
    bool justify = false;
    bool underline = false;
};

namespace TextFont {

/**
 * The font of a description in the syntax of Pango: a family, followed by words for the style
 * ("Bold", "Italic", "Semi-Condensed", ...). The size is not part of it.
 */
QFont fromDescription(const QString& description);

/// The description of a family with the two styles the user interface offers
QString toDescription(const QString& family, bool bold, bool italic);

QString family(const QString& description);
bool isBold(const QString& description);
bool isItalic(const QString& description);

}  // namespace TextFont

class TextBlock {
public:
    TextBlock(const QString& text, const TextStyle& style);

    /// Width and height of the block in page units, as Pango reports the logical extents
    QSizeF size() const { return m_size; }
    /// Distance between the lines: ascent and descent of the font, each rounded up as Pango does
    double lineHeight() const { return m_lineHeight; }
    int lineCount() const { return m_layout ? m_layout->lineCount() : 0; }

    /// The areas a part of the text covers, one for each line it is on; in page units from the top left corner
    QList<QRectF> rangeRects(int start, int length) const;

    /// Paints the block with its top left corner at the origin
    void draw(QPainter& p, const QColor& color) const;

private:
    std::unique_ptr<QTextLayout> m_layout;
    double m_scale = 0;  ///< from the size the text is laid out at to page units
    double m_lineHeight = 0;
    QSizeF m_size;
    /// Lines that are broken within a word: Pango shows a hyphen at their end, and so is it done here
    QList<int> m_hyphenated;
    QFont m_font;
};
