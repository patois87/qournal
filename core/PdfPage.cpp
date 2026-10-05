#include "PdfPage.h"

#include <QBuffer>
#include <QFont>
#include <QImage>
#include <QLinearGradient>
#include <QPainterPath>
#include <QPicture>
#include <QRawFont>
#include <QtMath>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <optional>
#include <variant>

#include "PdfEncodings.h"
#include "PdfFunction.h"

namespace Pdf {

namespace {

/// Gives up on a page: what was found cannot be drawn here
struct Unsupported {
    QString what;
};

constexpr int MAX_FORM_DEPTH = 12;
constexpr qint64 MAX_OPERATIONS = 5'000'000;
/// The size the fonts are loaded at: the glyphs come in thousandths of the size, as the widths of PDF fonts
constexpr double FONT_UNITS = 1000.0;
/// The colours a gradient is made of: its function is evaluated at so many places
constexpr int GRADIENT_STOPS = 64;
/// The most cells of a tiling pattern that are drawn for one area
constexpr int MAX_TILES = 4000;
/// What a soft mask covers is drawn as an image, at this many pixels per unit of the page (300 dpi for a page drawn
/// in points), and with at most so many pixels
constexpr double SOFT_MASK_SCALE = 300.0 / 72.0;
constexpr qint64 SOFT_MASK_MAX_PIXELS = 6000LL * 6000LL;

// Operands of the content streams

struct Operand;
using OperandArray = std::vector<Operand>;
using OperandDict = std::vector<std::pair<QByteArray, Operand>>;

struct Operand {
    enum class Kind { Null, Number, Name, String, Array, Dict, Bool };
    Kind kind = Kind::Null;
    double number = 0;
    QByteArray text;  ///< name, or the bytes of a string
    std::shared_ptr<OperandArray> array;
    std::shared_ptr<OperandDict> dict;

    const Operand* find(const QByteArray& key) const {
        if (dict) {
            for (const auto& [k, v]: *dict) {
                if (k == key) {
                    return &v;
                }
            }
        }
        return nullptr;
    }
};

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }
bool isDelimiter(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
           c == '%';
}

/// Reads a content stream: operands, and operators as keywords
class Lexer {
public:
    explicit Lexer(const QByteArray& data): d(data) {}

    bool atEnd() {
        skipSpace();
        return p >= d.size();
    }
    qsizetype pos() const { return p; }
    void setPos(qsizetype pos) { p = pos; }
    const QByteArray& data() const { return d; }

    /// The next token: an operand, or (as a name with isOperator set) an operator
    Operand next(bool& isOperator) {
        isOperator = false;
        skipSpace();
        if (p >= d.size()) {
            return {};
        }
        const char c = d[p];
        Operand result;
        if (c == '/') {
            ++p;
            result.kind = Operand::Kind::Name;
            result.text = name();
        } else if (c == '(') {
            ++p;
            result.kind = Operand::Kind::String;
            result.text = literalString();
        } else if (c == '<' && p + 1 < d.size() && d[p + 1] == '<') {
            p += 2;
            result.kind = Operand::Kind::Dict;
            result.dict = std::make_shared<OperandDict>();
            while (!atEnd() && !(d[p] == '>' && p + 1 < d.size() && d[p + 1] == '>')) {
                bool op = false;
                const Operand key = next(op);
                if (key.kind != Operand::Kind::Name) {
                    break;
                }
                result.dict->emplace_back(key.text, next(op));
            }
            p += 2;
        } else if (c == '<') {
            ++p;
            result.kind = Operand::Kind::String;
            QByteArray digits;
            while (p < d.size() && d[p] != '>') {
                if (std::isxdigit(static_cast<uchar>(d[p]))) {
                    digits.append(d[p]);
                }
                ++p;
            }
            ++p;
            if (digits.size() % 2) {
                digits.append('0');
            }
            result.text = QByteArray::fromHex(digits);
        } else if (c == '[') {
            ++p;
            result.kind = Operand::Kind::Array;
            result.array = std::make_shared<OperandArray>();
            while (!atEnd() && d[p] != ']') {
                bool op = false;
                Operand item = next(op);
                if (op) {
                    break;  // not in an array: the content is broken
                }
                result.array->push_back(std::move(item));
            }
            ++p;
        } else if (c == '+' || c == '-' || c == '.' || std::isdigit(static_cast<uchar>(c))) {
            const qsizetype start = p;
            ++p;
            while (p < d.size() && (std::isdigit(static_cast<uchar>(d[p])) || d[p] == '.')) {
                ++p;
            }
            result.kind = Operand::Kind::Number;
            result.number = d.mid(start, p - start).toDouble();
        } else if (c == ']' || c == ')' || c == '>' || c == '}' || c == '{') {
            ++p;  // stray delimiter
            return next(isOperator);
        } else {
            const qsizetype start = p;
            while (p < d.size() && !isSpace(d[p]) && !isDelimiter(d[p])) {
                ++p;
            }
            result.text = d.mid(start, p - start);
            if (result.text == "true" || result.text == "false") {
                result.kind = Operand::Kind::Bool;
                result.number = result.text == "true";
            } else if (result.text == "null") {
                result.kind = Operand::Kind::Null;
            } else {
                result.kind = Operand::Kind::Name;
                isOperator = true;
            }
        }
        return result;
    }

    void skipSpace() {
        while (p < d.size()) {
            if (isSpace(d[p])) {
                ++p;
            } else if (d[p] == '%') {
                while (p < d.size() && d[p] != '\n' && d[p] != '\r') {
                    ++p;
                }
            } else {
                break;
            }
        }
    }

private:
    QByteArray name() {
        QByteArray out;
        while (p < d.size() && !isSpace(d[p]) && !isDelimiter(d[p])) {
            if (d[p] == '#' && p + 2 < d.size()) {
                out.append(static_cast<char>(QByteArray::fromHex(d.mid(p + 1, 2)).at(0)));
                p += 3;
            } else {
                out.append(d[p++]);
            }
        }
        return out;
    }

    QByteArray literalString() {
        QByteArray out;
        int depth = 1;
        while (p < d.size()) {
            char c = d[p++];
            if (c == '\\' && p < d.size()) {
                c = d[p++];
                switch (c) {
                    case 'n':
                        out.append('\n');
                        break;
                    case 'r':
                        out.append('\r');
                        break;
                    case 't':
                        out.append('\t');
                        break;
                    case 'b':
                        out.append('\b');
                        break;
                    case 'f':
                        out.append('\f');
                        break;
                    case '\r':
                        if (p < d.size() && d[p] == '\n') {
                            ++p;
                        }
                        break;
                    case '\n':
                        break;
                    default:
                        if (c >= '0' && c <= '7') {
                            int value = c - '0';
                            for (int i = 0; i < 2 && p < d.size() && d[p] >= '0' && d[p] <= '7'; ++i) {
                                value = value * 8 + (d[p++] - '0');
                            }
                            out.append(static_cast<char>(value & 0xff));
                        } else {
                            out.append(c);
                        }
                }
            } else if (c == '(') {
                ++depth;
                out.append(c);
            } else if (c == ')') {
                if (--depth == 0) {
                    break;
                }
                out.append(c);
            } else {
                out.append(c);
            }
        }
        return out;
    }

    const QByteArray& d;
    qsizetype p = 0;
};

/// A value of the file as an operand, for dictionaries that come from the file (inline images have their own)
double number(const Reader& reader, const Dict& dict, const char* key, double fallback) {
    const Value* value = dict.find(key);
    if (!value) {
        return fallback;
    }
    const Value resolved = reader.resolve(*value);
    return resolved.kind() == Value::Kind::Number ? resolved.toNumber() : fallback;
}

Value get(const Reader& reader, const Dict& dict, const char* key) {
    const Value* value = dict.find(key);
    return value ? reader.resolve(*value) : Value();
}

QTransform matrixOf(const Reader& reader, const Value& value) {
    if (value.kind() != Value::Kind::Array || value.toArray().size() != 6) {
        return {};
    }
    double m[6];
    for (size_t i = 0; i < 6; ++i) {
        m[i] = reader.resolve(value.toArray()[i]).toNumber();
    }
    return QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
}

QRectF rectOf(const Reader& reader, const Value& value) {
    if (value.kind() != Value::Kind::Array || value.toArray().size() != 4) {
        return {};
    }
    double n[4];
    for (size_t i = 0; i < 4; ++i) {
        n[i] = reader.resolve(value.toArray()[i]).toNumber();
    }
    return QRectF(QPointF(std::min(n[0], n[2]), std::min(n[1], n[3])),
                  QPointF(std::max(n[0], n[2]), std::max(n[1], n[3])));
}

/// A string of the file as bytes: the file keeps it as it is written, with its delimiters
QByteArray stringBytes(const Value& value) {
    QByteArray text = value.text();
    if (text.startsWith('<')) {
        return QByteArray::fromHex(text.mid(1, text.size() - 2));
    }
    Lexer lexer(text);
    bool op = false;
    return lexer.next(op).text;
}

char16_t unicodeOfName(const QByteArray& name) {
    const auto& table = Encodings::UNICODE_BY_NAME;
    const auto end = std::end(table);
    const auto it = std::lower_bound(std::begin(table), end, name, [](const auto& entry, const QByteArray& n) {
        return std::strcmp(entry.first, n.constData()) < 0;
    });
    if (it != end && name == it->first) {
        return it->second;
    }
    bool ok = false;
    if (name.startsWith("uni") && name.size() >= 7) {
        const uint code = name.mid(3, 4).toUInt(&ok, 16);
        return ok ? static_cast<char16_t>(code) : 0;
    }
    if (name.startsWith('u') && name.size() >= 5 && name.size() <= 7) {
        const uint code = name.mid(1).toUInt(&ok, 16);
        return ok && code < 0x10000 ? static_cast<char16_t>(code) : 0;
    }
    // Variants of a glyph: "a.sc", "one.oldstyle"
    if (const qsizetype dot = name.indexOf('.'); dot > 0) {
        return unicodeOfName(name.left(dot));
    }
    return 0;
}

/// The encoding built into a Type 1 font program: "dup <code> /<name> put" in its clear text part
std::array<QByteArray, 256> type1Encoding(const QByteArray& program) {
    std::array<QByteArray, 256> names;
    const qsizetype start = program.indexOf("/Encoding");
    const qsizetype end = program.indexOf("eexec", start);
    if (start < 0) {
        return names;
    }
    const QByteArray part = program.mid(start, end < 0 ? -1 : end - start);
    if (part.contains("StandardEncoding def")) {
        for (int code = 0; code < 256; ++code) {
            if (Encodings::STANDARD[code]) {
                names[static_cast<size_t>(code)] = Encodings::STANDARD[code];
            }
        }
        return names;
    }
    qsizetype pos = 0;
    while ((pos = part.indexOf("dup ", pos)) >= 0) {
        pos += 4;
        const qsizetype slash = part.indexOf('/', pos);
        const qsizetype put = part.indexOf(" put", slash);
        if (slash < 0 || put < 0) {
            break;
        }
        bool ok = false;
        const int code = part.mid(pos, slash - pos).trimmed().toInt(&ok);
        if (ok && code >= 0 && code < 256) {
            names[static_cast<size_t>(code)] = part.mid(slash + 1, put - slash - 1).trimmed();
        }
        pos = put;
    }
    return names;
}

}  // namespace

namespace {

/// The character maps of a TrueType font ('cmap' table), by platform and encoding: which glyph a code has.
/// Formats 0, 4, 6 and 12 are read, which are those of the fonts in PDF files
class TrueTypeCmaps {
public:
    explicit TrueTypeCmaps(const QByteArray& table): t(table) {
        if (t.size() < 4) {
            return;
        }
        const int count = u16(2);
        for (int i = 0; i < count; ++i) {
            const qsizetype record = 4 + i * 8;
            if (record + 8 > t.size()) {
                break;
            }
            const int platform = u16(record);
            const int encoding = u16(record + 2);
            const quint32 offset = u32(record + 4);
            if (offset < static_cast<quint32>(t.size()) && !m_offsets.contains(key(platform, encoding))) {
                m_offsets.insert(key(platform, encoding), offset);
            }
        }
    }

    bool has(int platform, int encoding) const { return m_offsets.contains(key(platform, encoding)); }

    /// The glyph of a code in the map of a platform and encoding; 0 if there is none
    quint32 glyph(int platform, int encoding, quint32 code) const {
        const auto it = m_offsets.constFind(key(platform, encoding));
        if (it == m_offsets.constEnd()) {
            return 0;
        }
        const qsizetype o = *it;
        switch (u16(o)) {
            case 0:
                return code < 256 ? static_cast<uchar>(byte(o + 6 + code)) : 0;
            case 4: {
                const int segments = u16(o + 6) / 2;
                const qsizetype ends = o + 14;
                const qsizetype starts = ends + segments * 2 + 2;
                const qsizetype deltas = starts + segments * 2;
                const qsizetype ranges = deltas + segments * 2;
                for (int i = 0; i < segments; ++i) {
                    if (code > u16(ends + i * 2)) {
                        continue;
                    }
                    const quint32 start = u16(starts + i * 2);
                    if (code < start) {
                        return 0;
                    }
                    const int delta = static_cast<qint16>(u16(deltas + i * 2));
                    const int range = u16(ranges + i * 2);
                    if (range == 0) {
                        return (code + delta) & 0xffff;
                    }
                    const qsizetype at = ranges + i * 2 + range + (code - start) * 2;
                    const quint32 glyph = u16(at);
                    return glyph == 0 ? 0 : (glyph + delta) & 0xffff;
                }
                return 0;
            }
            case 6: {
                const quint32 first = u16(o + 6);
                const quint32 count = u16(o + 8);
                return code >= first && code < first + count ? u16(o + 10 + (code - first) * 2) : 0;
            }
            case 12: {
                const quint32 groups = u32(o + 12);
                for (quint32 i = 0; i < groups && i < 100000; ++i) {
                    const qsizetype g = o + 16 + static_cast<qsizetype>(i) * 12;
                    const quint32 start = u32(g);
                    const quint32 end = u32(g + 4);
                    if (code >= start && code <= end) {
                        return u32(g + 8) + (code - start);
                    }
                }
                return 0;
            }
            default:
                return 0;
        }
    }

private:
    static int key(int platform, int encoding) { return platform * 256 + encoding; }
    char byte(qsizetype at) const { return at >= 0 && at < t.size() ? t[at] : 0; }
    quint32 u16(qsizetype at) const { return (static_cast<uchar>(byte(at)) << 8) | static_cast<uchar>(byte(at + 1)); }
    quint32 u32(qsizetype at) const { return (u16(at) << 16) | u16(at + 2); }

