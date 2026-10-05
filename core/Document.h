/*
 * Qournal
 *
 * Document model. It only uses QtCore/QtGui value types and holds everything the .xopp format can
 * store, so that loading and saving a file does not lose information.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <array>
#include <optional>
#include <variant>
#include <vector>

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <QTransform>

/// Affine transformation as stored in the file: xx yx xy yy x0 y0
using Matrix = std::array<double, 6>;

inline QTransform toTransform(const Matrix& m) { return QTransform(m[0], m[1], m[2], m[3], m[4], m[5]); }

/// Position in an audio recording an element was created at
struct AudioRef {
    QString filename;  ///< empty if there is no recording
    qint64 timestamp = 0;
};

struct Stroke {
    enum class Tool { Pen, Highlighter, Eraser };

    Tool tool = Tool::Pen;
    QColor color = Qt::black;
    double width = 1.41;
    int fill = -1;  ///< fill opacity (0-255), -1 for no fill
    Qt::PenCapStyle cap = Qt::RoundCap;
    QString style;        ///< line style as stored in the file ("dash", "dot", "cust: ..."), empty for solid
    QList<qreal> dashes;  ///< dash pattern derived from style, empty for a solid line
    AudioRef audio;

    QList<QPointF> points;
    QList<double> widths;  ///< one width per segment for pressure sensitive strokes, empty otherwise

    QRectF bounds;

    bool hasPressure() const { return !widths.isEmpty() && widths.size() == points.size() - 1; }
    void setStyle(const QString& style);
    void updateBounds();
};

struct TextElement {
    QString text;
    QString font;  ///< Pango-like description, e.g. "Times New Roman, Bold"
    double size = 12;
    QPointF pos;
    std::optional<Matrix> matrix;  ///< replaces pos if set
    QColor color = Qt::black;
    double wrap = -1;  ///< wrap width, negative for no wrapping
    QString align;     ///< "left" (also if empty), "center" or "right"
    bool justify = false;
    AudioRef audio;
};

/// A raster image or a rendered LaTeX formula
struct ImageElement {
    bool tex = false;
    QString texSource;  ///< LaTeX code, for formulas only
    QByteArray data;    ///< the embedded file (PNG, PDF, ...), kept as it is for saving
    QImage image;       ///< decoded data, null if it could not be decoded

    QRectF rect;
    std::optional<Matrix> matrix;  ///< maps (0, 0, naturalSize) to the page; replaces rect if set
    QSizeF naturalSize;            ///< invalid if unknown
};

struct LinkElement {
    QString text;
    QString url;
    QString font;
    double size = 12;
    Matrix matrix{1, 0, 0, 1, 0, 0};
    QColor color = Qt::black;
    QString align;
};

using Element = std::variant<Stroke, TextElement, ImageElement, LinkElement>;

/// Moves an element on its page
void translateElement(Element& element, const QPointF& delta);

struct Layer {
    QString name;  ///< null if the layer has no name
    std::vector<Element> elements;

    bool visible = true;  ///< state of the view: not stored in the file
};

struct Background {
    enum class Type { Solid, Pdf, Pixmap };

    Type type = Type::Solid;
    QString name;  ///< null if not set

    // Solid
    QColor color = Qt::white;
    QString style = QStringLiteral("plain");
    QString config;

    // Pdf
    int pdfPage = -1;  ///< 0-based

    // Pixmap
    QString domain;  ///< "absolute", "attach" or "clone"
    QString filename;
    QImage pixmap;
};

struct Page {
    double width = 595.27559;  // A4 in points
    double height = 841.88976;
    Background background;
    std::vector<Layer> layers;

    // State of the view: not stored in the file
    bool backgroundVisible = true;
    int currentLayer = -1;  ///< the layer that is drawn on; negative for the top layer

    /// Index of the layer that is drawn on, -1 if the page has no layer
    int activeLayer() const {
        const int count = static_cast<int>(layers.size());
        return currentLayer < 0 || currentLayer >= count ? count - 1 : currentLayer;
    }
};

struct Document {
    std::vector<Page> pages;

    QString sourcePath;  ///< the file the document was loaded from, empty for new documents

    // Background PDF
    QString pdfDomain;    ///< "absolute" or "attach", as stored in the file
    QString pdfFilename;  ///< as stored in the file
    QString pdfPath;      ///< resolved location, empty if there is no background PDF

    static Document createEmpty();
};
