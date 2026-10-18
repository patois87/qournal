/*
 * Qournal
 *
 * Code in texts: the languages that are offered, their highlighting with KSyntaxHighlighting, and how code looks,
 * as code blocks look in Markdown: in a box, with the numbers of the lines on the left. The text itself is
 * ordinary; only its language is stored with it (TextElement::code).
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QColor>
#include <QList>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QStringList>
#include <QTextLayout>

class QTextDocument;

namespace Code {

struct Language {
    QString id;          ///< as stored in the file: "python", "cpp", ...
    QString definition;  ///< the name of the definition of KSyntaxHighlighting
    QString name;        ///< as the user interface shows it
};

/// The languages offered, in the order the user interface shows them
const QList<Language>& languages();

/// The language with the id, or nullptr if none is offered with it
const Language* language(const QString& id);

/// The names of the colour themes, sorted
QStringList themes();

/// How code looks: for the screen and for exports alike
struct Look {
    QString lightTheme = QStringLiteral("GitHub Light");  ///< on light paper
    QString darkTheme = QStringLiteral("GitHub Dark");    ///< on dark paper
    bool lineNumbers = true;
    bool box = true;  ///< a box with the background of the theme around the code
};

/// The look of code in this application; it is set from the settings
Look look();
void setLook(const Look& look);

/// The colours of a piece of code
struct Colors {
    QColor text;         ///< of what is not highlighted
    QColor background;   ///< of the box, which stands out from the paper
    QColor lineNumbers;
    QList<QTextLayout::FormatRange> formats;  ///< by the positions in the text, whose lines are separated by '\n'
};

/**
 * The highlighting of the code with the theme for the paper it is on. A language that is not offered gives the
 * colours of the theme without highlighting
 */
Colors colors(const QString& text, const QString& language, const QColor& paper);

/// Where the box and the numbers of the lines go around the text, in the units of its layout
struct Frame {
    QRectF box;           ///< around the text and the numbers, relative to the top left corner of the text
    double numbersRight;  ///< the right edge of the numbers; they end left of the text
    double radius;        ///< of the corners of the box
};

/**
 * The frame of code of the size, whose lines are lineHeight high, with digitWidth as wide as a digit, for the
 * number of lines given
 */
Frame frame(const QSizeF& size, double lineHeight, double digitWidth, int lines, const Look& look);

/**
 * Highlights the code in a document while it is edited, with the theme for the paper; a language that is not
 * offered takes the highlighting away
 */
void highlight(QTextDocument* document, const QString& language, const QColor& paper);

/// A change of the text in an editor: what replaces the characters from to to, and the selection after it
struct Change {
    int from = 0;
    int to = 0;
    QString replacement;
    int selectionStart = 0;
    int selectionEnd = 0;
};

/**
 * Indents the lines the selection from start to end is on by one unit (spaces or a tab), or with out takes one
 * unit away. A selection that ends at the start of a line does not take that line
 */
Change indent(const QString& text, int start, int end, const QString& unit, bool out);

/// What a new line at the position gets: the line break and the indentation of the line it is in
QString newLine(const QString& text, int position);

}  // namespace Code
