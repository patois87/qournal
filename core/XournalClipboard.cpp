#include "XournalClipboard.h"

#include <QCoreApplication>
#include <cstring>
#include <stdexcept>

#include "Renderer.h"
#include "XoppLoader.h"

namespace {

/// The first string of every stream of Xournal++ (XML_VERSION_STR)
const QByteArray STREAM_VERSION = QByteArrayLiteral("XojStrm1:");
/// The version this application passes for (PROJECT_STRING of Xournal++)
const QByteArray PROJECT_STRING = QByteArrayLiteral("xournalpp 1.3.8");
/// A point of Xournal++: x, y and the width of the segment that starts there
constexpr quint64 POINT_SIZE = 3 * sizeof(double);
constexpr double NO_PRESSURE = -1;
/// What EditSelectionContents holds before the selection was placed
constexpr double RELATIVE_UNSET = -9999999999.0;
constexpr int HIGHLIGHTER_ALPHA = 0x7f;

// StrokeTool and StrokeCapStyle of Xournal++
enum { TOOL_PEN = 0, TOOL_ERASER = 1, TOOL_HIGHLIGHTER = 2 };
enum { CAP_ROUND = 0, CAP_BUTT = 1, CAP_SQUARE = 2 };

QString tr(const char* text) { return QCoreApplication::translate("XournalClipboard", text); }

/// ObjectOutputStream with BinObjectEncoding
class Writer {
public:
    Writer() { string(STREAM_VERSION); }

    void object(const char* name) {
        m_data += "_{";
        string(name);
    }
    void end() { m_data += "_}"; }
    void integer(qint32 value) { tagged('i', &value, sizeof(value)); }
    void unsignedInteger(quint32 value) { tagged('u', &value, sizeof(value)); }
    void number(double value) { tagged('d', &value, sizeof(value)); }
    void size(quint64 value) { tagged('l', &value, sizeof(value)); }
    void string(const QByteArray& value) { sized('s', value); }
    void image(const QByteArray& value) { sized('m', value); }
    void doubles(const QList<double>& values, quint64 perItem) {
        const quint64 count = static_cast<quint64>(values.size()) * sizeof(double) / perItem;
        tagged('b', &count, sizeof(count));
        raw(&perItem, sizeof(perItem));
        raw(values.constData(), static_cast<size_t>(values.size()) * sizeof(double));
    }
    const QByteArray& data() const { return m_data; }

private:
    void raw(const void* data, size_t length) {
        m_data.append(static_cast<const char*>(data), static_cast<qsizetype>(length));
    }
    void tagged(char type, const void* data, size_t length) {
        m_data += '_';
        m_data += type;
        raw(data, length);
    }
    void sized(char type, const QByteArray& value) {
        const quint64 length = static_cast<quint64>(value.size());
        tagged(type, &length, sizeof(length));
        m_data += value;
    }

    QByteArray m_data;
};

/// ObjectInputStream. Throws std::runtime_error on data it does not expect
class Reader {
public:
    explicit Reader(const QByteArray& data): m_data(data) {}

    void object(const char* name) {
        check('{');
        const QByteArray found = string();
        if (found != name) {
            throw std::runtime_error("unexpected object " + found.toStdString());
        }
    }
    /// The name of the object that comes next, without reading it
    QByteArray nextObject() {
        const qsizetype pos = m_pos;
        check('{');
        const QByteArray name = string();
        m_pos = pos;
        return name;
    }
    void end() { check('}'); }
    qint32 integer() { return plain<qint32>('i'); }
    quint32 unsignedInteger() { return plain<quint32>('u'); }
    double number() { return plain<double>('d'); }
    quint64 size() { return plain<quint64>('l'); }
    QByteArray string() {
        check('s');
        return sizedData();
    }
    QByteArray image() {
        check('m');
        return sizedData();
    }
    QList<double> doubles(quint64 perItem) {
        check('b');
        const quint64 count = value<quint64>();
        const quint64 width = value<quint64>();
        if (width != perItem || count > static_cast<quint64>(remaining()) / perItem) {
            throw std::runtime_error("unexpected size of binary data");
        }
        QList<double> result(static_cast<qsizetype>(count * perItem / sizeof(double)));
        const qsizetype bytes = static_cast<qsizetype>(count * perItem);
        std::memcpy(result.data(), m_data.constData() + m_pos, static_cast<size_t>(bytes));
        m_pos += bytes;
        return result;
    }

private:
    qsizetype remaining() const { return m_data.size() - m_pos; }

