#include "PdfFile.h"

#include <QCoreApplication>
#include <algorithm>
#include <cctype>
#include <cmath>

#include <zlib.h>

#include "PdfCrypt.h"

namespace Pdf {

namespace {

constexpr int MAX_DEPTH = 64;  // of nested values, of the chain of cross-reference sections, of the page tree

QString tr(const char* text) { return QCoreApplication::translate("Pdf", text); }

bool isSpace(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; }

bool isDelimiter(char c) {
    return c == '(' || c == ')' || c == '<' || c == '>' || c == '[' || c == ']' || c == '{' || c == '}' || c == '/' ||
           c == '%';
}

/// Decompresses a zlib stream of unknown size
bool inflateData(const QByteArray& in, QByteArray& out) {
    z_stream stream{};
    if (inflateInit(&stream) != Z_OK) {
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.constData()));
    stream.avail_in = static_cast<uInt>(in.size());
    char buffer[16384];
    int status = Z_OK;
    while (status == Z_OK) {
        stream.next_out = reinterpret_cast<Bytef*>(buffer);
        stream.avail_out = sizeof(buffer);
        status = inflate(&stream, Z_NO_FLUSH);
        out.append(buffer, static_cast<qsizetype>(sizeof(buffer) - stream.avail_out));
    }
    inflateEnd(&stream);
    // Some files cut the checksum at the end: what was decoded counts
    return status == Z_STREAM_END || !out.isEmpty();
}

/// Undoes the PNG predictors, with which cross-reference streams and images are often written
bool undoPredictor(QByteArray& data, int predictor, int columns, int colors = 1, int bitsPerComponent = 8) {
    if (predictor < 10) {
        return predictor <= 1;  // the TIFF predictor is rare
    }
    // Bytes per pixel (at least one) and per row
    const int pixel = std::max(1, colors * bitsPerComponent / 8);
    const qsizetype bytes = (static_cast<qsizetype>(columns) * colors * bitsPerComponent + 7) / 8;
    const qsizetype row = bytes + 1;
    if (columns <= 0 || data.size() < row) {
        return false;
    }
    QByteArray out;
    out.reserve(data.size());
    QByteArray previous(bytes, '\0');
    for (qsizetype start = 0; start + row <= data.size(); start += row) {
        const int filter = static_cast<uchar>(data[start]);
        QByteArray line = data.mid(start + 1, bytes);
        for (qsizetype i = 0; i < bytes; ++i) {
            const int left = i >= pixel ? static_cast<uchar>(line[i - pixel]) : 0;
            const int up = static_cast<uchar>(previous[i]);
            const int upLeft = i >= pixel ? static_cast<uchar>(previous[i - pixel]) : 0;
            int add = 0;
            switch (filter) {
                case 1:
                    add = left;
                    break;
                case 2:
                    add = up;
                    break;
                case 3:
                    add = (left + up) / 2;
                    break;
                case 4: {
                    const int p = left + up - upLeft;
                    const int pa = std::abs(p - left);
                    const int pb = std::abs(p - up);
                    const int pc = std::abs(p - upLeft);
                    add = pa <= pb && pa <= pc ? left : pb <= pc ? up : upLeft;
                    break;
                }
                default:
                    break;
            }
            line[i] = static_cast<char>((static_cast<uchar>(line[i]) + add) & 0xff);
        }
        out.append(line);
        previous = line;
    }
    data = out;
    return true;
}

bool asciiHexDecode(const QByteArray& in, QByteArray& out) {
    QByteArray digits;
    for (char c: in) {
        if (c == '>') {
            break;
        }
        if (std::isxdigit(static_cast<uchar>(c))) {
            digits.append(c);
        }
    }
    if (digits.size() % 2 != 0) {
        digits.append('0');
    }
    out = QByteArray::fromHex(digits);
    return true;
}

bool ascii85Decode(const QByteArray& in, QByteArray& out) {
    out.clear();
    quint32 tuple = 0;
    int count = 0;
    for (qsizetype i = 0; i < in.size(); ++i) {
        const char c = in[i];
        if (c == '~') {
            break;
        }
        if (c == 'z' && count == 0) {
            out.append(4, '\0');
            continue;
        }
        if (c < '!' || c > 'u') {
            continue;  // white space
        }
        tuple = tuple * 85 + static_cast<quint32>(c - '!');
        if (++count == 5) {
            for (int shift = 24; shift >= 0; shift -= 8) {
                out.append(static_cast<char>((tuple >> shift) & 0xff));
            }
            tuple = 0;
            count = 0;
        }
    }
    if (count > 1) {
        for (int k = count; k < 5; ++k) {
            tuple = tuple * 85 + 84;
        }
        for (int k = 0; k < count - 1; ++k) {
            out.append(static_cast<char>((tuple >> (24 - 8 * k)) & 0xff));
        }
    }
    return true;
}

QByteArray numberText(double value) {
    if (std::abs(value - std::round(value)) < 1e-9 && std::abs(value) < 1e15) {
        return QByteArray::number(static_cast<qint64>(std::llround(value)));
    }
    QByteArray text = QByteArray::number(value, 'f', 5);
    while (text.endsWith('0')) {
        text.chop(1);
    }
    if (text.endsWith('.')) {
        text.chop(1);
    }
    return text;
}

}  // namespace

/// Reads values from the bytes of a file
class Parser {
public:
    Parser(const QByteArray& data, qint64 pos): d(data), p(pos) {}

    qint64 pos() const { return p; }
    bool failed() const { return m_failed; }

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

    /// Whether a keyword follows; it is read if so
    bool keyword(const char* word) {
        skipSpace();
        const qsizetype length = static_cast<qsizetype>(qstrlen(word));
        if (d.mid(p, length) != word) {
            return false;
        }
        const qint64 end = p + length;
        if (end < d.size() && !isSpace(d[end]) && !isDelimiter(d[end])) {
            return false;
        }
        p = end;
        return true;
    }

