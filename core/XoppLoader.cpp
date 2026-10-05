#include "XoppLoader.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

#include <zlib.h>

using namespace Qt::StringLiterals;

namespace {

bool gunzip(const QByteArray& in, QByteArray& out) {
    z_stream zs{};
    if (inflateInit2(&zs, 15 + 32) != Z_OK) {  // +32: detect gzip or zlib header
        return false;
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    char buffer[1 << 16];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buffer);
        zs.avail_out = sizeof(buffer);
        ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            inflateEnd(&zs);
            return false;
        }
        out.append(buffer, static_cast<qsizetype>(sizeof(buffer) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    inflateEnd(&zs);
    return true;
}

QList<double> parseDoubles(QStringView s) {
    QList<double> values;
    qsizetype i = 0;
    const qsizetype n = s.size();
    while (i < n) {
        while (i < n && s[i].isSpace()) {
            ++i;
        }
        const qsizetype start = i;
        while (i < n && !s[i].isSpace()) {
            ++i;
        }
        if (i > start) {
            values.append(s.mid(start, i - start).toDouble());
        }
    }
    return values;
}

std::optional<Matrix> parseMatrix(QStringView s) {
    const QList<double> v = parseDoubles(s);
    if (v.size() != 6) {
        return std::nullopt;
    }
    return Matrix{v[0], v[1], v[2], v[3], v[4], v[5]};
}

QColor parseColor(QStringView s, bool background) {
    if (s.size() == 9 && s.startsWith(u'#')) {
        bool ok = false;
        const uint v = s.mid(1).toUInt(&ok, 16);
        if (ok) {
            return QColor((v >> 24) & 0xff, (v >> 16) & 0xff, (v >> 8) & 0xff, v & 0xff);
        }
    }

    // Color names of the legacy Xournal format
    static const QHash<QString, QColor> penColors{
            {u"black"_s, QColor(0x00, 0x00, 0x00)},      {u"blue"_s, QColor(0x33, 0x33, 0xcc)},
            {u"red"_s, QColor(0xff, 0x00, 0x00)},        {u"green"_s, QColor(0x00, 0x80, 0x00)},
            {u"gray"_s, QColor(0x80, 0x80, 0x80)},       {u"lightblue"_s, QColor(0x00, 0xc0, 0xff)},
            {u"lightgreen"_s, QColor(0x00, 0xff, 0x00)}, {u"magenta"_s, QColor(0xff, 0x00, 0xff)},
            {u"orange"_s, QColor(0xff, 0x80, 0x00)},     {u"yellow"_s, QColor(0xff, 0xff, 0x00)},
            {u"white"_s, QColor(0xff, 0xff, 0xff)}};
    static const QHash<QString, QColor> backgroundColors{
            {u"blue"_s, QColor(0xa0, 0xe8, 0xff)},   {u"pink"_s, QColor(0xff, 0xc0, 0xd4)},
            {u"green"_s, QColor(0x80, 0xff, 0xc0)},  {u"orange"_s, QColor(0xff, 0xc0, 0x80)},
            {u"yellow"_s, QColor(0xff, 0xff, 0x80)}, {u"white"_s, QColor(0xff, 0xff, 0xff)}};

    const QString name = s.toString();
    const auto& table = background ? backgroundColors : penColors;
    if (auto it = table.constFind(name); it != table.constEnd()) {
        return *it;
    }
    const QColor c = QColor::fromString(name);
    if (c.isValid()) {
        return c;
    }
    return background ? QColor(Qt::white) : QColor(Qt::black);
}

AudioRef parseAudio(const QXmlStreamAttributes& a) {
    AudioRef audio;
    audio.filename = a.value("fn"_L1).toString();
    audio.timestamp = a.value("ts"_L1).toLongLong();
    return audio;
}

QString resolvePath(const QString& xoppPath, QStringView domain, const QString& filename) {
    if (domain == u"attach") {
        return xoppPath + u'.' + filename;
    }
    const QFileInfo info(filename);
    if (info.isAbsolute() && info.exists()) {
        return filename;
    }
    // The document may have been moved: look next to the .xopp file
    const QDir dir = QFileInfo(xoppPath).dir();
    if (info.isRelative() && dir.exists(filename)) {
        return dir.filePath(filename);
    }
    if (dir.exists(info.fileName())) {
        return dir.filePath(info.fileName());
    }
    return filename;
}

void parseBackground(const QXmlStreamAttributes& a, const QString& xoppPath, Document& doc, Page& page) {
    Background& bg = page.background;
    if (a.hasAttribute("name"_L1)) {
        bg.name = a.value("name"_L1).toString();
    }

    const auto type = a.value("type"_L1);
    if (type == u"pdf") {
        bg.type = Background::Type::Pdf;
        bg.pdfPage = a.value("pageno"_L1).toInt() - 1;
        // Only the first PDF page names the file
        if (a.hasAttribute("filename"_L1) && doc.pdfFilename.isEmpty()) {
            doc.pdfDomain = a.value("domain"_L1).toString();
            doc.pdfFilename = a.value("filename"_L1).toString();
            doc.pdfPath = resolvePath(xoppPath, doc.pdfDomain, doc.pdfFilename);
        }
    } else if (type == u"pixmap") {
        bg.type = Background::Type::Pixmap;
        bg.domain = a.value("domain"_L1).toString();
        bg.filename = a.value("filename"_L1).toString();
        if (bg.domain == u"clone") {
            // The image of another page, given by its index
            bool ok = false;
            const size_t source = bg.filename.toUInt(&ok);
            if (ok && source < doc.pages.size()) {
                bg.pixmap = doc.pages[source].background.pixmap;
            }
        } else {
            bg.pixmap.load(resolvePath(xoppPath, bg.domain, bg.filename));
        }
    } else {
        bg.type = Background::Type::Solid;
        bg.color = parseColor(a.value("color"_L1), true);
        bg.style = a.value("style"_L1).toString();
        bg.config = a.value("config"_L1).toString();
    }
}

Stroke parseStroke(const QXmlStreamAttributes& a, const QString& content) {
    Stroke s;
    const auto tool = a.value("tool"_L1);
    if (tool == u"highlighter") {
        s.tool = Stroke::Tool::Highlighter;
    } else if (tool == u"eraser") {
        s.tool = Stroke::Tool::Eraser;
    }
    s.color = parseColor(a.value("color"_L1), false);
    if (a.hasAttribute("fill"_L1)) {
        s.fill = a.value("fill"_L1).toInt();
    }

    const auto cap = a.value("capStyle"_L1);
    if (cap == u"butt") {
        s.cap = Qt::FlatCap;
    } else if (cap == u"square") {
        s.cap = Qt::SquareCap;
    }
    s.setStyle(a.value("style"_L1).toString());
    s.audio = parseAudio(a);

    const QList<double> coords = parseDoubles(content);
    s.points.reserve(coords.size() / 2);
    for (qsizetype i = 0; i + 1 < coords.size(); i += 2) {
        s.points.append(QPointF(coords[i], coords[i + 1]));
    }

    // "width" holds the nominal width, optionally followed by one width per segment
    QList<double> widths = parseDoubles(a.value("width"_L1));
    if (!widths.isEmpty()) {
        s.width = widths.takeFirst();
        if (s.points.size() > 1 && widths.size() >= s.points.size() - 1) {
            widths.resize(s.points.size() - 1);
            s.widths = widths;
        }
    }
    s.updateBounds();
    return s;
}

TextElement parseText(const QXmlStreamAttributes& a, const QString& content) {
    TextElement t;
    t.text = content;
    t.font = a.value("font"_L1).toString();
    t.size = a.value("size"_L1).toDouble();
    t.color = parseColor(a.value("color"_L1), false);
    if (a.hasAttribute("matrix"_L1)) {
        t.matrix = parseMatrix(a.value("matrix"_L1));
    }
    if (t.matrix) {
        t.pos = QPointF((*t.matrix)[4], (*t.matrix)[5]);
    } else {
        t.pos = QPointF(a.value("x"_L1).toDouble(), a.value("y"_L1).toDouble());
    }
    if (a.hasAttribute("wrap"_L1)) {
        t.wrap = a.value("wrap"_L1).toDouble();
    }
    t.align = a.value("align"_L1).toString();
    t.justify = a.value("justify"_L1) == u"true";
    t.audio = parseAudio(a);
    return t;
}

LinkElement parseLink(const QXmlStreamAttributes& a, const QString& content) {
    LinkElement l;
    l.text = content;
    l.url = a.value("url"_L1).toString();
    l.font = a.value("font"_L1).toString();
    l.size = a.value("size"_L1).toDouble();
    l.color = parseColor(a.value("color"_L1), false);
    l.align = a.value("align"_L1).toString();
    if (const auto matrix = parseMatrix(a.value("matrix"_L1))) {
        l.matrix = *matrix;
    } else {
        l.matrix = {1, 0, 0, 1, a.value("x"_L1).toDouble(), a.value("y"_L1).toDouble()};
    }
    return l;
}

/// Decodes an embedded image. LaTeX formulas are stored as PDF.
ImageElement parseImage(const QXmlStreamAttributes& a, const QString& content, bool tex) {
    ImageElement img;
    img.tex = tex;
    img.texSource = a.value("text"_L1).toString();
    img.data = QByteArray::fromBase64(content.toLatin1());

    const QList<double> natural = parseDoubles(a.value("natural_size"_L1));
    if (natural.size() == 2) {
        img.naturalSize = QSizeF(natural[0], natural[1]);
    }
    decodeImageData(img);

    if (a.hasAttribute("matrix"_L1)) {
        img.matrix = parseMatrix(a.value("matrix"_L1));
    }
    if (img.matrix) {
        img.rect = toTransform(*img.matrix).mapRect(QRectF(QPointF(0, 0), img.naturalSize));
    } else {
        img.rect = QRectF(QPointF(a.value("left"_L1).toDouble(), a.value("top"_L1).toDouble()),
                          QPointF(a.value("right"_L1).toDouble(), a.value("bottom"_L1).toDouble()));
    }
    return img;
}

}  // namespace

void decodeImageData(ImageElement& img) {
    if (img.data.startsWith("%PDF")) {
#ifdef HAVE_QTPDF
        constexpr double RENDER_SCALE = 4.0;
        QBuffer buffer(&img.data);
        buffer.open(QIODevice::ReadOnly);
        QPdfDocument pdf;
        pdf.load(&buffer);
        if (pdf.status() == QPdfDocument::Status::Ready && pdf.pageCount() > 0) {
            const QSizeF size = pdf.pagePointSize(0);
            img.image = pdf.render(0, (size * RENDER_SCALE).toSize());
            if (!img.naturalSize.isValid()) {
                img.naturalSize = size;
            }
        }
#endif
        return;
    }
    img.image = QImage::fromData(img.data);
    if (!img.naturalSize.isValid() && !img.image.isNull()) {
        img.naturalSize = img.image.size();
    }
}

bool loadXopp(const QString& path, Document& doc, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = QCoreApplication::translate("XoppLoader", "Could not open \"%1\": %2")
                             .arg(path, file.errorString());
        }
        return false;
    }
    return loadXoppData(file.readAll(), path, doc, error);
}