    void check(char type) {
        if (remaining() < 2 || m_data[m_pos] != '_' || m_data[m_pos + 1] != type) {
            throw std::runtime_error(std::string("expected a value of type ") + type);
        }
        m_pos += 2;
    }
    template <typename T>
    T value() {
        if (remaining() < static_cast<qsizetype>(sizeof(T))) {
            throw std::runtime_error("unexpected end of the data");
        }
        T result;
        std::memcpy(&result, m_data.constData() + m_pos, sizeof(T));
        m_pos += sizeof(T);
        return result;
    }
    template <typename T>
    T plain(char type) {
        check(type);
        return value<T>();
    }
    QByteArray sizedData() {
        const quint64 length = value<quint64>();
        if (length > static_cast<quint64>(remaining())) {
            throw std::runtime_error("unexpected end of the data");
        }
        const QByteArray result = m_data.mid(m_pos, static_cast<qsizetype>(length));
        m_pos += static_cast<qsizetype>(length);
        return result;
    }

    const QByteArray& m_data;
    qsizetype m_pos = 0;
};

void writeRect(Writer& out, const QRectF& rect) {
    out.number(rect.x());
    out.number(rect.y());
    out.number(rect.width());
    out.number(rect.height());
}

/// Xournal++ keeps the opacity of the highlighter apart from the colour
void writeElement(Writer& out, const QPointF& pos, const QColor& color) {
    out.object("Element");
    out.number(pos.x());
    out.number(pos.y());
    out.unsignedInteger(color.rgb() & 0xffffffU);
    out.end();
}

void writeAudioElement(Writer& out, const QPointF& pos, const QColor& color, const AudioRef& audio) {
    out.object("AudioElement");
    writeElement(out, pos, color);
    out.string(audio.filename.toUtf8());
    out.size(static_cast<quint64>(std::max<qint64>(audio.timestamp, 0)));
    out.end();
}

void writeStroke(Writer& out, const Stroke& stroke) {
    out.object("Stroke");
    writeAudioElement(out, stroke.bounds.topLeft(), stroke.color, stroke.audio);
    out.number(stroke.width);
    out.integer(stroke.tool == Stroke::Tool::Highlighter ? TOOL_HIGHLIGHTER :
                stroke.tool == Stroke::Tool::Eraser      ? TOOL_ERASER :
                                                           TOOL_PEN);
    out.integer(stroke.fill);
    out.integer(stroke.cap == Qt::FlatCap ? CAP_BUTT : stroke.cap == Qt::SquareCap ? CAP_SQUARE : CAP_ROUND);

    const bool pressure = stroke.hasPressure();
    QList<double> points;
    points.reserve(stroke.points.size() * 3);
    for (qsizetype i = 0; i < stroke.points.size(); ++i) {
        points << stroke.points[i].x() << stroke.points[i].y()
               << (pressure && i < stroke.widths.size() ? stroke.widths[i] : NO_PRESSURE);
    }
    out.doubles(points, POINT_SIZE);

    out.object("LineStyle");
    out.doubles(stroke.dashes, sizeof(double));
    out.end();
    out.end();
}

void writeText(Writer& out, const TextElement& text) {
    out.object("Text");
    const QPointF pos = text.matrix ? QPointF((*text.matrix)[4], (*text.matrix)[5]) : text.pos;
    writeAudioElement(out, pos, text.color, text.audio);
    out.string(text.text.toUtf8());
    out.object("XojFont");
    out.string(text.font.toUtf8());
    out.number(text.size);
    out.end();
    out.end();
}

void writeImage(Writer& out, const ImageElement& image) {
    out.object(image.tex ? "TexImage" : "Image");
    // Xournal++ 1.3 cannot rotate images: a rotated one is given the rectangle around it
    const QRectF rect = image.matrix && image.naturalSize.isValid() ?
                                toTransform(*image.matrix).mapRect(QRectF(QPointF(0, 0), image.naturalSize)) :
                                image.rect;
    writeElement(out, rect.topLeft(), Qt::black);
    out.number(rect.width());
    out.number(rect.height());
    if (image.tex) {
        out.string(image.texSource.toUtf8());
        out.string(image.data);
    } else {
        out.image(image.data);
    }
    out.end();
}

struct ElementHead {
    QPointF pos;
    QColor color;
};

ElementHead readElement(Reader& in) {
    in.object("Element");
    ElementHead head;
    head.pos.setX(in.number());
    head.pos.setY(in.number());
    head.color = QColor::fromRgb(in.unsignedInteger() & 0xffffffU);
    in.end();
    return head;
}

ElementHead readAudioElement(Reader& in, AudioRef& audio) {
    in.object("AudioElement");
    const ElementHead head = readElement(in);
    audio.filename = QString::fromUtf8(in.string());
    audio.timestamp = static_cast<qint64>(in.size());
    in.end();
    return head;
}

/// The names of the line styles of the file format, as Xournal++ gives them to its dash patterns (StrokeStyle)
QString styleOf(const QList<double>& dashes) {
    if (dashes.isEmpty()) {
        return {};
    }
    if (dashes == QList<double>{6, 3}) {
        return QStringLiteral("dash");
    }
    if (dashes == QList<double>{6, 3, 0.5, 3}) {
        return QStringLiteral("dashdot");
    }
    if (dashes == QList<double>{0.5, 3}) {
        return QStringLiteral("dot");
    }
    QString style = QStringLiteral("cust:");
    for (double dash: dashes) {
        style += u' ' + QString::number(dash, 'g', 15);
    }
    return style;
}

Stroke readStroke(Reader& in) {
    in.object("Stroke");
    Stroke stroke;
    const ElementHead head = readAudioElement(in, stroke.audio);
    stroke.width = in.number();
    const int tool = in.integer();
    stroke.tool = tool == TOOL_HIGHLIGHTER ? Stroke::Tool::Highlighter :
                  tool == TOOL_ERASER      ? Stroke::Tool::Eraser :
                                             Stroke::Tool::Pen;
    stroke.color = head.color;
    if (stroke.tool == Stroke::Tool::Highlighter) {
        stroke.color.setAlpha(HIGHLIGHTER_ALPHA);
    }
    stroke.fill = in.integer();
    const int cap = in.integer();
    stroke.cap = cap == CAP_BUTT ? Qt::FlatCap : cap == CAP_SQUARE ? Qt::SquareCap : Qt::RoundCap;

    const QList<double> points = in.doubles(POINT_SIZE);
    const qsizetype count = points.size() / 3;
    const bool pressure = count > 1 && points[2] != NO_PRESSURE;
    for (qsizetype i = 0; i < count; ++i) {
        stroke.points.append(QPointF(points[3 * i], points[3 * i + 1]));
        if (pressure && i + 1 < count) {
            // A width that is missing in between: Xournal++ would draw nothing there
            stroke.widths.append(points[3 * i + 2] > 0 ? points[3 * i + 2] : stroke.width);
        }
    }

    in.object("LineStyle");
    stroke.setStyle(styleOf(in.doubles(sizeof(double))));
    in.end();
    in.end();
    stroke.updateBounds();
    return stroke;
}

TextElement readText(Reader& in) {
    in.object("Text");
    TextElement text;
    const ElementHead head = readAudioElement(in, text.audio);
    text.pos = head.pos;
    text.color = head.color;
    text.text = QString::fromUtf8(in.string());
    in.object("XojFont");
    text.font = QString::fromUtf8(in.string());
    text.size = in.number();
    in.end();
    in.end();
    return text;
}

ImageElement readImage(Reader& in, bool tex) {
    in.object(tex ? "TexImage" : "Image");
    ImageElement image;
    image.tex = tex;
    const ElementHead head = readElement(in);
    const double width = in.number();
    const double height = in.number();
    image.rect = QRectF(head.pos, QSizeF(width, height));
    if (tex) {
        image.texSource = QString::fromUtf8(in.string());
        image.data = in.string();
    } else {
        image.data = in.image();
    }
    in.end();
    decodeImageData(image);
    return image;
}

}  // namespace