    /// An integer, without looking for a reference after it
    bool integer(qint64& value) {
        skipSpace();
        const qint64 start = p;
        if (p < d.size() && (d[p] == '+' || d[p] == '-')) {
            ++p;
        }
        while (p < d.size() && d[p] >= '0' && d[p] <= '9') {
            ++p;
        }
        bool ok = false;
        value = d.mid(start, p - start).toLongLong(&ok);
        if (!ok) {
            p = start;
        }
        return ok;
    }

    Value value(int depth = 0) {
        skipSpace();
        Value v;
        if (p >= d.size() || depth > MAX_DEPTH) {
            m_failed = true;
            return v;
        }
        const char c = d[p];
        if (c == '/') {
            ++p;
            const qint64 start = p;
            while (p < d.size() && !isSpace(d[p]) && !isDelimiter(d[p])) {
                ++p;
            }
            v.m_kind = Value::Kind::Name;
            v.m_text = d.mid(start, p - start);
        } else if (c == '(') {
            const qint64 start = p;
            int open = 0;
            do {
                if (d[p] == '\\') {
                    ++p;
                } else if (d[p] == '(') {
                    ++open;
                } else if (d[p] == ')') {
                    --open;
                }
                ++p;
            } while (p < d.size() && open > 0);
            m_failed = m_failed || open > 0;
            v.m_kind = Value::Kind::String;
            v.m_text = d.mid(start, p - start);
        } else if (c == '<' && p + 1 < d.size() && d[p + 1] == '<') {
            p += 2;
            v.m_kind = Value::Kind::Dict;
            while (true) {
                skipSpace();
                if (p + 1 < d.size() && d[p] == '>' && d[p + 1] == '>') {
                    p += 2;
                    break;
                }
                if (p >= d.size() || d[p] != '/') {
                    m_failed = true;
                    break;
                }
                const Value key = value(depth + 1);
                Value entry = value(depth + 1);
                if (m_failed) {
                    break;
                }
                v.m_dict.set(key.text(), std::move(entry));
            }
        } else if (c == '<') {
            const qint64 start = p;
            while (p < d.size() && d[p] != '>') {
                ++p;
            }
            m_failed = m_failed || p >= d.size();
            ++p;
            v.m_kind = Value::Kind::String;
            v.m_text = d.mid(start, p - start);
        } else if (c == '[') {
            ++p;
            v.m_kind = Value::Kind::Array;
            while (true) {
                skipSpace();
                if (p < d.size() && d[p] == ']') {
                    ++p;
                    break;
                }
                Value entry = value(depth + 1);
                if (m_failed) {
                    break;
                }
                v.m_array.push_back(std::move(entry));
            }
        } else if ((c >= '0' && c <= '9') || c == '+' || c == '-' || c == '.') {
            const qint64 start = p;
            ++p;
            while (p < d.size() && ((d[p] >= '0' && d[p] <= '9') || d[p] == '.')) {
                ++p;
            }
            v.m_kind = Value::Kind::Number;
            v.m_text = d.mid(start, p - start);
            v.m_number = v.m_text.toDouble();
            // "12 0 R" is a reference
            if (!v.m_text.contains('.') && c != '+' && c != '-') {
                const qint64 after = p;
                qint64 generation = 0;
                if (integer(generation) && keyword("R")) {
                    v.m_kind = Value::Kind::Ref;
                    v.m_ref = {v.toInt(), static_cast<int>(generation)};
                    v.m_text.clear();
                } else {
                    p = after;
                }
            }
        } else if (keyword("true")) {
            v = Value::boolean(true);
        } else if (keyword("false")) {
            v = Value::boolean(false);
        } else if (keyword("null")) {
            // null it is
        } else {
            m_failed = true;
        }
        return v;
    }

private:
    const QByteArray& d;
    qint64 p;
    bool m_failed = false;
};

// Values