    QByteArray t;
    QHash<int, quint32> m_offsets;
};

/// The glyph a name like "g0024" or "g5167" names by its index; 0 for other names. Programs write the index in
/// hexadecimal with four digits and leading zeros, or in decimal
quint32 glyphOfIndexName(const QByteArray& name) {
    if (name.size() < 2 || name[0] != 'g') {
        return 0;
    }
    const QByteArray digits = name.mid(1);
    if (!std::all_of(digits.begin(), digits.end(), [](char c) { return std::isxdigit(static_cast<uchar>(c)); })) {
        return 0;
    }
    const bool hex = std::any_of(digits.begin(), digits.end(), [](char c) { return std::isalpha(c); }) ||
                     (digits.size() == 4 && digits[0] == '0');
    bool ok = false;
    const quint32 index = digits.toUInt(&ok, hex ? 16 : 10);
    return ok ? index : 0;
}

/// The code of a character in the Mac Roman encoding, for the (1, 0) map of TrueType fonts; -1 if it has none
int macRomanCode(char16_t unicode) {
    for (int code = 0; code < 256; ++code) {
        const char* name = Encodings::MAC_ROMAN[code];
        if (name && unicodeOfName(name) == unicode) {
            return code;
        }
    }
    return unicode < 128 ? unicode : -1;
}

}  // namespace

struct PageDrawer::Font {
    QRawFont raw;
    bool composite = false;  ///< two bytes per character (Identity-H)
    /// For simple fonts: the glyph of each code; for composite ones the glyphs are the codes or cidToGid
    std::array<quint32, 256> glyphs{};
    std::array<bool, 256> known{};
    std::vector<quint16> cidToGid;
    /// Widths in thousandths of the size: by code (simple) or CID (composite)
    QHash<int, double> widths;
    double defaultWidth = 1000;
    bool widthsFromFont = false;
    /// A Type 3 font: its glyphs are content streams, in the space its matrix maps to that of the text
    bool type3 = false;
    QTransform fontMatrix;
    std::array<std::optional<Ref>, 256> procs;
    Dict resources;  ///< of the glyphs, empty if they use those of the page
};