namespace XournalClipboard {

QString mimeType() { return QStringLiteral("application/xournal"); }

QByteArray write(const std::vector<Element>& elements) {
    std::vector<const Element*> written;
    QRectF bounds;
    for (const Element& element: elements) {
        if (std::holds_alternative<LinkElement>(element)) {
            continue;  // Xournal++ 1.3 has no links, and refuses the whole selection if it meets one
        }
        written.push_back(&element);
        bounds |= Renderer::elementBounds(element);
    }

    Writer out;
    out.string(PROJECT_STRING);

    // The selection: where it is, where it was, and where its elements were when it was made. All the same here
    out.object("EditSelection");
    writeRect(out, bounds);
    writeRect(out, bounds);
    out.number(0);  // rotation
    out.object("EditSelectionContents");
    writeRect(out, bounds);
    writeRect(out, bounds);
    out.number(0);
    out.number(RELATIVE_UNSET);
    out.number(RELATIVE_UNSET);
    out.end();
    out.end();

    out.integer(static_cast<qint32>(written.size()));
    for (const Element* element: written) {
        if (const auto* stroke = std::get_if<Stroke>(element)) {
            writeStroke(out, *stroke);
        } else if (const auto* text = std::get_if<TextElement>(element)) {
            writeText(out, *text);
        } else if (const auto* image = std::get_if<ImageElement>(element)) {
            writeImage(out, *image);
        }
    }
    return out.data();
}

bool read(const QByteArray& data, std::vector<Element>& elements, QString* error) {
    std::vector<Element> result;
    try {
        Reader in(data);
        if (in.string() != STREAM_VERSION) {
            throw std::runtime_error("not a stream of Xournal++");
        }
        const QByteArray version = in.string();
        // Development versions call themselves 1.3 as well, but write another format
        if (!version.startsWith("xournalpp 1.3") || version.contains("+dev")) {
            if (error) {
                *error = tr("The clipboard was filled by \"%1\". Only selections of Xournal++ 1.3 can be pasted.")
                                 .arg(QString::fromUtf8(version));
            }
            return false;
        }

        // Where the selection was: of no use here, the elements have their own positions
        in.object("EditSelection");
        for (int i = 0; i < 9; ++i) {
            in.number();
        }
        in.object("EditSelectionContents");
        for (int i = 0; i < 11; ++i) {
            in.number();
        }
        in.end();
        in.end();

        const int count = in.integer();
        for (int i = 0; i < count; ++i) {
            const QByteArray name = in.nextObject();
            if (name == "Stroke") {
                result.emplace_back(readStroke(in));
            } else if (name == "Text") {
                result.emplace_back(readText(in));
            } else if (name == "Image") {
                result.emplace_back(readImage(in, false));
            } else if (name == "TexImage") {
                result.emplace_back(readImage(in, true));
            } else {
                throw std::runtime_error("unknown object " + name.toStdString());
            }
        }
    } catch (const std::exception& e) {
        if (error) {
            *error = tr("The selection of Xournal++ on the clipboard cannot be read: %1")
                             .arg(QString::fromUtf8(e.what()));
        }
        return false;
    }
    elements = std::move(result);
    return true;
}

}  // namespace XournalClipboard