const Value* Dict::find(const QByteArray& key) const {
    for (const auto& entry: m_entries) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

Value* Dict::find(const QByteArray& key) { return const_cast<Value*>(std::as_const(*this).find(key)); }

void Dict::set(const QByteArray& key, Value value) {
    if (Value* existing = find(key)) {
        *existing = std::move(value);
    } else {
        m_entries.emplace_back(key, std::move(value));
    }
}

void Dict::remove(const QByteArray& key) {
    m_entries.erase(
            std::remove_if(m_entries.begin(), m_entries.end(), [&](const auto& entry) { return entry.first == key; }),
            m_entries.end());
}

Value Value::boolean(bool value) {
    Value v;
    v.m_kind = Kind::Bool;
    v.m_number = value ? 1 : 0;
    return v;
}

Value Value::number(double value) {
    Value v;
    v.m_kind = Kind::Number;
    v.m_number = value;
    return v;
}

Value Value::name(const QByteArray& name) {
    Value v;
    v.m_kind = Kind::Name;
    v.m_text = name;
    return v;
}

Value Value::array(Array array) {
    Value v;
    v.m_kind = Kind::Array;
    v.m_array = std::move(array);
    return v;
}

Value Value::dict(Dict dict) {
    Value v;
    v.m_kind = Kind::Dict;
    v.m_dict = std::move(dict);
    return v;
}

Value Value::ref(Ref ref) {
    Value v;
    v.m_kind = Kind::Ref;
    v.m_ref = ref;
    return v;
}

Value Value::string(const QByteArray& bytes) {
    Value v;
    v.m_kind = Kind::String;
    v.m_text = '<' + bytes.toHex() + '>';
    return v;
}

QByteArray Value::stringBytes() const {
    if (m_kind != Kind::String || m_text.isEmpty()) {
        return {};
    }
    if (m_text.startsWith('<')) {
        QByteArray digits;
        for (char c: m_text.mid(1, m_text.size() - 2)) {
            if (std::isxdigit(static_cast<uchar>(c))) {
                digits.append(c);
            }
        }
        if (digits.size() % 2) {
            digits.append('0');  // a missing last digit is 0
        }
        return QByteArray::fromHex(digits);
    }
    // A literal string: between its parentheses, with escapes
    QByteArray out;
    const QByteArray& t = m_text;
    for (qsizetype i = 1; i + 1 < t.size(); ++i) {
        const char c = t[i];
        if (c != '\\') {
            out.append(c);
            continue;
        }
        if (++i + 1 > t.size() - 1) {
            break;
        }
        const char e = t[i];
        switch (e) {
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
                if (i + 1 < t.size() && t[i + 1] == '\n') {
                    ++i;  // a line that goes on
                }
                break;
            case '\n':
                break;
            default:
                if (e >= '0' && e <= '7') {
                    int value = e - '0';
                    for (int k = 0; k < 2 && i + 1 < t.size() - 1 && t[i + 1] >= '0' && t[i + 1] <= '7'; ++k) {
                        value = value * 8 + (t[++i] - '0');
                    }
                    out.append(static_cast<char>(value & 0xff));
                } else {
                    out.append(e);  // \(, \), \\ and unknown escapes
                }
        }
    }
    return out;
}

QByteArray Value::serialize() const {
    switch (m_kind) {
        case Kind::Null:
            return "null";
        case Kind::Bool:
            return m_number != 0 ? "true" : "false";
        case Kind::Number:
            return m_text.isEmpty() ? numberText(m_number) : m_text;
        case Kind::Name:
            return '/' + m_text;
        case Kind::String:
            return m_text;
        case Kind::Ref:
            return QByteArray::number(m_ref.number) + ' ' + QByteArray::number(m_ref.generation) + " R";
        case Kind::Array: {
            QByteArray out = "[";
            for (size_t i = 0; i < m_array.size(); ++i) {
                out += (i > 0 ? " " : "") + m_array[i].serialize();
            }
            return out + ']';
        }
        case Kind::Dict: {
            QByteArray out = "<<";
            for (const auto& entry: m_dict.entries()) {
                out += '/' + entry.first + ' ' + entry.second.serialize() + ' ';
            }
            return out + ">>";
        }
    }
    return {};
}

// Reader

Reader::Reader() = default;
Reader::~Reader() = default;

bool Reader::fail(const QString& message) {
    if (m_error.isEmpty()) {
        m_error = message;
    }
    return false;
}

bool Reader::load(const QByteArray& data) {
    m_data = data;
    m_error.clear();
    // Some files have something before their header, which their offsets do not count
    if (!m_data.startsWith("%PDF-")) {
        const qsizetype header = m_data.left(1024).indexOf("%PDF-");
        if (header < 0) {
            return fail(tr("Not a PDF file"));
        }
        m_data = m_data.mid(header);
    }
    if (loadWithTable()) {
        return true;
    }
    // A damaged file: unless it is a password that is missing, its objects are looked for. The first error is the
    // one that is told
    if (m_error == tr("The PDF file is protected by a password")) {
        return false;
    }
    const QString error = m_error;
    if (loadRepaired()) {
        m_error.clear();
        return true;
    }
    // A file whose table is right but which has no pages
    if (error == tr("The PDF file has no pages")) {
        m_error.clear();
        if (loadWithTable(false)) {
            return true;
        }
    }
    m_error = error;
    return false;
}

bool Reader::loadWithTable(bool requirePages) {
    m_trailer = Dict();
    m_entries.clear();
    m_cache.clear();
    m_objectStreams.clear();
    m_crypt.reset();
    m_encryptObject = -1;
    m_repaired = false;
    m_size = 0;

    const qsizetype marker = m_data.lastIndexOf("startxref");
    Parser parser(m_data, marker + 9);
    qint64 offset = 0;
    if (marker < 0 || !parser.integer(offset) || offset <= 0 || offset >= m_data.size()) {
        return fail(tr("The PDF file has no table of its objects"));
    }
    m_xrefOffset = offset;
    Parser probe(m_data, offset);
    m_xrefStream = !probe.keyword("xref");
    if (!readXref(offset, 0)) {
        return fail(tr("The table of the objects of the PDF file cannot be read"));
    }
    if (m_trailer.find("Encrypt") && !setUpEncryption()) {
        return false;
    }
    if (!m_trailer.find("Root")) {
        return fail(tr("The PDF file has no document catalog"));
    }
    if (const Value* size = m_trailer.find("Size")) {
        m_size = std::max(m_size, size->toInt());
    }
    // A table that leads to no pages is wrong
    if (requirePages && pages().empty()) {
        return fail(tr("The PDF file has no pages"));
    }
    return true;
}

bool Reader::loadRepaired() {
    m_trailer = Dict();
    m_entries.clear();
    m_cache.clear();
    m_objectStreams.clear();
    m_crypt.reset();
    m_encryptObject = -1;
    m_repaired = true;
    m_size = 0;

    // Every "n g obj" at the start of a token; a later one of the same number replaces an earlier one, as in
    // incremental updates
    for (qsizetype at = m_data.indexOf("obj"); at >= 0; at = m_data.indexOf("obj", at + 3)) {
        if (at + 3 < m_data.size() && !isSpace(m_data[at + 3]) && !isDelimiter(m_data[at + 3])) {
            continue;
        }
        // Back over the generation and the number
        qsizetype p = at;
        auto digitsBefore = [&](qsizetype& end) {
            while (end > 0 && isSpace(m_data[end - 1])) {
                --end;
            }
            const qsizetype last = end;
            while (end > 0 && m_data[end - 1] >= '0' && m_data[end - 1] <= '9') {
                --end;
            }
            return end < last;
        };
        if (!digitsBefore(p)) {
            continue;
        }
        const qsizetype generationStart = p;
        if (!digitsBefore(p) || (p > 0 && !isSpace(m_data[p - 1]) && !isDelimiter(m_data[p - 1]))) {
            continue;
        }
        Parser parser(m_data, p);
        qint64 number = 0;
        qint64 generation = 0;
        if (!parser.integer(number) || !parser.integer(generation) || number <= 0 || number > 10'000'000 ||
            generationStart <= p) {
            continue;
        }
        m_entries.insert(static_cast<int>(number), Entry{1, p, static_cast<int>(generation)});
        m_size = std::max(m_size, static_cast<int>(number) + 1);
    }
    if (m_entries.isEmpty()) {
        return fail(tr("No objects were found in the PDF file"));
    }
    // The trailers, and the cross-reference streams that are trailers too: later ones count more
    std::vector<Dict> trailers;
    for (qsizetype at = m_data.indexOf("trailer"); at >= 0; at = m_data.indexOf("trailer", at + 7)) {
        Parser parser(m_data, at + 7);
        const Value trailer = parser.value();
        if (!parser.failed() && trailer.kind() == Value::Kind::Dict) {
            trailers.push_back(trailer.toDict());
        }
    }
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
        Parser parser(m_data, it->offset);
        qint64 number = 0;
        qint64 generation = 0;
        if (!parser.integer(number) || !parser.integer(generation) || !parser.keyword("obj")) {
            continue;
        }
        const Value header = parser.value();
        const Value* type = header.kind() == Value::Kind::Dict ? header.toDict().find("Type") : nullptr;
        if (type && type->text() == "XRef") {
            trailers.push_back(header.toDict());
        }
    }
    for (auto it = trailers.rbegin(); it != trailers.rend(); ++it) {
        for (const char* key: {"Root", "Info", "ID", "Encrypt"}) {
            if (const Value* value = it->find(key); value && !m_trailer.find(key)) {
                m_trailer.set(key, *value);
            }
        }
    }
    if (m_trailer.find("Encrypt") && !setUpEncryption()) {
        return false;
    }
    indexObjectStreams();
    // Without a trailer that names it, the catalog is the object that says it is one
    const auto isCatalog = [this](const Value* root) {
        const Value catalog = root ? resolve(*root) : Value();
        return catalog.kind() == Value::Kind::Dict && catalog.toDict().find("Pages");
    };
    if (!isCatalog(m_trailer.find("Root"))) {
        m_trailer.remove("Root");
        for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it) {
            const Value object = this->object(Ref{it.key(), 0});
            const Value* type = object.kind() == Value::Kind::Dict ? object.toDict().find("Type") : nullptr;
            if (type && type->text() == "Catalog" && object.toDict().find("Pages")) {
                m_trailer.set("Root", Value::ref(Ref{it.key(), 0}));
                break;
            }
        }
    }
    if (!m_trailer.find("Root")) {
        return fail(tr("The PDF file has no document catalog"));
    }
    if (pages().empty()) {
        return fail(tr("The PDF file has no pages"));
    }
    return true;
}