bool loadXoppData(const QByteArray& raw, const QString& path, Document& doc, QString* error) {
    auto fail = [error](const QString& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    QByteArray xmlData;
    const bool gzipped = raw.size() > 2 && static_cast<uchar>(raw[0]) == 0x1f && static_cast<uchar>(raw[1]) == 0x8b;
    if (!gzipped) {
        xmlData = raw;
    } else if (!gunzip(raw, xmlData)) {
        return fail(QCoreApplication::translate("XoppLoader", "\"%1\" is damaged: decompression failed").arg(path));
    }

    doc = Document();
    doc.sourcePath = path;
    Page* page = nullptr;
    Layer* layer = nullptr;

    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement) {
            continue;
        }
        const auto name = xml.name();
        const QXmlStreamAttributes a = xml.attributes();

        if (name == u"page") {
            doc.pages.emplace_back();
            page = &doc.pages.back();
            layer = nullptr;
            page->width = a.value("width"_L1).toDouble();
            page->height = a.value("height"_L1).toDouble();
        } else if (name == u"background" && page) {
            parseBackground(a, path, doc, *page);
        } else if (name == u"layer" && page) {
            page->layers.emplace_back();
            layer = &page->layers.back();
            if (a.hasAttribute("name"_L1)) {
                layer->name = a.value("name"_L1).toString();
            }
        } else if (layer) {
            if (name == u"stroke") {
                Stroke s = parseStroke(a, xml.readElementText());
                if (!s.points.isEmpty()) {
                    layer->elements.emplace_back(std::move(s));
                }
            } else if (name == u"text") {
                layer->elements.emplace_back(parseText(a, xml.readElementText()));
            } else if (name == u"image" || name == u"teximage") {
                layer->elements.emplace_back(parseImage(a, xml.readElementText(), name == u"teximage"));
            } else if (name == u"link") {
                layer->elements.emplace_back(parseLink(a, xml.readElementText()));
            }
        }
    }

    if (xml.hasError()) {
        return fail(QCoreApplication::translate("XoppLoader", "\"%1\" is not a valid Xournal++ file: %2 (line %3)")
                            .arg(path, xml.errorString())
                            .arg(xml.lineNumber()));
    }
    if (doc.pages.empty()) {
        return fail(QCoreApplication::translate("XoppLoader", "\"%1\" contains no pages").arg(path));
    }
    return true;
}