namespace {

/// A small image that is shown large is made larger first, smoothly: Qt fades the border pixels of a small image
/// out when it scales it while drawing, which makes a blurred image smaller than viewers show it
QImage enlargedIfSmall(const QImage& image) {
    constexpr int SMALL = 64;
    const int side = std::min(image.width(), image.height());
    if (side <= 0 || side >= SMALL) {
        return image;
    }
    const int factor = std::min((SMALL + side - 1) / side, 4096 / std::max(image.width(), image.height()));
    if (factor <= 1) {
        return image;
    }
    return image.scaled(image.size() * factor, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

}  // namespace

struct PageDrawer::Image {
    QImage image;
    bool stencil = false;  ///< a mask that is painted with the fill colour
};

// The interpreter

class Interpreter {
public:
    Interpreter(PageDrawer& drawer, QPainter& painter): m_drawer(drawer), r(drawer.m_reader), m_painter(&painter) {}

    struct ColorSpace {
        /// Function: Separation and DeviceN, whose colours a function turns into colours of the base space
        enum class Kind { Gray, Rgb, Cmyk, Indexed, Function, Pattern };
        Kind kind = Kind::Gray;
        int components = 1;
        Kind base = Kind::Rgb;  ///< of an indexed colour space, or of a Separation or DeviceN
        int baseComponents = 3;
        QByteArray lookup;
        int hival = 0;
        std::shared_ptr<Function> tint;
        bool none = false;  ///< the Separation "None", which paints nothing
    };

    /// What a pattern paints: a gradient, or the cells of a tiling pattern
    struct Pattern {
        QBrush gradient;       ///< in the space of the pattern; NoBrush for tiling patterns
        QTransform toDefault;  ///< from the space of the pattern to the default space of the page or form
        Ref tiling;            ///< the stream of a tiling pattern
        QRectF cell;           ///< its box
        double xStep = 0;
        double yStep = 0;
    };

    /// A soft mask of the graphics state (section 11.6.5.2): what is painted while it is set shows where the
    /// group is bright (luminosity) or opaque (alpha)
    struct SoftMask {
        Ref group;
        bool luminosity = true;
        QColor backdrop = Qt::black;
        QTransform ctm;  ///< of the time the mask was set
        Dict resources;
    };

    struct State {
        QTransform ctm;
        std::shared_ptr<const SoftMask> softMask;
        /// The default space of the content that is run (the page, or a form): patterns are placed in it
        QTransform base;
        std::optional<Pattern> fillPattern;
        std::optional<Pattern> strokePattern;
        ColorSpace fillSpace;
        ColorSpace strokeSpace;
        QColor fill = Qt::black;
        QColor stroke = Qt::black;
        double fillAlpha = 1;
        double strokeAlpha = 1;
        double lineWidth = 1;
        Qt::PenCapStyle cap = Qt::FlatCap;
        Qt::PenJoinStyle join = Qt::MiterJoin;
        double miterLimit = 10;
        QList<qreal> dash;
        double dashPhase = 0;
        std::optional<QPainterPath> clip;  ///< in the coordinates of the page
        // Text
        double charSpacing = 0;
        double wordSpacing = 0;
        double horizontalScale = 1;
        double leading = 0;
        double rise = 0;
        int renderMode = 0;
        std::shared_ptr<PageDrawer::Font> font;
        double fontSize = 0;
    };

    void run(const QByteArray& content, const Dict& resources, int depth) {
        if (depth > MAX_FORM_DEPTH) {
            throw Unsupported{QStringLiteral("forms nested too deeply")};
        }
        // The depth of what is run, for the cells of patterns, which are run from painting
        struct Depth {
            int& depth;
            const int outer;
            ~Depth() { depth = outer; }
        } restore{m_depth, m_depth};
        m_depth = depth;
        // The resources of what is run, for the glyphs of Type 3 fonts without their own
        const Dict outerResources = m_resources;
        m_resources = resources;
        struct Resources {
            Dict& current;
            const Dict& outer;
            ~Resources() { current = outer; }
        } restoreResources{m_resources, outerResources};
        Lexer lexer(content);
        std::vector<Operand> operands;
        while (!lexer.atEnd()) {
            bool isOperator = false;
            Operand token = lexer.next(isOperator);
            if (!isOperator) {
                operands.push_back(std::move(token));
                continue;
            }
            if (++m_operations > MAX_OPERATIONS) {
                throw Unsupported{QStringLiteral("too much content")};
            }
            if (token.text == "BI") {
                inlineImage(lexer, resources);
            } else {
                execute(token.text, operands, resources, depth);
            }
            operands.clear();
        }
    }

    State s;

private:
    double num(const std::vector<Operand>& o, size_t i) const { return i < o.size() ? o[i].number : 0; }

    void need(const std::vector<Operand>& o, size_t count) const {
        if (o.size() < count) {
            throw Unsupported{QStringLiteral("operator with too few operands")};
        }
    }

    Dict resourceDict(const Dict& resources, const char* category) const {
        const Value value = get(r, resources, category);
        return value.kind() == Value::Kind::Dict ? value.toDict() : Dict();
    }

    // Colours

    ColorSpace colorSpace(const Value& value, const Dict& resources) const {
        ColorSpace space;
        Value v = r.resolve(value);
        if (v.kind() == Value::Kind::Name) {
            const QByteArray name = v.text();
            if (name == "DeviceGray" || name == "G" || name == "CalGray") {
                return space;
            }
            if (name == "DeviceRGB" || name == "RGB" || name == "CalRGB") {
                space.kind = ColorSpace::Kind::Rgb;
                space.components = 3;
                return space;
            }
            if (name == "DeviceCMYK" || name == "CMYK") {
                space.kind = ColorSpace::Kind::Cmyk;
                space.components = 4;
                return space;
            }
            if (name == "Pattern") {
                space.kind = ColorSpace::Kind::Pattern;
                space.components = 0;
                return space;
            }
            // A colour space of the resources
            const Dict spaces = resourceDict(resources, "ColorSpace");
            const Value* named = spaces.find(name);
            if (!named) {
                throw Unsupported{QStringLiteral("colour space ") + QString::fromLatin1(name)};
            }
            return colorSpace(*named, Dict());
        }
        if (v.kind() == Value::Kind::Array && !v.toArray().empty()) {
            const Array& a = v.toArray();
            const QByteArray family = r.resolve(a[0]).text();
            if (family == "ICCBased" && a.size() > 1) {
                // The colours as the alternative device space: as many components as the profile has
                const Value stream = r.object(a[1].toRef());
                const int n =
                        stream.kind() == Value::Kind::Dict ? static_cast<int>(number(r, stream.toDict(), "N", 3)) : 3;
                space.kind = n == 1 ? ColorSpace::Kind::Gray : n == 4 ? ColorSpace::Kind::Cmyk : ColorSpace::Kind::Rgb;
                space.components = n;
                return space;
            }
            if ((family == "CalRGB" || family == "CalGray") && a.size() > 1) {
                // Only where it is the device space: gamma 1, no matrix
                const Value parameters = r.resolve(a[1]);
                if (parameters.kind() == Value::Kind::Dict) {
                    const Value gamma = get(r, parameters.toDict(), "Gamma");
                    bool plain = parameters.toDict().find("Matrix") == nullptr;
                    if (gamma.kind() == Value::Kind::Number) {
                        plain = plain && std::abs(gamma.toNumber() - 1) < 1e-6;
                    }
                    for (const Value& value: gamma.toArray()) {
                        plain = plain && std::abs(r.resolve(value).toNumber() - 1) < 1e-6;
                    }
                    if (!plain) {
                        throw Unsupported{QStringLiteral("calibrated colour space")};
                    }
                }
                space.kind = family == "CalRGB" ? ColorSpace::Kind::Rgb : ColorSpace::Kind::Gray;
                space.components = family == "CalRGB" ? 3 : 1;
                return space;
            }
            if ((family == "Indexed" || family == "I") && a.size() == 4) {
                const ColorSpace base = colorSpace(a[1], resources);
                if (base.kind == ColorSpace::Kind::Indexed) {
                    throw Unsupported{QStringLiteral("nested indexed colours")};
                }
                space.kind = ColorSpace::Kind::Indexed;
                space.components = 1;
                space.base = base.kind;
                space.baseComponents = base.components;
                space.hival = r.resolve(a[2]).toInt();
                const Value lookup = r.resolve(a[3]);
                if (lookup.kind() == Value::Kind::String) {
                    space.lookup = stringBytes(lookup);
                } else if (a[3].kind() == Value::Kind::Ref) {
                    Dict dict;
                    if (!r.stream(a[3].toRef(), dict, space.lookup)) {
                        throw Unsupported{QStringLiteral("table of an indexed colour space")};
                    }
                }
                return space;
            }
            if (family == "DeviceGray" || family == "DeviceRGB" || family == "DeviceCMYK") {
                return colorSpace(a[0], resources);
            }
            if (family == "Pattern") {
                // With a base space for patterns without colours of their own, which are not understood
                space.kind = ColorSpace::Kind::Pattern;
                space.components = 0;
                return space;
            }
            if ((family == "Separation" || family == "DeviceN") && a.size() >= 4) {
                // Colourants, the space they are shown in where there are none, and the function that converts
                const Value names = r.resolve(a[1]);
                space.components = family == "Separation" ? 1 : static_cast<int>(names.toArray().size());
                space.none = names.kind() == Value::Kind::Name && names.text() == "None";
                const ColorSpace alternate = colorSpace(a[2], resources);
                if (alternate.kind != ColorSpace::Kind::Gray && alternate.kind != ColorSpace::Kind::Rgb &&
                    alternate.kind != ColorSpace::Kind::Cmyk) {
                    throw Unsupported{QStringLiteral("alternate colour space of ") + QString::fromLatin1(family)};
                }
                space.kind = ColorSpace::Kind::Function;
                space.base = alternate.kind;
                space.baseComponents = alternate.components;
                space.tint = Function::parse(r, a[3]);
                if (!space.tint || space.components < 1 || space.tint->inputs() != space.components) {
                    throw Unsupported{QStringLiteral("function of ") + QString::fromLatin1(family)};
                }
                return space;
            }
            throw Unsupported{QStringLiteral("colour space ") + QString::fromLatin1(family)};
        }
        throw Unsupported{QStringLiteral("colour space")};
    }

    static QColor deviceColor(ColorSpace::Kind kind, const double* c) {
        auto unit = [](double value) { return std::clamp(value, 0.0, 1.0); };
        switch (kind) {
            case ColorSpace::Kind::Gray:
                return QColor::fromRgbF(unit(c[0]), unit(c[0]), unit(c[0]));
            case ColorSpace::Kind::Rgb:
                return QColor::fromRgbF(unit(c[0]), unit(c[1]), unit(c[2]));
            case ColorSpace::Kind::Cmyk: {
                // As most viewers do without a colour profile
                const double k = unit(c[3]);
                return QColor::fromRgbF((1 - unit(c[0])) * (1 - k), (1 - unit(c[1])) * (1 - k),
                                        (1 - unit(c[2])) * (1 - k));
            }
            default:
                return Qt::black;
        }
    }

    QColor colorOf(const ColorSpace& space, const std::vector<Operand>& o) const {
        if (space.kind == ColorSpace::Kind::Function) {
            if (space.none) {
                return Qt::transparent;
            }
            std::vector<double> in;
            for (const Operand& operand: o) {
                if (operand.kind == Operand::Kind::Number) {
                    in.push_back(operand.number);
                }
            }
            std::vector<double> out = (*space.tint)(in);
            out.resize(4, 0.0);
            return deviceColor(space.base, out.data());
        }
        double c[4] = {0, 0, 0, 0};
        const size_t count = std::min<size_t>(o.size(), 4);
        for (size_t i = 0; i < count; ++i) {
            if (o[i].kind == Operand::Kind::Name) {
                throw Unsupported{QStringLiteral("patterns")};
            }
            c[i] = o[i].number;
        }
        if (space.kind == ColorSpace::Kind::Indexed) {
            const int index = std::clamp(static_cast<int>(c[0]), 0, space.hival);
            double base[4] = {0, 0, 0, 0};
            for (int i = 0; i < space.baseComponents; ++i) {
                const qsizetype at = static_cast<qsizetype>(index) * space.baseComponents + i;
                base[i] = at < space.lookup.size() ? static_cast<uchar>(space.lookup[at]) / 255.0 : 0;
            }
            return deviceColor(space.base, base);
        }
        return deviceColor(space.kind, c);
    }

    // Painting

    /// Sets the transformation of the painter, from user space to the space of the page
    void setTransform(const QTransform& transform) { m_painter->setTransform(transform * m_deviceBase); }

    void applyClip() {
        m_painter->setTransform(m_deviceBase);
        if (s.clip) {
            m_painter->setClipPath(*s.clip, Qt::ReplaceClip);
        } else {
            m_painter->setClipping(false);
        }
    }

    /// The pen for the painter with this transformation
    QPen pen(const QTransform& painterTransform) const {
        QColor color = s.stroke;
        color.setAlphaF(color.alphaF() * s.strokeAlpha);
        QPen result(color, s.lineWidth, Qt::SolidLine, s.cap, s.join);
        if (s.strokePattern) {
            result.setBrush(patternBrush(*s.strokePattern, s.strokeAlpha, painterTransform));
        }
        result.setMiterLimit(s.miterLimit);
        if (!s.dash.isEmpty() && s.lineWidth > 0) {
            // Qt counts dashes in line widths, PDF in units of the user space
            QList<qreal> pattern;
            bool any = false;
            for (qreal length: s.dash) {
                pattern.append(length / s.lineWidth);
                any = any || length > 0;
            }
            if (pattern.size() % 2) {
                pattern.append(pattern);
            }
            if (any) {
                result.setDashPattern(pattern);
                result.setDashOffset(s.dashPhase / s.lineWidth);
            }
        }
        return result;
    }

    /// The brush for the painter with this transformation
    QBrush brush(const QTransform& painterTransform) const {
        if (s.fillPattern) {
            return patternBrush(*s.fillPattern, s.fillAlpha, painterTransform);
        }
        QColor color = s.fill;
        color.setAlphaF(color.alphaF() * s.fillAlpha);
        return color;
    }

    /// The gradient of a pattern, placed for the painter with this transformation
    QBrush patternBrush(const Pattern& pattern, double alpha, const QTransform& painterTransform) const {
        QBrush result = pattern.gradient;
        if (result.style() == Qt::NoBrush || !painterTransform.isInvertible()) {
            return Qt::NoBrush;
        }
        if (alpha < 1 && result.gradient()) {
            QGradient gradient = *result.gradient();
            QGradientStops stops = gradient.stops();
            for (auto& stop: stops) {
                stop.second.setAlphaF(stop.second.alphaF() * alpha);
            }
            gradient.setStops(stops);
            result = QBrush(gradient);
        }
        result.setTransform(pattern.toDefault * painterTransform.inverted());
        return result;
    }

    // Soft masks

    std::shared_ptr<const SoftMask> softMaskOf(const Value& value, const Dict& resources) {
        if (value.kind() == Value::Kind::Name && value.text() == "None") {
            return nullptr;
        }
        if (value.kind() != Value::Kind::Dict) {
            throw Unsupported{QStringLiteral("soft mask")};
        }
        const Dict& dict = value.toDict();
        const Value* group = dict.find("G");
        if (!group || group->kind() != Value::Kind::Ref) {
            throw Unsupported{QStringLiteral("soft mask without a group")};
        }
        const Value transfer = get(r, dict, "TR");
        if (!transfer.isNull() && !(transfer.kind() == Value::Kind::Name && transfer.text() == "Identity")) {
            throw Unsupported{QStringLiteral("transfer function of a soft mask")};
        }
        auto mask = std::make_shared<SoftMask>();
        mask->group = group->toRef();
        mask->luminosity = get(r, dict, "S").text() != "Alpha";
        mask->ctm = s.ctm;
        mask->resources = resources;
        // The backdrop, in the colour space of the group
        const Value backdrop = get(r, dict, "BC");
        if (mask->luminosity && !backdrop.toArray().empty()) {
            const Value groupObject = r.object(mask->group);
            const Value groupDict = get(r, groupObject.toDict(), "Group");
            const ColorSpace space = colorSpace(get(r, groupDict.toDict(), "CS"), resources);
            std::vector<Operand> components;
            for (const Value& c: backdrop.toArray()) {
                components.push_back(Operand{Operand::Kind::Number, r.resolve(c).toNumber()});
            }
            mask->backdrop = colorOf(space, components);
        }
        return mask;
    }

    /// Draws what op paints into an image, for the area of the page it covers, and lets the soft mask of the state
    /// decide how much of it shows
    void masked(const QRectF& bounds, const std::function<void()>& op) {
        const std::shared_ptr<const SoftMask> mask = s.softMask;
        QRectF area = bounds.adjusted(-1, -1, 1, 1).intersected(m_pageArea);
        if (s.clip) {
            area = area.intersected(s.clip->boundingRect());
        }
        if (area.isEmpty()) {
            return;
        }
        const double k = m_maskScale;
        const QRect pixels(
                QPoint(static_cast<int>(std::floor(area.left() * k)), static_cast<int>(std::floor(area.top() * k))),
                QPoint(static_cast<int>(std::ceil(area.right() * k)), static_cast<int>(std::ceil(area.bottom() * k))));
        if (static_cast<qint64>(pixels.width()) * pixels.height() > SOFT_MASK_MAX_PIXELS) {
            throw Unsupported{QStringLiteral("soft mask over a large area")};
        }
        const QTransform base = QTransform::fromScale(k, k) * QTransform::fromTranslate(-pixels.left(), -pixels.top());
        // Draws into an image instead of the page
        const auto into = [&](QImage& image, const std::function<void()>& draw) {
            QPainter painter(&image);
            painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
            QPainter* const outerPainter = m_painter;
            const QTransform outerBase = m_deviceBase;
            m_painter = &painter;
            m_deviceBase = base;
            draw();
            m_painter = outerPainter;
            m_deviceBase = outerBase;
        };

        QImage content(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        content.fill(Qt::transparent);
        s.softMask.reset();
        into(content, op);
        s.softMask = mask;

        // The group of the mask, on its backdrop
        QImage maskImage(pixels.size(), QImage::Format_ARGB32_Premultiplied);
        maskImage.fill(mask->luminosity ? mask->backdrop : QColor(Qt::transparent));
        Dict groupDict;
        QByteArray groupContent;
        if (!r.stream(mask->group, groupDict, groupContent)) {
            throw Unsupported{QStringLiteral("group of a soft mask")};
        }
        into(maskImage, [&] {
            const State saved = s;
            const QPainterPath savedPath = m_path;
            const int hidden = m_hidden;
            s = State();
            s.ctm = mask->ctm;
            s.base = mask->ctm;
            QPainterPath page;
            page.addRect(m_pageArea);
            s.clip = page;
            m_hidden = 0;
            form(groupContent, groupDict, mask->resources, m_depth);
            s = saved;
            m_path = savedPath;
            m_hidden = hidden;
        });

        for (int y = 0; y < content.height(); ++y) {
            auto* c = reinterpret_cast<QRgb*>(content.scanLine(y));
            const auto* m = reinterpret_cast<const QRgb*>(maskImage.constScanLine(y));
            for (int x = 0; x < content.width(); ++x) {
                const int factor = mask->luminosity ? (qRed(m[x]) * 77 + qGreen(m[x]) * 150 + qBlue(m[x]) * 29) >> 8 :
                                                      qAlpha(m[x]);
                c[x] = qRgba(qRed(c[x]) * factor / 255, qGreen(c[x]) * factor / 255, qBlue(c[x]) * factor / 255,
                             qAlpha(c[x]) * factor / 255);
            }
        }
        m_painter->setTransform(m_deviceBase);
        m_painter->setClipping(false);
        m_painter->drawImage(QRectF(pixels.left() / k, pixels.top() / k, pixels.width() / k, pixels.height() / k),
                             content);
    }

    // Patterns and shadings

    QColor initialColor(const ColorSpace& space) const {
        if (space.kind == ColorSpace::Kind::Function) {
            // All colourants at full strength
            return colorOf(space, std::vector<Operand>(static_cast<size_t>(space.components),
                                                       Operand{Operand::Kind::Number, 1.0}));
        }
        if (space.kind == ColorSpace::Kind::Indexed) {
            return colorOf(space, {Operand{Operand::Kind::Number, 0.0}});
        }
        return Qt::black;
    }

    /// The pattern that the last operand names
    Pattern pattern(const std::vector<Operand>& o, const Dict& resources) {
        if (o.empty() || o.back().kind != Operand::Kind::Name) {
            throw Unsupported{QStringLiteral("pattern without a name")};
        }
        const Dict patterns = resourceDict(resources, "Pattern");
        const Value* ref = patterns.find(o.back().text);
        if (!ref) {
            throw Unsupported{QStringLiteral("pattern that is missing")};
        }
        Dict dict;
        QByteArray content;
        const bool isStream = ref->kind() == Value::Kind::Ref && r.stream(ref->toRef(), dict, content);
        if (!isStream) {
            const Value object = r.resolve(*ref);
            if (object.kind() != Value::Kind::Dict) {
                throw Unsupported{QStringLiteral("pattern")};
            }
            dict = object.toDict();
        }
        Pattern result;
        result.toDefault = matrixOf(r, get(r, dict, "Matrix")) * s.base;
        const int type = static_cast<int>(number(r, dict, "PatternType", 0));
        if (type == 2) {
            const Value* shading = dict.find("Shading");
            if (!shading) {
                throw Unsupported{QStringLiteral("pattern without shading")};
            }
            result.gradient = gradientBrush(*shading, resources);
            return result;
        }
        if (type == 1 && isStream) {
            // Cells with colours of their own (1); those that take the colour of the operands (2) are not done
            if (static_cast<int>(number(r, dict, "PaintType", 1)) != 1) {
                throw Unsupported{QStringLiteral("uncoloured tiling pattern")};
            }
            result.gradient = Qt::NoBrush;
            result.tiling = ref->toRef();
            result.cell = rectOf(r, get(r, dict, "BBox"));
            result.xStep = std::abs(number(r, dict, "XStep", 0));
            result.yStep = std::abs(number(r, dict, "YStep", 0));
            if (!result.cell.isValid() || result.xStep < 1e-6 || result.yStep < 1e-6) {
                throw Unsupported{QStringLiteral("tiling pattern")};
            }
            return result;
        }
        throw Unsupported{QStringLiteral("pattern of type %1").arg(type)};
    }

    /// An axial or radial shading as a gradient, in the space of the shading
    QBrush gradientBrush(const Value& value, const Dict& resources) {
        Dict dict;
        QByteArray unused;
        if (value.kind() == Value::Kind::Ref && r.stream(value.toRef(), dict, unused)) {
            // Shadings of types 4 to 7 are streams
        } else {
            const Value object = r.resolve(value);
            if (object.kind() != Value::Kind::Dict) {
                throw Unsupported{QStringLiteral("shading")};
            }
            dict = object.toDict();
        }
        const int type = static_cast<int>(number(r, dict, "ShadingType", 0));
        if (type != 2 && type != 3) {
            throw Unsupported{QStringLiteral("shading of type %1").arg(type)};
        }
        const ColorSpace space = colorSpace(get(r, dict, "ColorSpace"), resources);
        if (space.kind == ColorSpace::Kind::Pattern || space.kind == ColorSpace::Kind::Indexed) {
            throw Unsupported{QStringLiteral("colour space of a shading")};
        }
        // One function with all components, or one function per component
        std::vector<std::shared_ptr<Function>> functions;
        const Value* functionValue = dict.find("Function");
        const Value resolvedFunction = functionValue ? r.resolve(*functionValue) : Value();
        if (resolvedFunction.kind() == Value::Kind::Array) {
            for (const Value& item: resolvedFunction.toArray()) {
                functions.push_back(Function::parse(r, item));
            }
        } else if (functionValue) {
            functions.push_back(Function::parse(r, *functionValue));
        }
        if (functions.empty() || std::any_of(functions.begin(), functions.end(), [](const auto& f) { return !f; })) {
            throw Unsupported{QStringLiteral("function of a shading")};
        }
        std::vector<double> coords;
        const Value coordsValue = get(r, dict, "Coords");
        for (const Value& item: coordsValue.toArray()) {
            coords.push_back(r.resolve(item).toNumber());
        }
        if (coords.size() != (type == 2 ? 4u : 6u)) {
            throw Unsupported{QStringLiteral("shading without coordinates")};
        }
        double t0 = 0;
        double t1 = 1;
        if (const Value domain = get(r, dict, "Domain"); domain.toArray().size() == 2) {
            t0 = r.resolve(domain.toArray()[0]).toNumber();
            t1 = r.resolve(domain.toArray()[1]).toNumber();
        }
        bool extend[2] = {false, false};
        if (const Value e = get(r, dict, "Extend"); e.toArray().size() == 2) {
            for (size_t i = 0; i < 2; ++i) {
                const Value v = r.resolve(e.toArray()[i]);
                extend[i] = v.kind() == Value::Kind::Bool && v.toNumber() != 0;
            }
        }

        // The colours along the gradient: at even steps, and on both sides of the places where the function jumps.
        // Where it does not extend, Qt's padding gets a transparent colour
        constexpr double EDGE = 1e-4;
        std::vector<double> fractions;
        for (int i = 0; i <= GRADIENT_STOPS; ++i) {
            fractions.push_back(static_cast<double>(i) / GRADIENT_STOPS);
        }
        if (t1 != t0) {
            std::vector<double> jumps;
            for (const auto& function: functions) {
                function->jumps(jumps);
            }
            if (jumps.size() > 4096) {
                throw Unsupported{QStringLiteral("function of a shading")};
            }
            for (double jump: jumps) {
                const double fraction = (jump - t0) / (t1 - t0);
                if (fraction > EDGE && fraction < 1 - EDGE) {
                    fractions.push_back(fraction - EDGE / 10);
                    fractions.push_back(fraction + EDGE / 10);
                }
            }
            std::sort(fractions.begin(), fractions.end());
        }
        QGradientStops stops;
        const auto colorAt = [&](double fraction) {
            const double t = t0 + (t1 - t0) * fraction;
            std::vector<Operand> components;
            for (const auto& function: functions) {
                for (double c: (*function)({t})) {
                    components.push_back(Operand{Operand::Kind::Number, c});
                }
            }
            return colorOf(space, components);
        };
        for (size_t i = 0; i < fractions.size(); ++i) {
            double position = fractions[i];
            if (i == 0 && !extend[0]) {
                stops.append({0.0, Qt::transparent});
                position = EDGE;
            } else if (i + 1 == fractions.size() && !extend[1]) {
                position = 1 - EDGE;
            }
            stops.append({position, colorAt(fractions[i])});
        }
        if (!extend[1]) {
            stops.append({1.0, Qt::transparent});
        }
        if (type == 2) {
            QLinearGradient gradient(coords[0], coords[1], coords[2], coords[3]);
            gradient.setStops(stops);
            return gradient;
        }
        // Qt's radial gradient goes from the focal circle to the outer one, as PDF from the first to the second
        QRadialGradient gradient(QPointF(coords[3], coords[4]), coords[5], QPointF(coords[0], coords[1]), coords[2]);
        gradient.setStops(stops);
        return gradient;
    }

    /// The operator sh: a shading fills what is not clipped away
    void paintShading(const QByteArray& name, const Dict& resources) {
        const Dict shadings = resourceDict(resources, "Shading");
        const Value* value = shadings.find(name);
        if (!value) {
            throw Unsupported{QStringLiteral("shading that is missing")};
        }
        const QBrush gradient = gradientBrush(*value, resources);
        if (m_hidden > 0 || !s.ctm.isInvertible()) {
            return;
        }
        if (s.softMask) {
            masked(s.clip ? s.clip->boundingRect() : m_pageArea, [&] { paintShading(name, resources); });
            return;
        }
        applyClip();
        setTransform(s.ctm);
        // Its own box limits it too
        QPainterPath area;
        area.addRect(s.ctm.inverted().mapRect(s.clip ? s.clip->boundingRect() : QRectF(-1e5, -1e5, 2e5, 2e5)));
        Dict dict;
        QByteArray unused;
        const Value object = value->kind() == Value::Kind::Ref && r.stream(value->toRef(), dict, unused) ?
                                     Value::dict(dict) :
                                     r.resolve(*value);
        if (const QRectF box = rectOf(r, get(r, object.toDict(), "BBox")); box.isValid()) {
            QPainterPath boxPath;
            boxPath.addRect(box);
            area = area.intersected(boxPath);
        }
        m_painter->setOpacity(s.fillAlpha);
        m_painter->fillPath(area, gradient);
        m_painter->setOpacity(1);
    }

    /// Fills an area of the page with the cells of a tiling pattern, as vectors: its content is drawn once per cell
    void fillTiling(const Pattern& pattern, const QPainterPath& pagePath) {
        if (m_hidden > 0) {
            return;
        }
        const QPainterPath area = s.clip ? s.clip->intersected(pagePath) : pagePath;
        if (area.isEmpty() || !pattern.toDefault.isInvertible()) {
            return;
        }
        Dict dict;
        QByteArray content;
        if (!r.stream(pattern.tiling, dict, content)) {
            throw Unsupported{QStringLiteral("tiling pattern")};
        }
        // The cells that reach into the area
        const QRectF bounds = pattern.toDefault.inverted().mapRect(area.boundingRect());
        const int i0 = static_cast<int>(std::floor((bounds.left() - pattern.cell.right()) / pattern.xStep));
        const int i1 = static_cast<int>(std::ceil((bounds.right() - pattern.cell.left()) / pattern.xStep));
        const int j0 = static_cast<int>(std::floor((bounds.top() - pattern.cell.bottom()) / pattern.yStep));
        const int j1 = static_cast<int>(std::ceil((bounds.bottom() - pattern.cell.top()) / pattern.yStep));
        if (static_cast<qint64>(i1 - i0 + 1) * (j1 - j0 + 1) > MAX_TILES) {
            throw Unsupported{QStringLiteral("tiling pattern with too many cells")};
        }
        const Value own = get(r, dict, "Resources");
        const Dict resources = own.kind() == Value::Kind::Dict ? own.toDict() : Dict();
        const State saved = s;
        const QPainterPath savedPath = m_path;
        for (int j = j0; j <= j1; ++j) {
            for (int i = i0; i <= i1; ++i) {
                // A cell starts with the state of graphics a pattern starts with
                s = State();
                s.ctm = QTransform::fromTranslate(i * pattern.xStep, j * pattern.yStep) * pattern.toDefault;
                s.base = s.ctm;
                QPainterPath cell;
                cell.addRect(pattern.cell);
                s.clip = area.intersected(s.ctm.map(cell));
                m_path = QPainterPath();
                run(content, resources, m_depth + 1);
            }
        }
        s = saved;
        m_path = savedPath;
    }

    void paintPath(bool fill, bool stroke, Qt::FillRule rule) {
        if (m_path.isEmpty() && !m_pendingClip) {
            return;
        }
        if (!m_path.isEmpty() && (fill || stroke) && m_hidden == 0 && s.softMask) {
            const double width = stroke ? s.lineWidth * std::sqrt(std::abs(s.ctm.determinant())) + 2 : 0;
            masked(s.ctm.map(m_path).boundingRect().adjusted(-width, -width, width, width),
                   [&] { paintPath(fill, stroke, rule); });
            // What the path leaves behind (clip, its end) is done by the call above
            return;
        }
        if (!m_path.isEmpty() && (fill || stroke) && m_hidden == 0) {
            applyClip();
            setTransform(s.ctm);
            QPainterPath path = m_path;
            path.setFillRule(rule);
            if (fill && s.fillPattern && s.fillPattern->gradient.style() == Qt::NoBrush) {
                fillTiling(*s.fillPattern, s.ctm.map(path));
            } else if (fill) {
                m_painter->fillPath(path, brush(s.ctm));
            }
            if (stroke && s.strokePattern && s.strokePattern->gradient.style() == Qt::NoBrush) {
                QPainterPathStroker stroker(pen(s.ctm));
                fillTiling(*s.strokePattern, s.ctm.map(stroker.createStroke(path)));
            } else if (stroke) {
                applyClip();
                setTransform(s.ctm);
                m_painter->strokePath(path, pen(s.ctm));
            }
        }
        if (m_pendingClip) {
            QPainterPath clip = m_path;
            clip.setFillRule(*m_pendingClip);
            intersectClip(s.ctm.map(clip));
            m_pendingClip.reset();
        }
        m_path = QPainterPath();
    }

    void intersectClip(const QPainterPath& pagePath) { s.clip = s.clip ? s.clip->intersected(pagePath) : pagePath; }

    // Images

    std::shared_ptr<PageDrawer::Image> imageFrom(const QByteArray& rawData, const QByteArray& filter,
                                                 const std::function<Value(const char*, const char*)>& entry,
                                                 const Dict& resources, std::optional<Ref> softMask) {
        auto result = std::make_shared<PageDrawer::Image>();
        const int width = entry("Width", "W").toInt();
        const int height = entry("Height", "H").toInt();
        if (width <= 0 || height <= 0 || static_cast<qint64>(width) * height > 64'000'000) {
            throw Unsupported{QStringLiteral("image size")};
        }
        const Value maskValue = entry("ImageMask", "IM");
        result->stencil = maskValue.kind() == Value::Kind::Bool && maskValue.toNumber() != 0;
        const Value decode = entry("Decode", "D");
        const Value maskEntry = entry("Mask", nullptr);
        if (!maskEntry.isNull()) {
            throw Unsupported{QStringLiteral("image with a mask")};
        }

        if (result->stencil) {
            // One bit per pixel; 0 is painted, unless the decode array turns it round
            bool paintOne = false;
            if (decode.kind() == Value::Kind::Array && !decode.toArray().empty()) {
                paintOne = r.resolve(decode.toArray()[0]).toNumber() > 0.5;
            }
            const qsizetype row = (width + 7) / 8;
            if (!filter.isEmpty() || rawData.size() < row * height) {
                throw Unsupported{filter.isEmpty() ? QStringLiteral("image mask data too short") :
                                                     QStringLiteral("image mask in ") + QString::fromLatin1(filter)};
            }
            QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
            for (int y = 0; y < height; ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    const bool bit = (static_cast<uchar>(rawData[y * row + x / 8]) >> (7 - x % 8)) & 1;
                    line[x] = bit == paintOne ? 0xff000000 : 0;
                }
            }
            result->image = enlargedIfSmall(image);
            return result;
        }

        if (decode.kind() == Value::Kind::Array) {
            throw Unsupported{QStringLiteral("decode array of an image")};
        }
        const ColorSpace space = colorSpace(entry("ColorSpace", "CS"), resources);
        QImage image;
        if (filter == "DCTDecode" || filter == "DCT") {
            if (space.kind != ColorSpace::Kind::Gray && space.kind != ColorSpace::Kind::Rgb) {
                throw Unsupported{QStringLiteral("JPEG image that is not RGB or gray")};
            }
            image = QImage::fromData(rawData, "JPEG");
            if (image.isNull()) {
                throw Unsupported{QStringLiteral("JPEG image")};
            }
        } else if (!filter.isEmpty()) {
            throw Unsupported{QStringLiteral("image format ") + QString::fromLatin1(filter)};
        } else {
            const int bits = std::max(1, entry("BitsPerComponent", "BPC").toInt());
            if (space.kind == ColorSpace::Kind::Pattern || space.components < 1 || space.components > 4) {
                throw Unsupported{QStringLiteral("colour space of an image")};
            }
            // Colours made by a function, by the values of the pixel (there are few different ones, usually)
            QHash<quint32, QRgb> converted;
            if (bits != 8 && !(space.components == 1 && (bits == 1 || bits == 2 || bits == 4))) {
                throw Unsupported{QStringLiteral("image with %1 bits per component").arg(bits)};
            }
            const qsizetype row = (static_cast<qsizetype>(width) * space.components * bits + 7) / 8;
            if (rawData.size() < row * height) {
                throw Unsupported{QStringLiteral("image data too short")};
            }
            // The Separation "None" paints nothing: its pixels are transparent
            image = QImage(width, height, space.none ? QImage::Format_ARGB32 : QImage::Format_RGB32);
            const int maxValue = (1 << bits) - 1;
            for (int y = 0; y < height; ++y) {
                const auto* in = reinterpret_cast<const uchar*>(rawData.constData() + y * row);
                auto* out = reinterpret_cast<QRgb*>(image.scanLine(y));
                for (int x = 0; x < width; ++x) {
                    double c[4] = {0, 0, 0, 0};
                    for (int k = 0; k < space.components; ++k) {
                        if (bits == 8) {
                            c[k] = in[x * space.components + k];
                        } else {
                            const int bit = x * bits;
                            c[k] = (in[bit / 8] >> (8 - bits - bit % 8)) & maxValue;
                        }
                    }
                    if (space.kind == ColorSpace::Kind::Indexed) {
                        const std::vector<Operand> index{Operand{Operand::Kind::Number, c[0]}};
                        out[x] = colorOf(space, index).rgb();
                    } else if (space.kind == ColorSpace::Kind::Function) {
                        quint32 key = 0;
                        std::vector<Operand> components;
                        for (int k = 0; k < space.components; ++k) {
                            key = (key << 8) | static_cast<quint32>(c[k]);
                            components.push_back(Operand{Operand::Kind::Number, c[k] / maxValue});
                        }
                        auto it = converted.constFind(key);
                        if (it == converted.constEnd()) {
                            it = converted.insert(key, colorOf(space, components).rgba());
                        }
                        out[x] = *it;
                    } else {
                        for (double& component: c) {
                            component /= maxValue;
                        }
                        out[x] = deviceColor(space.kind, c).rgb();
                    }
                }
            }
        }
        if (softMask) {
            Dict maskDict;
            QByteArray maskData;
            QByteArray maskFilter;
            if (!r.stream(*softMask, maskDict, maskData, &maskFilter)) {
                throw Unsupported{QStringLiteral("soft mask of an image")};
            }
            const int mw = static_cast<int>(number(r, maskDict, "Width", 0));
            const int mh = static_cast<int>(number(r, maskDict, "Height", 0));
            const int bits = static_cast<int>(number(r, maskDict, "BitsPerComponent", 8));
            if (mw <= 0 || mh <= 0 || static_cast<qint64>(mw) * mh > 64'000'000) {
                throw Unsupported{QStringLiteral("soft mask of an image")};
            }
            // The mask as gray values: from JPEG, or from samples of 1 to 16 bits
            QImage alpha;
            if (maskFilter == "DCTDecode" || maskFilter == "DCT") {
                alpha = QImage::fromData(maskData, "JPEG").convertToFormat(QImage::Format_Grayscale8);
            } else if (maskFilter.isEmpty() && (bits == 1 || bits == 2 || bits == 4 || bits == 8 || bits == 16)) {
                const qsizetype row = (static_cast<qsizetype>(mw) * bits + 7) / 8;
                if (maskData.size() >= row * mh) {
                    alpha = QImage(mw, mh, QImage::Format_Grayscale8);
                    const int maxValue = (1 << std::min(bits, 8)) - 1;
                    for (int y = 0; y < mh; ++y) {
                        const auto* in = reinterpret_cast<const uchar*>(maskData.constData() + y * row);
                        uchar* out = alpha.scanLine(y);
                        for (int x = 0; x < mw; ++x) {
                            int value = 0;
                            if (bits == 16) {
                                value = in[2 * x];  // the high byte
                            } else if (bits == 8) {
                                value = in[x];
                            } else {
                                const int bit = x * bits;
                                value = ((in[bit / 8] >> (8 - bits - bit % 8)) & maxValue) * 255 / maxValue;
                            }
                            out[x] = static_cast<uchar>(value);
                        }
                    }
                }
            }
            if (alpha.isNull()) {
                throw Unsupported{QStringLiteral("soft mask of an image")};
            }
            // Image and mask each fill the unit square: the smaller one is brought to the size of the larger one. An
            // image is enlarged without smoothing, as its pixels are areas of one colour (text in the shape of its
            // mask has an image of a few pixels)
            if (static_cast<qint64>(alpha.width()) * alpha.height() >
                static_cast<qint64>(image.width()) * image.height()) {
                image = image.scaled(alpha.size(), Qt::IgnoreAspectRatio, Qt::FastTransformation);
            } else if (alpha.size() != image.size()) {
                alpha = alpha.scaled(image.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
            }
            image = image.convertToFormat(QImage::Format_ARGB32);
            for (int y = 0; y < image.height(); ++y) {
                auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
                const uchar* a = alpha.constScanLine(y);
                for (int x = 0; x < image.width(); ++x) {
                    line[x] = qRgba(qRed(line[x]), qGreen(line[x]), qBlue(line[x]), a[x]);
                }
            }
        }
        result->image = enlargedIfSmall(image);
        return result;
    }

    void drawImage(const PageDrawer::Image& image) {
        if (m_hidden > 0) {
            return;
        }
        if (s.softMask) {
            masked((QTransform(1, 0, 0, -1, 0, 1) * s.ctm).mapRect(QRectF(0, 0, 1, 1)), [&] { drawImage(image); });
            return;
        }
        applyClip();
        // The image fills the unit square of the user space, its first row at the top
        setTransform(QTransform(1, 0, 0, -1, 0, 1) * s.ctm);
        m_painter->setRenderHint(QPainter::SmoothPixmapTransform);
        if (image.stencil) {
            QImage colored(image.image.size(), QImage::Format_ARGB32_Premultiplied);
            if (s.fillPattern) {
                throw Unsupported{QStringLiteral("image mask painted with a pattern")};
            }
            colored.fill(brush(QTransform()).color());
            QPainter mask(&colored);
            mask.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            mask.drawImage(0, 0, image.image);
            mask.end();
            m_painter->drawImage(QRectF(0, 0, 1, 1), colored);
        } else {
            m_painter->setOpacity(s.fillAlpha);
            m_painter->drawImage(QRectF(0, 0, 1, 1), image.image);
            m_painter->setOpacity(1);
        }
    }

    void inlineImage(Lexer& lexer, const Dict& resources) {
        OperandDict entries;
        while (!lexer.atEnd()) {
            bool op = false;
            const qsizetype before = lexer.pos();
            Operand key = lexer.next(op);
            if (op && key.text == "ID") {
                break;
            }
            if (key.kind != Operand::Kind::Name) {
                lexer.setPos(before + 1);
                continue;
            }
            entries.emplace_back(key.text, lexer.next(op));
        }
        // One white space, then the data up to "EI" between white space
        qsizetype start = lexer.pos() + 1;
        const QByteArray& d = lexer.data();
        qsizetype end = start;
        while (true) {
            end = d.indexOf("EI", end);
            if (end < 0) {
                throw Unsupported{QStringLiteral("inline image without end")};
            }
            if (end > start && isSpace(d[end - 1]) && (end + 2 >= d.size() || isSpace(d[end + 2]))) {
                break;
            }
            end += 2;
        }
        QByteArray data = d.mid(start, end - 1 - start);
        lexer.setPos(end + 2);

        // The inline dictionary as a dictionary of the file, so that the image is read as the others
        std::function<Value(const Operand&)> toValue = [&toValue](const Operand& o) -> Value {
            switch (o.kind) {
                case Operand::Kind::Dict: {
                    Dict dict;
                    for (const auto& [key, value]: *o.dict) {
                        dict.set(key, toValue(value));
                    }
                    return Value::dict(std::move(dict));
                }
                case Operand::Kind::Number:
                    return Value::number(o.number);
                case Operand::Kind::Name:
                    return Value::name(o.text);
                case Operand::Kind::Bool:
                    return Value::boolean(o.number != 0);
                case Operand::Kind::Array: {
                    Array array;
                    for (const Operand& item: *o.array) {
                        array.push_back(toValue(item));
                    }
                    return Value::array(std::move(array));
                }
                default:
                    return Value();
            }
        };
        Dict dict;
        for (const auto& [key, value]: entries) {
            dict.set(key, toValue(value));
        }
        // The filters, with their short names
        QByteArray filter;
        const Value filters = get(r, dict, "F").isNull() ? get(r, dict, "Filter") : get(r, dict, "F");
        Array filterList;
        if (filters.kind() == Value::Kind::Array) {
            filterList = filters.toArray();
        } else if (!filters.isNull()) {
            filterList.push_back(filters);
        }
        for (size_t i = 0; i < filterList.size(); ++i) {
            const QByteArray name = filterList[i].text();
            if (name == "Fl" || name == "FlateDecode" || name == "AHx" || name == "ASCIIHexDecode" || name == "A85" ||
                name == "ASCII85Decode") {
                Dict single;
                single.set("Filter", Value::name(name));
                if (!get(r, dict, "DP").isNull()) {
                    single.set("DecodeParms", get(r, dict, "DP"));
                }
                Dict streamDict = single;
                QByteArray out;
                if (!decodeWith(streamDict, data, out)) {
                    throw Unsupported{QStringLiteral("inline image data")};
                }
                data = std::move(out);
            } else if (i + 1 == filterList.size()) {
                filter = name;
            } else {
                throw Unsupported{QStringLiteral("inline image filter")};
            }
        }
        auto entry = [&](const char* full, const char* abbreviation) {
            Value value = get(r, dict, full);
            if (value.isNull() && abbreviation) {
                value = get(r, dict, abbreviation);
            }
            return value;
        };
        const auto image = imageFrom(data, filter, entry, resources, std::nullopt);
        drawImage(*image);
    }

    /// Undoes a filter of an inline image with the decoder of the reader
    bool decodeWith(const Dict& dict, const QByteArray& raw, QByteArray& out) const {
        return r.decodeStream(raw, dict, out, nullptr);
    }

    void xobject(const QByteArray& name, const Dict& resources, int depth) {
        const Dict objects = resourceDict(resources, "XObject");
        const Value* ref = objects.find(name);
        if (!ref || ref->kind() != Value::Kind::Ref) {
            return;  // a missing object draws nothing, as in other viewers
        }
        const Value object = r.object(ref->toRef());
        if (object.kind() != Value::Kind::Dict) {
            return;
        }
        const Dict& dict = object.toDict();
        if (const Value* optional = dict.find("OC"); optional && m_drawer.hides(*optional)) {
            return;
        }
        const QByteArray subtype = get(r, dict, "Subtype").text();
        if (subtype == "Image") {
            auto& cache = m_drawer.m_images;
            std::shared_ptr<PageDrawer::Image> image = cache.value(ref->toRef().number);
            if (!image) {
                Dict streamDict;
                QByteArray data;
                QByteArray filter;
                if (!r.stream(ref->toRef(), streamDict, data, &filter)) {
                    throw Unsupported{QStringLiteral("image data")};
                }
                const Value smask = dict.find("SMask") ? *dict.find("SMask") : Value();
                std::optional<Ref> softMask;
                if (smask.kind() == Value::Kind::Ref) {
                    softMask = smask.toRef();
                }
                auto entry = [&](const char* full, const char*) { return get(r, dict, full); };
                image = imageFrom(data, filter, entry, resources, softMask);
                cache.insert(ref->toRef().number, image);
            }
            drawImage(*image);
        } else if (subtype == "Form") {
            Dict streamDict;
            QByteArray content;
            if (!r.stream(ref->toRef(), streamDict, content)) {
                throw Unsupported{QStringLiteral("form data")};
            }
            const Value group = get(r, dict, "Group");
            if (group.kind() == Value::Kind::Dict && !get(r, group.toDict(), "SMask").isNull()) {
                throw Unsupported{QStringLiteral("soft mask")};
            }
            form(content, dict, resources, depth);
        } else if (subtype != "PS") {
            throw Unsupported{QStringLiteral("object ") + QString::fromLatin1(subtype)};
        }
    }

    void form(const QByteArray& content, const Dict& dict, const Dict& parentResources, int depth,
              const QTransform& extra = QTransform()) {
        const State saved = s;
        const QPainterPath savedPath = m_path;
        s.ctm = extra * matrixOf(r, get(r, dict, "Matrix")) * s.ctm;
        s.base = s.ctm;
        const QRectF bbox = rectOf(r, get(r, dict, "BBox"));
        if (bbox.isValid()) {
            QPainterPath clip;
            clip.addRect(bbox);
            intersectClip(s.ctm.map(clip));
        }
        const Value own = get(r, dict, "Resources");
        run(content, own.kind() == Value::Kind::Dict ? own.toDict() : parentResources, depth + 1);
        s = saved;
        m_path = savedPath;
    }

    void graphicsState(const QByteArray& name, const Dict& resources) {
        const Dict states = resourceDict(resources, "ExtGState");
        const Value* ref = states.find(name);
        const Value state = ref ? r.resolve(*ref) : Value();
        if (state.kind() != Value::Kind::Dict) {
            return;
        }
        for (const auto& [key, raw]: state.toDict().entries()) {
            const Value value = r.resolve(raw);
            if (key == "CA") {
                s.strokeAlpha = value.toNumber();
            } else if (key == "ca") {
                s.fillAlpha = value.toNumber();
            } else if (key == "LW") {
                s.lineWidth = std::abs(value.toNumber());
            } else if (key == "LC") {
                s.cap = capOf(value.toInt());
            } else if (key == "LJ") {
                s.join = joinOf(value.toInt());
            } else if (key == "ML") {
                s.miterLimit = value.toNumber();
            } else if (key == "SMask") {
                s.softMask = softMaskOf(value, resources);
            } else if (key == "BM") {
                const QByteArray mode = value.kind() == Value::Kind::Array && !value.toArray().empty() ?
                                                value.toArray()[0].text() :
                                                value.text();
                // QPainter has most of them, but the PDF writer of Qt and the SVG generator leave them out: a
                // highlight that multiplies would hide the text below it when printed
                if (mode != "Normal" && mode != "Compatible") {
                    throw Unsupported{QStringLiteral("blend mode ") + QString::fromLatin1(mode)};
                }
            } else if (key == "D" && value.kind() == Value::Kind::Array && value.toArray().size() == 2) {
                s.dash.clear();
                const Value lengths = r.resolve(value.toArray()[0]);
                for (const Value& length: lengths.toArray()) {
                    s.dash.append(r.resolve(length).toNumber());
                }
                s.dashPhase = r.resolve(value.toArray()[1]).toNumber();
            } else if (key == "Font" && value.kind() == Value::Kind::Array && value.toArray().size() == 2) {
                if (value.toArray()[0].kind() == Value::Kind::Ref) {
                    s.font = font(value.toArray()[0].toRef());
                    s.fontSize = r.resolve(value.toArray()[1]).toNumber();
                }
            } else if (key == "TR" || key == "TR2") {
                if (!(value.kind() == Value::Kind::Name && (value.text() == "Identity" || value.text() == "Default"))) {
                    throw Unsupported{QStringLiteral("transfer function")};
                }
            }
        }
    }

    static Qt::PenCapStyle capOf(int value) {
        return value == 1 ? Qt::RoundCap : value == 2 ? Qt::SquareCap : Qt::FlatCap;
    }
    static Qt::PenJoinStyle joinOf(int value) {
        return value == 1 ? Qt::RoundJoin : value == 2 ? Qt::BevelJoin : Qt::MiterJoin;
    }

    // Fonts

    std::shared_ptr<PageDrawer::Font> font(Ref ref) {
        auto& cache = m_drawer.m_fonts;
        if (auto cached = cache.value(ref.number)) {
            return cached;
        }
        const Value object = r.object(ref);
        if (object.kind() != Value::Kind::Dict) {
            throw Unsupported{QStringLiteral("font")};
        }
        auto loaded = loadFont(object.toDict());
        cache.insert(ref.number, loaded);
        return loaded;
    }

    /// The font program of a font descriptor, if it is embedded
    QRawFont embeddedFont(const Dict& descriptor, QByteArray* type1Data) const {
        for (const char* key: {"FontFile2", "FontFile3", "FontFile"}) {
            const Value* value = descriptor.find(key);
            if (!value || value->kind() != Value::Kind::Ref) {
                continue;
            }
            Dict dict;
            QByteArray data;
            if (!r.stream(value->toRef(), dict, data)) {
                throw Unsupported{QStringLiteral("font program")};
            }
            if (qstrcmp(key, "FontFile") == 0 && type1Data) {
                *type1Data = data;
            }
            QRawFont raw(data, FONT_UNITS, QFont::PreferNoHinting);
            if (!raw.isValid()) {
                throw Unsupported{QStringLiteral("font program that Qt cannot read")};
            }
            return raw;
        }
        return {};
    }

    /// A font of the system for a font that is not embedded: the standard fonts of PDF and their relatives
    static QRawFont systemFont(QByteArray baseFont) {
        if (baseFont.size() > 7 && baseFont[6] == '+') {
            baseFont = baseFont.mid(7);  // the prefix of a subset
        }
        const QByteArray lower = baseFont.toLower();
        // Only the standard fonts of PDF and their usual relatives have an equivalent here; for any other font
        // (a Chinese one, say) the characters would not even be the right ones
        const bool standard = lower.startsWith("helvetica") || lower.startsWith("arial") || lower.startsWith("times") ||
                              lower.startsWith("courier");
        if (!standard) {
            throw Unsupported{QStringLiteral("font that is not embedded: ") + QString::fromLatin1(baseFont)};
        }
        QFont font;
        if (lower.contains("courier") || lower.contains("mono")) {
            font.setStyleHint(QFont::Monospace);
            font.setFamily(QStringLiteral("Courier New"));
        } else if (lower.contains("times") || lower.contains("serif") || lower.contains("roman")) {
            font.setStyleHint(QFont::Serif);
            font.setFamily(QStringLiteral("Times New Roman"));
        } else {
            font.setStyleHint(QFont::SansSerif);
            font.setFamily(QStringLiteral("Arial"));
        }
        font.setBold(lower.contains("bold") || lower.contains("black") || lower.contains("heavy"));
        font.setItalic(lower.contains("italic") || lower.contains("oblique"));
        font.setPixelSize(static_cast<int>(FONT_UNITS));
        font.setHintingPreference(QFont::PreferNoHinting);
        QRawFont raw = QRawFont::fromFont(font);
        if (!raw.isValid()) {
            throw Unsupported{QStringLiteral("no system font for ") + QString::fromLatin1(baseFont)};
        }
        return raw;
    }

    std::shared_ptr<PageDrawer::Font> loadFont(const Dict& dict) {
        auto result = std::make_shared<PageDrawer::Font>();
        const QByteArray subtype = get(r, dict, "Subtype").text();
        if (subtype == "Type0") {
            const QByteArray encoding = get(r, dict, "Encoding").text();
            if (encoding != "Identity-H") {
                throw Unsupported{QStringLiteral("CMap ") + QString::fromLatin1(encoding)};
            }
            const Value descendants = get(r, dict, "DescendantFonts");
            if (descendants.kind() != Value::Kind::Array || descendants.toArray().empty()) {
                throw Unsupported{QStringLiteral("composite font")};
            }
            const Value cidFont = r.resolve(descendants.toArray()[0]);
            if (cidFont.kind() != Value::Kind::Dict) {
                throw Unsupported{QStringLiteral("composite font")};
            }
            const Dict& cid = cidFont.toDict();
            // TrueType (type 2) or CFF (type 0). A CFF font that is CID-keyed has a charset that maps the CIDs to
            // its glyphs, which FreeType applies when it loads a glyph: the CID is the index then
            const QByteArray cidType = get(r, cid, "Subtype").text();
            if (cidType != "CIDFontType2" && cidType != "CIDFontType0") {
                throw Unsupported{QStringLiteral("CID font of type ") + QString::fromLatin1(cidType)};
            }
            const Value descriptor = get(r, cid, "FontDescriptor");
            if (descriptor.kind() == Value::Kind::Dict) {
                result->raw = embeddedFont(descriptor.toDict(), nullptr);
            }
            if (!result->raw.isValid()) {
                throw Unsupported{QStringLiteral("composite font that is not embedded")};
            }
            result->composite = true;
            const Value* map = cid.find("CIDToGIDMap");
            if (map && map->kind() == Value::Kind::Ref) {
                Dict mapDict;
                QByteArray data;
                if (!r.stream(map->toRef(), mapDict, data)) {
                    throw Unsupported{QStringLiteral("CIDToGIDMap")};
                }
                for (qsizetype i = 0; i + 1 < data.size(); i += 2) {
                    result->cidToGid.push_back(
                            static_cast<quint16>((static_cast<uchar>(data[i]) << 8) | static_cast<uchar>(data[i + 1])));
                }
            }
            result->defaultWidth = number(r, cid, "DW", 1000);
            const Value widths = get(r, cid, "W");
            if (widths.kind() == Value::Kind::Array) {
                const Array& w = widths.toArray();
                for (size_t i = 0; i + 1 < w.size();) {
                    const int first = r.resolve(w[i]).toInt();
                    const Value next = r.resolve(w[i + 1]);
                    if (next.kind() == Value::Kind::Array) {
                        int code = first;
                        for (const Value& width: next.toArray()) {
                            result->widths.insert(code++, r.resolve(width).toNumber());
                        }
                        i += 2;
                    } else if (i + 2 < w.size()) {
                        const int last = next.toInt();
                        const double width = r.resolve(w[i + 2]).toNumber();
                        for (int code = first; code <= last && code - first < 65536; ++code) {
                            result->widths.insert(code, width);
                        }
                        i += 3;
                    } else {
                        break;
                    }
                }
            }
            return result;
        }
        if (subtype == "Type3") {
            return loadType3Font(dict);
        }
        if (subtype != "Type1" && subtype != "TrueType" && subtype != "MMType1") {
            throw Unsupported{QStringLiteral("font of type ") + QString::fromLatin1(subtype)};
        }

        // A simple font: one byte per character
        const Value descriptor = get(r, dict, "FontDescriptor");
        QByteArray type1Data;
        bool symbolic = false;
        if (descriptor.kind() == Value::Kind::Dict) {
            result->raw = embeddedFont(descriptor.toDict(), &type1Data);
            symbolic = (static_cast<int>(number(r, descriptor.toDict(), "Flags", 0)) & 4) != 0;
        }
        const bool embedded = result->raw.isValid();
        if (!embedded) {
            result->raw = systemFont(get(r, dict, "BaseFont").text());
        }

        // The name of the glyph of each code: the encoding of the font, changed by the differences
        std::array<QByteArray, 256> names;
        const Value encoding = get(r, dict, "Encoding");
        const char* const* base = nullptr;
        QByteArray baseName = encoding.kind() == Value::Kind::Name ? encoding.text() : QByteArray();
        if (encoding.kind() == Value::Kind::Dict) {
            baseName = get(r, encoding.toDict(), "BaseEncoding").text();
        }
        if (baseName == "WinAnsiEncoding") {
            base = Encodings::WIN_ANSI;
        } else if (baseName == "MacRomanEncoding") {
            base = Encodings::MAC_ROMAN;
        } else if (baseName == "StandardEncoding") {
            base = Encodings::STANDARD;
        }
        if (base) {
            for (int code = 0; code < 256; ++code) {
                if (base[code]) {
                    names[static_cast<size_t>(code)] = base[code];
                }
            }
        } else if (!type1Data.isEmpty()) {
            names = type1Encoding(type1Data);  // the encoding of the font program
        } else if (!(subtype == "TrueType" && symbolic && embedded)) {
            for (int code = 0; code < 256; ++code) {
                if (Encodings::STANDARD[code]) {
                    names[static_cast<size_t>(code)] = Encodings::STANDARD[code];
                }
            }
        }
        if (encoding.kind() == Value::Kind::Dict) {
            const Value differences = get(r, encoding.toDict(), "Differences");
            int code = 0;
            for (const Value& item: differences.toArray()) {
                const Value value = r.resolve(item);
                if (value.kind() == Value::Kind::Number) {
                    code = value.toInt();
                } else if (value.kind() == Value::Kind::Name && code >= 0 && code < 256) {
                    names[static_cast<size_t>(code++)] = value.text();
                }
            }
        }

        // The glyph of each code: by the character of its name; by the number in a name like "g123" (the glyph of
        // that index, as some programs name them); for a symbolic TrueType font by its code. A name that says
        // nothing of the glyph makes the code unknown: guessing would show wrong letters
        const bool trueType = embedded && subtype == "TrueType";
        // For TrueType fonts the PDF specification says which of their maps is used (9.6.6.4)
        const TrueTypeCmaps cmaps(trueType ? result->raw.fontTable("cmap") : QByteArray());
        for (int code = 0; code < 256; ++code) {
            const QByteArray& name = names[static_cast<size_t>(code)];
            quint32 glyph = 0;
            // A code without a name is not in the encoding: nothing is drawn for it, except in a TrueType font,
            // where it is looked up by the code
            bool understood = (name.isEmpty() && !trueType) || name == "space" || name == ".notdef";
            const char16_t unicode = name.isEmpty() ? 0 : unicodeOfName(name);
            if (trueType) {
                if (unicode && cmaps.has(3, 1)) {
                    glyph = cmaps.glyph(3, 1, unicode);
                }
                if (glyph == 0) {
                    glyph = glyphOfIndexName(name);  // named by its index, as some programs do
                }
                if (glyph == 0 && cmaps.has(3, 0)) {
                    for (const quint32 base: {0x0000u, 0xF000u, 0xF100u, 0xF200u}) {
                        glyph = cmaps.glyph(3, 0, base + static_cast<quint32>(code));
                        if (glyph != 0) {
                            break;
                        }
                    }
                }
                if (glyph == 0 && cmaps.has(1, 0)) {
                    const int mac = unicode && !symbolic ? macRomanCode(unicode) : code;
                    glyph = mac >= 0 ? cmaps.glyph(1, 0, static_cast<quint32>(mac)) : 0;
                }
                if (glyph == 0 && unicode && !cmaps.has(3, 1) && !cmaps.has(3, 0) && !cmaps.has(1, 0)) {
                    glyph = glyphOf(result->raw, unicode);  // a font without the usual maps
                }
            } else if (unicode) {
                glyph = glyphOf(result->raw, unicode);
            }
            understood = understood || glyph != 0;
            result->glyphs[static_cast<size_t>(code)] = glyph;
            result->known[static_cast<size_t>(code)] = understood;
        }

        const int firstChar = static_cast<int>(number(r, dict, "FirstChar", 0));
        const Value widths = get(r, dict, "Widths");
        if (widths.kind() == Value::Kind::Array) {
            int code = firstChar;
            for (const Value& width: widths.toArray()) {
                result->widths.insert(code++, r.resolve(width).toNumber());
            }
        } else {
            result->widthsFromFont = true;
        }
        if (descriptor.kind() == Value::Kind::Dict) {
            result->defaultWidth = number(r, descriptor.toDict(), "MissingWidth", 0);
        }
        return result;
    }

    /// A font whose glyphs are content streams, named by the differences of its encoding
    std::shared_ptr<PageDrawer::Font> loadType3Font(const Dict& dict) {
        auto result = std::make_shared<PageDrawer::Font>();
        result->type3 = true;
        result->fontMatrix = matrixOf(r, get(r, dict, "FontMatrix"));
        if (result->fontMatrix.isIdentity() || !result->fontMatrix.isInvertible()) {
            result->fontMatrix = QTransform::fromScale(0.001, 0.001);
        }
        const Value resources = get(r, dict, "Resources");
        if (resources.kind() == Value::Kind::Dict) {
            result->resources = resources.toDict();
        }
        const Value procs = get(r, dict, "CharProcs");
        const Value encoding = get(r, dict, "Encoding");
        const Value differences =
                encoding.kind() == Value::Kind::Dict ? get(r, encoding.toDict(), "Differences") : Value();
        int code = 0;
        for (const Value& item: differences.toArray()) {
            const Value value = r.resolve(item);
            if (value.kind() == Value::Kind::Number) {
                code = value.toInt();
            } else if (value.kind() == Value::Kind::Name && code >= 0 && code < 256) {
                const Value* proc = procs.kind() == Value::Kind::Dict ? procs.toDict().find(value.text()) : nullptr;
                if (proc && proc->kind() == Value::Kind::Ref) {
                    result->procs[static_cast<size_t>(code)] = proc->toRef();
                }
                ++code;
            }
        }
        // Widths in the space of the glyphs, here in thousandths of the size as for other fonts
        const int firstChar = static_cast<int>(number(r, dict, "FirstChar", 0));
        const Value widths = get(r, dict, "Widths");
        int widthCode = firstChar;
        for (const Value& width: widths.toArray()) {
            result->widths.insert(widthCode++, r.resolve(width).toNumber() * result->fontMatrix.m11() * 1000);
        }
        result->defaultWidth = 0;
        return result;
    }

    /// Draws a glyph of a Type 3 font: its content stream, in the space of the glyph
    void drawType3Glyph(const PageDrawer::Font& font, int code) {
        const std::optional<Ref>& proc = font.procs[static_cast<size_t>(code)];
        Dict dict;
        QByteArray content;
        if (!proc || !r.stream(*proc, dict, content)) {
            return;  // a code without a glyph draws nothing
        }
        if (s.renderMode >= 4) {
            throw Unsupported{QStringLiteral("Type 3 font as a clip")};
        }
        const State saved = s;
        const QPainterPath savedPath = m_path;
        const QTransform textMatrix = m_textMatrix;
        const QTransform lineMatrix = m_lineMatrix;
        const bool ignoredColors = m_ignoreColors;
        s.ctm = font.fontMatrix * QTransform(s.fontSize * s.horizontalScale, 0, 0, s.fontSize, 0, s.rise) *
                m_textMatrix * s.ctm;
        s.base = s.ctm;
        m_path = QPainterPath();
        run(content, font.resources.isEmpty() ? m_resources : font.resources, m_depth + 1);
        s = saved;
        m_path = savedPath;
        m_textMatrix = textMatrix;
        m_lineMatrix = lineMatrix;
        m_ignoreColors = ignoredColors;
    }

    static quint32 glyphOf(const QRawFont& raw, char16_t unicode) {
        const QChar character(unicode);
        quint32 glyph = 0;
        int count = 1;
        if (!raw.glyphIndexesForChars(&character, 1, &glyph, &count)) {
            return 0;
        }
        return glyph;
    }

    // Text

    void showText(const QByteArray& bytes) {
        if (!s.font) {
            throw Unsupported{QStringLiteral("text without a font")};
        }
        const PageDrawer::Font& font = *s.font;
        const bool composite = font.composite;
        const int step = composite ? 2 : 1;
        const bool fill = s.renderMode == 0 || s.renderMode == 2 || s.renderMode == 4 || s.renderMode == 6;
        const bool stroke = s.renderMode == 1 || s.renderMode == 2 || s.renderMode == 5 || s.renderMode == 6;
        const bool clip = s.renderMode >= 4;
        for (qsizetype i = 0; i + step <= bytes.size(); i += step) {
            const int code = composite ? (static_cast<uchar>(bytes[i]) << 8) | static_cast<uchar>(bytes[i + 1]) :
                                         static_cast<uchar>(bytes[i]);
            quint32 glyph = 0;
            if (font.type3) {
                if (s.renderMode != 3 && m_hidden == 0) {
                    drawType3Glyph(font, code);
                }
            } else if (composite) {
                glyph = code < static_cast<int>(font.cidToGid.size()) ? font.cidToGid[static_cast<size_t>(code)] :
                                                                        static_cast<quint32>(code);
            } else {
                if (!font.known[static_cast<size_t>(code)]) {
                    throw Unsupported{QStringLiteral("character %1 not in its font").arg(code)};
                }
                glyph = font.glyphs[static_cast<size_t>(code)];
            }
            double width = font.widths.value(code, font.defaultWidth);
            if (!font.type3 && (font.widthsFromFont || (!composite && !font.widths.contains(code) && glyph != 0))) {
                const QList<QPointF> advances = font.raw.advancesForGlyphIndexes(QList<quint32>{glyph});
                width = advances.isEmpty() ? width : advances.first().x();
            }

            // From the space of the glyph (a thousandth of the size, y down in Qt) to the page
            const QTransform textToDevice =
                    QTransform(s.fontSize * s.horizontalScale, 0, 0, s.fontSize, 0, s.rise) * m_textMatrix * s.ctm;
            if (glyph != 0 && (fill || stroke || clip) && m_hidden == 0) {
                const QPainterPath outline = font.raw.pathForGlyph(glyph);
                if (!outline.isEmpty()) {
                    const QTransform glyphToText(1 / FONT_UNITS, 0, 0, -1 / FONT_UNITS, 0, 0);
                    const QTransform transform = glyphToText * textToDevice;
                    if ((fill || stroke) && s.softMask) {
                        // The glyph through the mask: drawn as text without it, into an image
                        const QRectF bounds = transform.map(outline).boundingRect();
                        const int savedMode = s.renderMode;
                        s.renderMode = fill && stroke ? 2 : fill ? 0 : 1;
                        masked(bounds.adjusted(-s.lineWidth, -s.lineWidth, s.lineWidth, s.lineWidth), [&] {
                            QPainterPath path = outline;
                            path.setFillRule(Qt::WindingFill);
                            applyClip();
                            setTransform(transform);
                            if (fill) {
                                m_painter->fillPath(path, brush(transform));
                            }
                            if (stroke) {
                                QPen textPen = pen(transform);
                                const double scale = std::sqrt(std::abs(transform.determinant())) /
                                                     std::max(1e-9, std::sqrt(std::abs(s.ctm.determinant())));
                                textPen.setWidthF(s.lineWidth / std::max(scale, 1e-9));
                                m_painter->strokePath(path, textPen);
                            }
                        });
                        s.renderMode = savedMode;
                    } else if (fill || stroke) {
                        applyClip();
                        setTransform(transform);
                        QPainterPath path = outline;
                        path.setFillRule(Qt::WindingFill);
                        if (fill && s.fillPattern && s.fillPattern->gradient.style() == Qt::NoBrush) {
                            fillTiling(*s.fillPattern, transform.map(path));
                            applyClip();
                            setTransform(transform);
                        } else if (fill) {
                            m_painter->fillPath(path, brush(transform));
                        }
                        if (stroke) {
                            if (s.strokePattern && s.strokePattern->gradient.style() == Qt::NoBrush) {
                                throw Unsupported{QStringLiteral("text stroked with a tiling pattern")};
                            }
                            QPen textPen = pen(transform);
                            // The line width is in the user space, the glyph in thousandths of the size
                            const double scale = std::sqrt(std::abs(transform.determinant())) /
                                                 std::max(1e-9, std::sqrt(std::abs(s.ctm.determinant())));
                            textPen.setWidthF(s.lineWidth / std::max(scale, 1e-9));
                            m_painter->strokePath(path, textPen);
                        }
                    }
                    if (clip) {
                        m_textClip.addPath(transform.map(outline));
                    }
                }
            }
            double advance = width / 1000.0 * s.fontSize + s.charSpacing;
            if (!composite && code == 32) {
                advance += s.wordSpacing;
            }
            m_textMatrix = QTransform::fromTranslate(advance * s.horizontalScale, 0) * m_textMatrix;
        }
    }

    void execute(const QByteArray& op, const std::vector<Operand>& o, const Dict& resources, int depth) {
        const char first = op.isEmpty() ? 0 : op[0];
        switch (first) {
            case 'q':
                if (op == "q") {
                    m_stack.push_back(s);
                    return;
                }
                break;
            case 'Q':
                if (op == "Q") {
                    if (!m_stack.empty()) {
                        s = m_stack.back();
                        m_stack.pop_back();
                    }
                    return;
                }
                break;
            default:
                break;
        }
        if (op == "cm") {
            need(o, 6);
            s.ctm = QTransform(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5)) * s.ctm;
        } else if (op == "w") {
            need(o, 1);
            s.lineWidth = std::abs(num(o, 0));  // viewers take a negative width as positive
        } else if (op == "J") {
            need(o, 1);
            s.cap = capOf(static_cast<int>(num(o, 0)));
        } else if (op == "j") {
            need(o, 1);
            s.join = joinOf(static_cast<int>(num(o, 0)));
        } else if (op == "M") {
            need(o, 1);
            s.miterLimit = num(o, 0);
        } else if (op == "d") {
            need(o, 2);
            s.dash.clear();
            if (o[0].array) {
                for (const Operand& length: *o[0].array) {
                    s.dash.append(length.number);
                }
            }
            s.dashPhase = num(o, 1);
        } else if (op == "ri" || op == "i") {
            // rendering intent and flatness: nothing to do
        } else if (op == "gs") {
            need(o, 1);
            graphicsState(o[0].text, resources);
        } else if (op == "m") {
            need(o, 2);
            m_path.moveTo(num(o, 0), num(o, 1));
        } else if (op == "l") {
            need(o, 2);
            m_path.lineTo(num(o, 0), num(o, 1));
        } else if (op == "c") {
            need(o, 6);
            m_path.cubicTo(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5));
        } else if (op == "v") {
            need(o, 4);
            m_path.cubicTo(m_path.currentPosition(), QPointF(num(o, 0), num(o, 1)), QPointF(num(o, 2), num(o, 3)));
        } else if (op == "y") {
            need(o, 4);
            m_path.cubicTo(QPointF(num(o, 0), num(o, 1)), QPointF(num(o, 2), num(o, 3)), QPointF(num(o, 2), num(o, 3)));
        } else if (op == "h") {
            m_path.closeSubpath();
        } else if (op == "re") {
            need(o, 4);
            m_path.addRect(QRectF(num(o, 0), num(o, 1), num(o, 2), num(o, 3)));
        } else if (op == "S") {
            paintPath(false, true, Qt::WindingFill);
        } else if (op == "s") {
            m_path.closeSubpath();
            paintPath(false, true, Qt::WindingFill);
        } else if (op == "f" || op == "F") {
            paintPath(true, false, Qt::WindingFill);
        } else if (op == "f*") {
            paintPath(true, false, Qt::OddEvenFill);
        } else if (op == "B") {
            paintPath(true, true, Qt::WindingFill);
        } else if (op == "B*") {
            paintPath(true, true, Qt::OddEvenFill);
        } else if (op == "b") {
            m_path.closeSubpath();
            paintPath(true, true, Qt::WindingFill);
        } else if (op == "b*") {
            m_path.closeSubpath();
            paintPath(true, true, Qt::OddEvenFill);
        } else if (op == "n") {
            paintPath(false, false, Qt::WindingFill);
        } else if (op == "W") {
            m_pendingClip = Qt::WindingFill;
        } else if (op == "W*") {
            m_pendingClip = Qt::OddEvenFill;
        } else if (m_ignoreColors &&
                   (op == "CS" || op == "cs" || op == "SC" || op == "SCN" || op == "sc" || op == "scn" || op == "G" ||
                    op == "g" || op == "RG" || op == "rg" || op == "K" || op == "k")) {
            // a glyph that has the colour of the text
        } else if (op == "CS") {
            need(o, 1);
            s.strokeSpace = colorSpace(Value::name(o[0].text), resources);
            s.stroke = initialColor(s.strokeSpace);
            s.strokePattern.reset();
        } else if (op == "cs") {
            need(o, 1);
            s.fillSpace = colorSpace(Value::name(o[0].text), resources);
            s.fill = initialColor(s.fillSpace);
            s.fillPattern.reset();
        } else if (op == "SC" || op == "SCN") {
            if (s.strokeSpace.kind == ColorSpace::Kind::Pattern) {
                s.strokePattern = pattern(o, resources);
            } else {
                s.stroke = colorOf(s.strokeSpace, o);
            }
        } else if (op == "sc" || op == "scn") {
            if (s.fillSpace.kind == ColorSpace::Kind::Pattern) {
                s.fillPattern = pattern(o, resources);
            } else {
                s.fill = colorOf(s.fillSpace, o);
            }
        } else if (op == "G" || op == "g" || op == "RG" || op == "rg" || op == "K" || op == "k") {
            ColorSpace space;
            const char kind = static_cast<char>(std::tolower(op[0]));
            space.kind = kind == 'g' ? ColorSpace::Kind::Gray :
                         kind == 'r' ? ColorSpace::Kind::Rgb :
                                       ColorSpace::Kind::Cmyk;
            space.components = kind == 'g' ? 1 : kind == 'r' ? 3 : 4;
            need(o, static_cast<size_t>(space.components));
            if (std::isupper(static_cast<uchar>(op[0]))) {
                s.strokeSpace = space;
                s.stroke = colorOf(space, o);
                s.strokePattern.reset();
            } else {
                s.fillSpace = space;
                s.fill = colorOf(space, o);
                s.fillPattern.reset();
            }
        } else if (op == "sh") {
            need(o, 1);
            paintShading(o[0].text, resources);
        } else if (op == "Do") {
            need(o, 1);
            xobject(o[0].text, resources, depth);
        } else if (op == "BT") {
            m_textMatrix = QTransform();
            m_lineMatrix = QTransform();
            m_textClip = QPainterPath();
        } else if (op == "ET") {
            if (s.renderMode >= 4) {
                intersectClip(m_textClip);
            }
        } else if (op == "Tc") {
            s.charSpacing = num(o, 0);
        } else if (op == "Tw") {
            s.wordSpacing = num(o, 0);
        } else if (op == "Tz") {
            s.horizontalScale = num(o, 0) / 100.0;
        } else if (op == "TL") {
            s.leading = num(o, 0);
        } else if (op == "Ts") {
            s.rise = num(o, 0);
        } else if (op == "Tr") {
            s.renderMode = static_cast<int>(num(o, 0));
        } else if (op == "Tf") {
            need(o, 2);
            const Dict fonts = resourceDict(resources, "Font");
            const Value* ref = fonts.find(o[0].text);
            if (!ref || ref->kind() != Value::Kind::Ref) {
                throw Unsupported{QStringLiteral("font that is missing")};
            }
            s.font = font(ref->toRef());
            s.fontSize = num(o, 1);
        } else if (op == "Td") {
            need(o, 2);
            m_lineMatrix = QTransform::fromTranslate(num(o, 0), num(o, 1)) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
        } else if (op == "TD") {
            need(o, 2);
            s.leading = -num(o, 1);
            m_lineMatrix = QTransform::fromTranslate(num(o, 0), num(o, 1)) * m_lineMatrix;
            m_textMatrix = m_lineMatrix;
        } else if (op == "Tm") {
            need(o, 6);
            m_lineMatrix = QTransform(num(o, 0), num(o, 1), num(o, 2), num(o, 3), num(o, 4), num(o, 5));
            m_textMatrix = m_lineMatrix;
        } else if (op == "T*") {
            nextLine();
        } else if (op == "Tj") {
            need(o, 1);
            showText(o[0].text);
        } else if (op == "'") {
            need(o, 1);
            nextLine();
            showText(o[0].text);
        } else if (op == "\"") {
            need(o, 3);
            s.wordSpacing = num(o, 0);
            s.charSpacing = num(o, 1);
            nextLine();
            showText(o[2].text);
        } else if (op == "TJ") {
            need(o, 1);
            if (o[0].array) {
                for (const Operand& item: *o[0].array) {
                    if (item.kind == Operand::Kind::String) {
                        showText(item.text);
                    } else {
                        const double shift = -item.number / 1000.0 * s.fontSize * s.horizontalScale;
                        m_textMatrix = QTransform::fromTranslate(shift, 0) * m_textMatrix;
                    }
                }
            }
        } else if (op == "BMC") {
            m_marks.push_back(false);
        } else if (op == "BDC") {
            // Optional content: /OC with a name of the resources or a dictionary
            bool hidden = false;
            if (o.size() >= 2 && o[0].text == "OC" && o[1].kind == Operand::Kind::Name) {
                const Dict properties = resourceDict(resources, "Properties");
                if (const Value* group = properties.find(o[1].text)) {
                    hidden = m_drawer.hides(*group);
                }
            }
            m_marks.push_back(hidden);
            m_hidden += hidden;
        } else if (op == "EMC") {
            if (!m_marks.empty()) {
                m_hidden -= m_marks.back();
                m_marks.pop_back();
            }
        } else if (op == "d1") {
            // A glyph of a Type 3 font without colours of its own: it has the colour the text is shown in
            m_ignoreColors = true;
        } else if (op == "d0" || op == "BX" || op == "EX" || op == "MP" || op == "DP") {
            // the width of a glyph, compatibility sections, marked content: nothing to draw
        } else {
            throw Unsupported{QStringLiteral("operator ") + QString::fromLatin1(op)};
        }
    }

    void nextLine() {
        m_lineMatrix = QTransform::fromTranslate(0, -s.leading) * m_lineMatrix;
        m_textMatrix = m_lineMatrix;
    }

    PageDrawer& m_drawer;
    const Reader& r;
    /// What is drawn into: the picture of the page, or an image of a part that a soft mask covers
    QPainter* m_painter;
    /// From the space of the page to the painter: identity, or the place and scale of such an image
    QTransform m_deviceBase;
    std::vector<State> m_stack;
    QPainterPath m_path;
    std::optional<Qt::FillRule> m_pendingClip;
    QTransform m_textMatrix;
    QTransform m_lineMatrix;
    QPainterPath m_textClip;
    qint64 m_operations = 0;
    /// Marked content that is open, and whether it hides what is in it; how many of them hide
    std::vector<bool> m_marks;
    int m_hidden = 0;
    int m_depth = 0;
    Dict m_resources;
    /// In a glyph of a Type 3 font that has the colour of the text (d1): colours it sets are not used
    bool m_ignoreColors = false;

public:
    /// The page in the coordinates of the painter, and the resolution of what soft masks cover
    QRectF m_pageArea;
    double m_maskScale = SOFT_MASK_SCALE;

private:
public:
    /// The content of a page or of an appearance: one stream or several
    QByteArray contentOf(const Value& value) const {
        QByteArray content;
        const Value resolved = r.resolve(value);
        Array parts;
        if (value.kind() == Value::Kind::Ref && resolved.kind() != Value::Kind::Array) {
            parts.push_back(value);
        } else if (resolved.kind() == Value::Kind::Array) {
            parts = resolved.toArray();
        }
        for (const Value& part: parts) {
            Dict dict;
            QByteArray data;
            if (part.kind() != Value::Kind::Ref || !r.stream(part.toRef(), dict, data)) {
                throw Unsupported{QStringLiteral("content of the page")};
            }
            content += data;
            content += '\n';
        }
        return content;
    }

    /// The annotations of a page that have an appearance, drawn as viewers draw them
    void annotations(const Dict& page, const Dict& resources) {
        const Value annots = get(r, page, "Annots");
        for (const Value& item: annots.toArray()) {
            const Value annotation = r.resolve(item);
            if (annotation.kind() != Value::Kind::Dict) {
                continue;
            }
            const Dict& a = annotation.toDict();
            const int flags = static_cast<int>(number(r, a, "F", 0));
            const QByteArray subtype = get(r, a, "Subtype").text();
            const Value* optional = a.find("OC");
            // Hidden (2) or not to be shown (32)
            if ((flags & 2) || (flags & 32) || subtype == "Popup" || (optional && m_drawer.hides(*optional))) {
                continue;  // hidden
            }
            const Value appearances = get(r, a, "AP");
            if (appearances.kind() != Value::Kind::Dict) {
                // Viewers make up an appearance for most kinds (a square, an underline, ...): not done here
                if (subtype != "Link" && subtype != "Widget") {
                    throw Unsupported{QStringLiteral("annotation without appearance")};
                }
                continue;
            }
            const Value* normalValue = appearances.toDict().find("N");
            if (!normalValue) {
                continue;
            }
            Value normal = r.resolve(*normalValue);
            Ref streamRef;
            if (normalValue->kind() == Value::Kind::Ref && normal.kind() == Value::Kind::Dict &&
                normal.toDict().find("BBox")) {
                streamRef = normalValue->toRef();
            } else if (normal.kind() == Value::Kind::Dict) {
                // Appearances by state, as of check boxes
                const Value* chosen = normal.toDict().find(get(r, a, "AS").text());
                if (!chosen || chosen->kind() != Value::Kind::Ref) {
                    continue;
                }
                streamRef = chosen->toRef();
                normal = r.object(streamRef);
            } else {
                continue;
            }
            Dict dict;
            QByteArray content;
            if (!r.stream(streamRef, dict, content)) {
                throw Unsupported{QStringLiteral("appearance of an annotation")};
            }
            // The box of the appearance, transformed by its matrix, is fitted into the rectangle of the annotation
            const QRectF rect = rectOf(r, get(r, a, "Rect"));
            const QRectF bbox = matrixOf(r, get(r, dict, "Matrix")).mapRect(rectOf(r, get(r, dict, "BBox")));
            if (!rect.isValid() || bbox.width() <= 0 || bbox.height() <= 0) {
                continue;
            }
            const QTransform fit = QTransform::fromTranslate(-bbox.left(), -bbox.top()) *
                                   QTransform::fromScale(rect.width() / bbox.width(), rect.height() / bbox.height()) *
                                   QTransform::fromTranslate(rect.left(), rect.top());
            const State saved = s;
            s.ctm = fit * s.ctm;
            const Dict resourcesOfForm = dict;
            form(content, resourcesOfForm, resources, 0);
            s = saved;
        }
    }
};

PageDrawer::PageDrawer(const Reader& reader): m_reader(reader) {
    // The default configuration of the optional content: which groups are off
    const Value* rootValue = reader.trailer().find("Root");
    const Value root = rootValue ? reader.resolve(*rootValue) : Value();
    if (root.kind() != Value::Kind::Dict) {
        return;
    }
    const Value properties = get(reader, root.toDict(), "OCProperties");
    if (properties.kind() != Value::Kind::Dict) {
        return;
    }
    const Value config = get(reader, properties.toDict(), "D");
    if (config.kind() != Value::Kind::Dict) {
        return;
    }
    auto numbers = [&](const Value& list) {
        QSet<int> result;
        const Value groups = reader.resolve(list);  // not a temporary: the loop goes over a part of it
        for (const Value& item: groups.toArray()) {
            if (item.kind() == Value::Kind::Ref) {
                result.insert(item.toRef().number);
            }
        }
        return result;
    };
    if (get(reader, config.toDict(), "BaseState").text() == "OFF") {
        const Value* all = properties.toDict().find("OCGs");
        m_hiddenGroups = all ? numbers(*all) : QSet<int>();
        if (const Value* on = config.toDict().find("ON")) {
            m_hiddenGroups.subtract(numbers(*on));
        }
    }
    if (const Value* off = config.toDict().find("OFF")) {
        m_hiddenGroups.unite(numbers(*off));
    }
}

bool PageDrawer::hides(const Value& optionalContent) const {
    if (m_hiddenGroups.isEmpty()) {
        return false;
    }
    if (optionalContent.kind() == Value::Kind::Ref && m_hiddenGroups.contains(optionalContent.toRef().number)) {
        return true;
    }
    const Value resolved = m_reader.resolve(optionalContent);
    if (resolved.kind() != Value::Kind::Dict || get(m_reader, resolved.toDict(), "Type").text() != "OCMD") {
        return false;
    }
    // A membership dictionary: visible by the policy over its groups
    const Value* groupsValue = resolved.toDict().find("OCGs");
    Array groups;
    if (groupsValue && m_reader.resolve(*groupsValue).kind() == Value::Kind::Array) {
        groups = m_reader.resolve(*groupsValue).toArray();
    } else if (groupsValue) {
        groups.push_back(*groupsValue);
    }
    int on = 0;
    int off = 0;
    for (const Value& group: groups) {
        if (group.kind() == Value::Kind::Ref && m_hiddenGroups.contains(group.toRef().number)) {
            ++off;
        } else {
            ++on;
        }
    }
    const QByteArray policy = get(m_reader, resolved.toDict(), "P").text();
    if (policy == "AllOn") {
        return off > 0;
    }
    if (policy == "AnyOff") {
        return off == 0;
    }
    if (policy == "AllOff") {
        return on > 0;
    }
    return on == 0 && !groups.empty();  // AnyOn
}

PageDrawer::~PageDrawer() = default;

bool PageDrawer::draw(QPainter& painter, const PageInfo& page, const QSizeF& size) {
    m_why.clear();
    QPicture picture;
    try {
        QPainter recorder(&picture);
        recorder.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        Interpreter interpreter(*this, recorder);

        // From the space of the page to the rectangle: the crop box, turned by the rotation, fills it
        const Dict& dict = page.dict;
        QRectF box = rectOf(m_reader, get(m_reader, dict, "CropBox"));
        QRectF media = rectOf(m_reader, get(m_reader, dict, "MediaBox"));
        if (!media.isValid() && !box.isValid()) {
            media = QRectF(0, 0, 612, 792);  // as viewers show a page without a size: US Letter
        }
        box = box.isValid() ? box.intersected(media.isValid() ? media : box) : media;
        if (!box.isValid() || size.isEmpty()) {
            throw Unsupported{QStringLiteral("page without a size")};
        }
        const int rotate = ((static_cast<int>(number(m_reader, dict, "Rotate", 0)) % 360) + 360) % 360;
        const bool sideways = rotate == 90 || rotate == 270;
        const double sx = size.width() / (sideways ? box.height() : box.width());
        const double sy = size.height() / (sideways ? box.width() : box.height());
        QTransform toRect;
        switch (rotate) {
            case 90:
                // Turned clockwise: y of the page goes to the right, x down
                toRect = QTransform::fromTranslate(-box.left(), -box.top()) * QTransform(0, sy, sx, 0, 0, 0);
                break;
            case 180:
                toRect = QTransform::fromTranslate(-box.right(), -box.top()) * QTransform(-sx, 0, 0, sy, 0, 0);
                break;
            case 270:
                toRect = QTransform::fromTranslate(-box.right(), -box.bottom()) * QTransform(0, -sy, -sx, 0, 0, 0);
                break;
            default:
                toRect = QTransform::fromTranslate(-box.left(), -box.bottom()) * QTransform(sx, 0, 0, -sy, 0, 0);
                break;
        }
        interpreter.s.ctm = toRect;
        interpreter.s.base = toRect;
        QPainterPath area;
        area.addRect(QRectF(QPointF(0, 0), size));
        interpreter.s.clip = area;
        interpreter.m_pageArea = QRectF(QPointF(0, 0), size);
        // Pages drawn large get no larger images of what soft masks cover
        interpreter.m_maskScale = std::min(SOFT_MASK_SCALE, 6000.0 / std::max(size.width(), size.height()));

        const Value resourcesValue = get(m_reader, dict, "Resources");
        const Dict resources = resourcesValue.kind() == Value::Kind::Dict ? resourcesValue.toDict() : Dict();
        const Value* contents = dict.find("Contents");
        if (contents) {
            interpreter.run(interpreter.contentOf(*contents), resources, 0);
        }
        interpreter.s = Interpreter::State();
        interpreter.s.ctm = toRect;
        interpreter.s.base = toRect;
        interpreter.s.clip = area;
        interpreter.annotations(dict, resources);
        recorder.end();
    } catch (const Unsupported& unsupported) {
        m_why = unsupported.what;
        return false;
    }
    painter.save();
    // A picture scales itself from the resolution of the screen to that of the device it is played on
    if (QPaintDevice* device = painter.device(); device && device->logicalDpiX() > 0 && device->logicalDpiY() > 0) {
        painter.scale(static_cast<qreal>(picture.logicalDpiX()) / device->logicalDpiX(),
                      static_cast<qreal>(picture.logicalDpiY()) / device->logicalDpiY());
    }
    painter.drawPicture(0, 0, picture);
    painter.restore();
    return true;
}

}  // namespace Pdf