void Reader::indexObjectStreams() {
    // The objects in object streams, unless they are objects of their own too
    const QList<int> numbers = m_entries.keys();
    for (int container: numbers) {
        const Entry entry = m_entries.value(container);
        if (entry.type != 1) {
            continue;
        }
        Parser parser(m_data, entry.offset);
        qint64 number = 0;
        qint64 generation = 0;
        if (!parser.integer(number) || !parser.integer(generation) || !parser.keyword("obj")) {
            continue;
        }
        const Value header = parser.value();
        const Value* type = header.kind() == Value::Kind::Dict ? header.toDict().find("Type") : nullptr;
        if (!type || type->text() != "ObjStm") {
            continue;
        }
        Dict dict;
        QByteArray data;
        if (!streamAt(entry.offset, dict, data) || !dict.find("N")) {
            continue;
        }
        Parser list(data, 0);
        const int count = resolve(*dict.find("N")).toInt();
        for (int i = 0; i < count; ++i) {
            qint64 inner = 0;
            qint64 offset = 0;
            if (!list.integer(inner) || !list.integer(offset)) {
                break;
            }
            if (inner > 0 && !m_entries.contains(static_cast<int>(inner))) {
                m_entries.insert(static_cast<int>(inner), Entry{2, container, i});
                m_size = std::max(m_size, static_cast<int>(inner) + 1);
            }
        }
    }
}

qint64 Reader::streamLength(qint64 start, const Dict& dict) const {
    const Value* lengthValue = dict.find("Length");
    const qint64 length = lengthValue ? static_cast<qint64>(resolve(*lengthValue).toNumber()) : -1;
    if (length >= 0 && start + length <= m_data.size()) {
        // The data has to end there, with "endstream" after the end of the line
        qint64 end = start + length;
        while (end < m_data.size() && end < start + length + 4 && isSpace(m_data[end])) {
            ++end;
        }
        if (m_data.mid(end, 9) == "endstream") {
            return length;
        }
    }
    const qsizetype end = m_data.indexOf("endstream", start);
    if (end < 0) {
        return -1;
    }
    qint64 result = end - start;
    // Without the end of the line before it
    if (result > 0 && m_data[start + result - 1] == '\n') {
        --result;
    }
    if (result > 0 && m_data[start + result - 1] == '\r') {
        --result;
    }
    return result;
}

bool Reader::setUpEncryption() {
    const Value* encryptValue = m_trailer.find("Encrypt");
    if (encryptValue->kind() == Value::Kind::Ref) {
        m_encryptObject = encryptValue->toRef().number;
    }
    const Value encrypt = resolve(*encryptValue);
    if (encrypt.kind() != Value::Kind::Dict) {
        return fail(tr("The encryption of the PDF file is damaged"));
    }
    const Dict& dict = encrypt.toDict();
    auto get = [&](const Dict& from, const char* key) {
        const Value* value = from.find(key);
        return value ? resolve(*value) : Value();
    };
    if (get(dict, "Filter").text() != "Standard") {
        return fail(tr("The PDF file is encrypted in a way that is not understood"));
    }
    Crypt::Parameters parameters;
    parameters.v = get(dict, "V").toInt();
    parameters.r = get(dict, "R").toInt();
    const Value length = get(dict, "Length");
    parameters.length = length.kind() == Value::Kind::Number ? length.toInt() : parameters.v >= 4 ? 128 : 40;
    parameters.o = get(dict, "O").stringBytes();
    parameters.u = get(dict, "U").stringBytes();
    parameters.oe = get(dict, "OE").stringBytes();
    parameters.ue = get(dict, "UE").stringBytes();
    parameters.p = static_cast<qint32>(static_cast<qint64>(get(dict, "P").toNumber()));
    const Value metadata = get(dict, "EncryptMetadata");
    parameters.encryptMetadata = metadata.kind() != Value::Kind::Bool || metadata.toNumber() != 0;
    // The method of RC4 for the versions before crypt filters
    parameters.streamMethod = "V2";
    parameters.stringMethod = "V2";
    if (parameters.v >= 4) {
        const Value filters = get(dict, "CF");
        auto method = [&](const char* key) -> QByteArray {
            const QByteArray name = get(dict, key).text();
            if (name.isEmpty() || name == "Identity") {
                return "None";
            }
            const Value filter =
                    filters.kind() == Value::Kind::Dict ? get(filters.toDict(), name.constData()) : Value();
            const QByteArray cfm = filter.kind() == Value::Kind::Dict ? get(filter.toDict(), "CFM").text() : QByteArray();
            return cfm.isEmpty() ? QByteArray("None") : cfm;
        };
        parameters.streamMethod = method("StmF");
        parameters.stringMethod = method("StrF");
        for (const QByteArray& m: {parameters.streamMethod, parameters.stringMethod}) {
            if (m != "None" && m != "V2" && m != "AESV2" && m != "AESV3") {
                return fail(tr("The PDF file is encrypted in a way that is not understood"));
            }
        }
    }
    const Value id = get(m_trailer, "ID");
    if (!id.toArray().empty()) {
        parameters.id = resolve(id.toArray()[0]).stringBytes();
    }
    auto crypt = std::make_unique<Crypt>();
    if (!crypt->init(parameters)) {
        return fail(crypt->error());
    }
    m_crypt = std::move(crypt);
    // What was read before the key was known is read again
    m_cache.clear();
    m_objectStreams.clear();
    return true;
}

void Reader::decryptStrings(Value& value, int number, int generation) const {
    switch (value.kind()) {
        case Value::Kind::String:
            value = Value::string(m_crypt->decryptString(value.stringBytes(), number, generation));
            break;
        case Value::Kind::Array:
            for (Value& item: value.toArray()) {
                decryptStrings(item, number, generation);
            }
            break;
        case Value::Kind::Dict: {
            Dict decrypted;
            for (const auto& [key, item]: value.toDict().entries()) {
                Value copy = item;
                decryptStrings(copy, number, generation);
                decrypted.set(key, std::move(copy));
            }
            value = Value::dict(std::move(decrypted));
            break;
        }
        default:
            break;
    }
}

QByteArray Reader::decryptStreamData(const QByteArray& raw, const Dict& dict, int number, int generation) const {
    const Value* type = dict.find("Type");
    const QByteArray typeName = type ? resolve(*type).text() : QByteArray();
    if (!m_crypt || typeName == "XRef" || (typeName == "Metadata" && !m_crypt->encryptsMetadata())) {
        return raw;
    }
    // A crypt filter of its own, which is the Identity filter in all files that are understood here
    const Value* filter = dict.find("Filter");
    const Value filters = filter ? resolve(*filter) : Value();
    if (filters.text() == "Crypt" || (!filters.toArray().empty() && resolve(filters.toArray()[0]).text() == "Crypt")) {
        return raw;
    }
    return m_crypt->decryptStream(raw, number, generation);
}

bool Reader::readXref(qint64 offset, int depth) {
    if (depth > MAX_DEPTH || offset < 0 || offset >= m_data.size()) {
        return false;
    }
    Parser probe(m_data, offset);
    return probe.keyword("xref") ? readXrefTable(offset, depth) : readXrefStream(offset, depth);
}

bool Reader::readXrefTable(qint64 offset, int depth) {
    Parser parser(m_data, offset);
    parser.keyword("xref");
    while (true) {
        qint64 first = 0;
        qint64 count = 0;
        if (!parser.integer(first) || !parser.integer(count)) {
            break;
        }
        for (qint64 i = 0; i < count; ++i) {
            qint64 position = 0;
            qint64 generation = 0;
            if (!parser.integer(position) || !parser.integer(generation)) {
                return false;
            }
            const int number = static_cast<int>(first + i);
            m_size = std::max(m_size, number + 1);
            if (parser.keyword("n")) {
                if (!m_entries.contains(number)) {
                    m_entries.insert(number, Entry{1, position, static_cast<int>(generation)});
                }
            } else if (!parser.keyword("f")) {
                return false;
            }
        }
    }
    if (!parser.keyword("trailer")) {
        return false;
    }
    const Value trailer = parser.value();
    if (parser.failed() || trailer.kind() != Value::Kind::Dict) {
        return false;
    }
    for (const auto& entry: trailer.toDict().entries()) {
        if (!m_trailer.find(entry.first)) {
            m_trailer.set(entry.first, entry.second);
        }
    }
    // Objects in object streams of a file that has both kinds of tables
    if (const Value* stream = trailer.toDict().find("XRefStm")) {
        readXrefStream(static_cast<qint64>(stream->toNumber()), depth + 1);
    }
    if (const Value* previous = trailer.toDict().find("Prev")) {
        return readXref(static_cast<qint64>(previous->toNumber()), depth + 1);
    }
    return true;
}

bool Reader::readXrefStream(qint64 offset, int depth) {
    Dict dict;
    QByteArray data;
    if (!streamAt(offset, dict, data)) {
        return false;
    }
    const Value* widths = dict.find("W");
    const Value* size = dict.find("Size");
    if (!widths || widths->toArray().size() != 3 || !size) {
        return false;
    }
    const int w0 = widths->toArray()[0].toInt();
    const int w1 = widths->toArray()[1].toInt();
    const int w2 = widths->toArray()[2].toInt();
    const int width = w0 + w1 + w2;
    if (w0 < 0 || w1 < 0 || w2 < 0 || width <= 0) {
        return false;
    }
    Array index;
    if (const Value* given = dict.find("Index")) {
        index = given->toArray();
    } else {
        index = {Value::number(0), *size};
    }
    auto field = [&](qsizetype pos, int bytes) {
        qint64 value = 0;
        for (int i = 0; i < bytes; ++i) {
            value = (value << 8) | static_cast<uchar>(data[pos + i]);
        }
        return value;
    };
    qsizetype pos = 0;
    for (size_t i = 0; i + 1 < index.size(); i += 2) {
        const int first = index[i].toInt();
        const int count = index[i + 1].toInt();
        for (int k = 0; k < count; ++k, pos += width) {
            if (pos + width > data.size()) {
                return false;
            }
            const int number = first + k;
            m_size = std::max(m_size, number + 1);
            const qint64 type = w0 > 0 ? field(pos, w0) : 1;
            const qint64 second = field(pos + w0, w1);
            const qint64 third = field(pos + w0 + w1, w2);
            if ((type == 1 || type == 2) && !m_entries.contains(number)) {
                m_entries.insert(number, Entry{static_cast<int>(type), second, static_cast<int>(third)});
            }
        }
    }
    for (const auto& entry: dict.entries()) {
        if (!m_trailer.find(entry.first)) {
            m_trailer.set(entry.first, entry.second);
        }
    }
    if (const Value* previous = dict.find("Prev")) {
        return readXref(static_cast<qint64>(previous->toNumber()), depth + 1);
    }
    return true;
}

bool Reader::streamAt(qint64 offset, Dict& dict, QByteArray& decoded) const {
    Parser parser(m_data, offset);
    qint64 number = 0;
    qint64 generation = 0;
    if (!parser.integer(number) || !parser.integer(generation) || !parser.keyword("obj")) {
        return false;
    }
    const Value header = parser.value();
    if (parser.failed() || header.kind() != Value::Kind::Dict || !parser.keyword("stream")) {
        return false;
    }
    dict = header.toDict();
    // The data starts after the end of the line
    qint64 start = parser.pos();
    if (start < m_data.size() && m_data[start] == '\r') {
        ++start;
    }
    if (start < m_data.size() && m_data[start] == '\n') {
        ++start;
    }
    const qint64 length = streamLength(start, dict);
    if (length < 0 || start + length > m_data.size()) {
        return false;
    }
    // Object streams are encrypted, cross-reference streams are not
    return decodeStream(
            decryptStreamData(m_data.mid(start, length), dict, static_cast<int>(number), static_cast<int>(generation)),
            dict, decoded, nullptr);
}

bool Reader::decodeStream(const QByteArray& raw, const Dict& dict, QByteArray& decoded, QByteArray* imageFilter) const {
    // The filters in the order they are undone, each with its parameters
    Value filters = dict.find("Filter") ? resolve(*dict.find("Filter")) : Value();
    Value parms = dict.find("DecodeParms") ? resolve(*dict.find("DecodeParms")) : Value();
    Array filterList;
    Array parmsList;
    if (filters.kind() == Value::Kind::Array) {
        filterList = filters.toArray();
        if (parms.kind() == Value::Kind::Array) {
            parmsList = parms.toArray();
        }
    } else if (!filters.isNull()) {
        filterList.push_back(filters);
        parmsList.push_back(parms);
    }
    decoded = raw;
    for (size_t i = 0; i < filterList.size(); ++i) {
        const QByteArray name = resolve(filterList[i]).text();
        const Value parm = i < parmsList.size() ? resolve(parmsList[i]) : Value();
        QByteArray out;
        if (name == "Crypt") {
            continue;  // the Identity crypt filter: the data is as it is
        }
        if (name == "FlateDecode" || name == "Fl") {
            if (!inflateData(decoded, out)) {
                return false;
            }
            if (parm.kind() == Value::Kind::Dict) {
                auto number = [&](const char* key, int fallback) {
                    const Value* value = parm.toDict().find(key);
                    return value ? resolve(*value).toInt() : fallback;
                };
                if (!undoPredictor(out, number("Predictor", 1), number("Columns", 1), number("Colors", 1),
                                   number("BitsPerComponent", 8))) {
                    return false;
                }
            }
        } else if (name == "ASCIIHexDecode" || name == "AHx") {
            asciiHexDecode(decoded, out);
        } else if (name == "ASCII85Decode" || name == "A85") {
            ascii85Decode(decoded, out);
        } else if (imageFilter && i + 1 == filterList.size()) {
            // An image format (DCTDecode, JPXDecode, ...): the data is the image file
            *imageFilter = name;
            return true;
        } else {
            return false;
        }
        decoded = std::move(out);
    }
    return true;
}

bool Reader::rawStream(Ref ref, Dict& dict, QByteArray& raw) const {
    const auto entry = m_entries.constFind(ref.number);
    if (entry == m_entries.constEnd() || entry->type != 1) {
        return false;  // streams are never in object streams
    }
    Parser parser(m_data, entry->offset);
    qint64 number = 0;
    qint64 generation = 0;
    if (!parser.integer(number) || !parser.integer(generation) || !parser.keyword("obj")) {
        return false;
    }
    const Value header = parser.value();
    if (parser.failed() || header.kind() != Value::Kind::Dict || !parser.keyword("stream")) {
        return false;
    }
    Value decrypted = header;
    if (m_crypt) {
        decryptStrings(decrypted, static_cast<int>(number), static_cast<int>(generation));
    }
    dict = decrypted.toDict();
    qint64 start = parser.pos();
    if (start < m_data.size() && m_data[start] == '\r') {
        ++start;
    }
    if (start < m_data.size() && m_data[start] == '\n') {
        ++start;
    }
    const qint64 length = streamLength(start, dict);
    if (length < 0 || start + length > m_data.size()) {
        return false;
    }
    raw = decryptStreamData(m_data.mid(start, length), dict, static_cast<int>(number), static_cast<int>(generation));
    return true;
}

bool Reader::stream(Ref ref, Dict& dict, QByteArray& data, QByteArray* imageFilter) const {
    QByteArray raw;
    if (!rawStream(ref, dict, raw)) {
        return false;
    }
    if (imageFilter) {
        imageFilter->clear();
    }
    return decodeStream(raw, dict, data, imageFilter);
}

Value Reader::object(Ref ref) const {
    const auto cached = m_cache.constFind(ref.number);
    if (cached != m_cache.constEnd()) {
        return *cached;
    }
    const auto entry = m_entries.constFind(ref.number);
    if (entry == m_entries.constEnd()) {
        return {};
    }
    Value result;
    if (entry->type == 1) {
        Parser parser(m_data, entry->offset);
        qint64 number = 0;
        qint64 generation = 0;
        if (parser.integer(number) && parser.integer(generation) && parser.keyword("obj") && number == ref.number) {
            result = parser.value();
            if (parser.failed()) {
                result = Value();
            } else if (m_crypt && ref.number != m_encryptObject) {
                // Strings of objects in object streams are not encrypted of their own: the stream is
                decryptStrings(result, ref.number, static_cast<int>(generation));
            }
        }
    } else {
        // In an object stream: numbers and offsets first, then the objects
        const int container = static_cast<int>(entry->offset);
        if (!m_objectStreams.contains(container)) {
            std::vector<std::pair<int, Value>> objects;
            const auto containerEntry = m_entries.constFind(container);
            Dict dict;
            QByteArray data;
            if (containerEntry != m_entries.constEnd() && containerEntry->type == 1 &&
                streamAt(containerEntry->offset, dict, data) && dict.find("N") && dict.find("First")) {
                const int count = dict.find("N")->toInt();
                const qint64 first = static_cast<qint64>(dict.find("First")->toNumber());
                Parser header(data, 0);
                for (int i = 0; i < count; ++i) {
                    qint64 number = 0;
                    qint64 offset = 0;
                    if (!header.integer(number) || !header.integer(offset)) {
                        break;
                    }
                    Parser parser(data, first + offset);
                    Value value = parser.value();
                    objects.emplace_back(static_cast<int>(number), parser.failed() ? Value() : std::move(value));
                }
            }
            m_objectStreams.insert(container, std::move(objects));
        }
        for (const auto& object: m_objectStreams.value(container)) {
            if (object.first == ref.number) {
                result = object.second;
                break;
            }
        }
    }
    m_cache.insert(ref.number, result);
    return result;
}

Value Reader::resolve(const Value& value) const {
    Value current = value;
    for (int i = 0; i < MAX_DEPTH && current.kind() == Value::Kind::Ref; ++i) {
        current = object(current.toRef());
    }
    return current.kind() == Value::Kind::Ref ? Value() : current;
}

std::optional<Ref> Reader::pagesRoot() const {
    const Value* root = m_trailer.find("Root");
    if (!root) {
        return std::nullopt;
    }
    const Value catalog = resolve(*root);
    const Value* pages = catalog.kind() == Value::Kind::Dict ? catalog.toDict().find("Pages") : nullptr;
    if (!pages || pages->kind() != Value::Kind::Ref) {
        return std::nullopt;
    }
    return pages->toRef();
}

void Reader::collectPages(const Value& node, Dict inherited, std::vector<PageInfo>& pages, int depth) const {
    static const char* const INHERITED[] = {"Resources", "MediaBox", "CropBox", "Rotate"};
    const Value value = resolve(node);
    if (value.kind() != Value::Kind::Dict || depth > MAX_DEPTH || pages.size() > 100000) {
        return;
    }
    const Dict& dict = value.toDict();
    const Value* kids = dict.find("Kids");
    if (kids) {
        for (const char* key: INHERITED) {
            if (const Value* own = dict.find(key)) {
                inherited.set(key, *own);
            }
        }
        const Value list = resolve(*kids);
        for (const Value& kid: list.toArray()) {
            collectPages(kid, inherited, pages, depth + 1);
        }
        return;
    }
    if (node.kind() != Value::Kind::Ref) {
        return;  // a page is always an object of its own
    }
    PageInfo page{node.toRef(), dict};
    for (const auto& entry: inherited.entries()) {
        if (!page.dict.find(entry.first)) {
            page.dict.set(entry.first, entry.second);
        }
    }
    pages.push_back(std::move(page));
}

std::vector<PageInfo> Reader::pages() const {
    std::vector<PageInfo> pages;
    if (const auto root = pagesRoot()) {
        collectPages(Value::ref(*root), Dict(), pages, 0);
    }
    return pages;
}

// Update

Update::Update(const Reader& reader): m_reader(reader), m_next(reader.size()) {
    m_out = "\n";  // the file may not end with a line break
}

Ref Update::newRef() { return {m_next++, 0}; }

namespace {

/// The strings of an object as an encrypted file has them
Value encryptedStrings(const Value& value, const Crypt& crypt, Ref ref) {
    switch (value.kind()) {
        case Value::Kind::String:
            return Value::string(crypt.encryptString(value.stringBytes(), ref.number, ref.generation));
        case Value::Kind::Array: {
            Array array;
            for (const Value& item: value.toArray()) {
                array.push_back(encryptedStrings(item, crypt, ref));
            }
            return Value::array(std::move(array));
        }
        case Value::Kind::Dict: {
            Dict dict;
            for (const auto& [key, item]: value.toDict().entries()) {
                dict.set(key, encryptedStrings(item, crypt, ref));
            }
            return Value::dict(std::move(dict));
        }
        default:
            return value;
    }
}

}  // namespace

void Update::add(Ref ref, const Value& value) {
    const Value stored = m_reader.crypt() ? encryptedStrings(value, *m_reader.crypt(), ref) : value;
    m_offsets.push_back({ref.number, ref.generation, m_reader.data().size() + m_out.size()});
    m_out += QByteArray::number(ref.number) + ' ' + QByteArray::number(ref.generation) + " obj\n" + stored.serialize() +
             "\nendobj\n";
}

void Update::addStream(Ref ref, Dict dict, const QByteArray& data, bool compress) {
    QByteArray stored = data;
    if (compress) {
        stored = qCompress(data, 9).mid(4);  // without the length Qt puts in front of the zlib stream
        dict.set("Filter", Value::name("FlateDecode"));
    }
    if (const Crypt* crypt = m_reader.crypt()) {
        // In an encrypted file, what is appended is encrypted with its key
        stored = crypt->encryptStream(stored, ref.number, ref.generation);
        dict = encryptedStrings(Value::dict(dict), *crypt, ref).toDict();
    }
    dict.set("Length", Value::number(static_cast<double>(stored.size())));
    m_offsets.push_back({ref.number, ref.generation, m_reader.data().size() + m_out.size()});
    m_out += QByteArray::number(ref.number) + ' ' + QByteArray::number(ref.generation) + " obj\n" +
             Value::dict(dict).serialize() + "\nstream\n" + stored + "\nendstream\nendobj\n";
}

QByteArray Update::finish() {
    // What the trailer keeps from the file
    Dict trailer;
    for (const char* key: {"Root", "Info", "ID", "Encrypt"}) {
        if (const Value* value = m_reader.trailer().find(key)) {
            trailer.set(key, *value);
        }
    }
    // The table of a repaired file is not used again: the new one has all objects, in a stream, which can say where
    // objects in object streams are
    const bool repaired = m_reader.repaired();
    if (repaired) {
        std::vector<Written> old;
        for (auto it = m_reader.m_entries.constBegin(); it != m_reader.m_entries.constEnd(); ++it) {
            old.push_back({it.key(), it->index, it->offset, it->type});
        }
        m_offsets.insert(m_offsets.begin(), old.begin(), old.end());
    } else {
        trailer.set("Prev", Value::number(static_cast<double>(m_reader.xrefOffset())));
    }

    const qint64 base = m_reader.data().size();
    const qint64 xrefOffset = base + m_out.size();
    Ref xrefRef;
    if (m_reader.usesXrefStream() || repaired) {
        // A file with cross-reference streams is updated with one; this one is not compressed
        xrefRef = newRef();
        m_offsets.push_back({xrefRef.number, 0, xrefOffset});
    }
    // In the order of the numbers; the last version of an object that was written twice counts
    std::stable_sort(m_offsets.begin(), m_offsets.end(),
                     [](const Written& a, const Written& b) { return a.number < b.number; });
    std::vector<Written> entries;
    for (const Written& entry: m_offsets) {
        if (!entries.empty() && entries.back().number == entry.number) {
            entries.back() = entry;
        } else {
            entries.push_back(entry);
        }
    }
    trailer.set("Size", Value::number(m_next));

    if (m_reader.usesXrefStream() || repaired) {
        Array index;
        QByteArray data;
        for (size_t i = 0; i < entries.size();) {
            size_t end = i + 1;
            while (end < entries.size() && entries[end].number == entries[end - 1].number + 1) {
                ++end;
            }
            index.push_back(Value::number(entries[i].number));
            index.push_back(Value::number(static_cast<double>(end - i)));
            for (; i < end; ++i) {
                const qint64 offset = entries[i].offset;
                data.append(static_cast<char>(entries[i].type));
                for (int shift = 24; shift >= 0; shift -= 8) {
                    data.append(static_cast<char>((offset >> shift) & 0xff));
                }
                const int generation = entries[i].generation;
                data.append(static_cast<char>((generation >> 8) & 0xff));
                data.append(static_cast<char>(generation & 0xff));
            }
        }
        trailer.set("Type", Value::name("XRef"));
        trailer.set("W", Value::array({Value::number(1), Value::number(4), Value::number(2)}));
        trailer.set("Index", Value::array(std::move(index)));
        trailer.set("Length", Value::number(static_cast<double>(data.size())));
        m_out += QByteArray::number(xrefRef.number) + " 0 obj\n" + Value::dict(trailer).serialize() + "\nstream\n" +
                 data + "\nendstream\nendobj\n";
    } else {
        m_out += "xref\n";
        for (size_t i = 0; i < entries.size();) {
            size_t end = i + 1;
            while (end < entries.size() && entries[end].number == entries[end - 1].number + 1) {
                ++end;
            }
            m_out += QByteArray::number(entries[i].number) + ' ' + QByteArray::number(static_cast<qint64>(end - i)) +
                     '\n';
            for (; i < end; ++i) {
                m_out += QByteArray::number(entries[i].offset).rightJustified(10, '0') + ' ' +
                         QByteArray::number(entries[i].generation).rightJustified(5, '0') + " n \n";
            }
        }
        m_out += "trailer\n" + Value::dict(trailer).serialize() + '\n';
    }
    m_out += "startxref\n" + QByteArray::number(xrefOffset) + "\n%%EOF\n";
    return m_reader.data() + m_out;
}

}  // namespace Pdf
