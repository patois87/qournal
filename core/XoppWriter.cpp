#include "XoppWriter.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QPainter>
#include <QSaveFile>
#include <QXmlStreamWriter>
#include <algorithm>

#include <zlib.h>

#include "Renderer.h"
#include "XoppCompat.h"

using namespace Qt::StringLiterals;

namespace {

constexpr int PREVIEW_SIZE = 128;

QString num(double v) { return QString::number(v, 'g', 8); }

QString nums(const QList<double>& values) {
    QStringList parts;
    parts.reserve(values.size());
    for (double v: values) {
        parts.append(num(v));
    }
    return parts.join(u' ');
}

QString matrixString(const Matrix& m) { return nums({m[0], m[1], m[2], m[3], m[4], m[5]}); }

QString colorString(const QColor& c) {
    return u"#%1%2%3%4"_s.arg(c.red(), 2, 16, QLatin1Char('0'))
            .arg(c.green(), 2, 16, QLatin1Char('0'))
            .arg(c.blue(), 2, 16, QLatin1Char('0'))
            .arg(c.alpha(), 2, 16, QLatin1Char('0'));
}

void writeAudio(QXmlStreamWriter& xml, const AudioRef& audio) {
    if (!audio.filename.isEmpty()) {
        xml.writeAttribute("ts"_L1, QString::number(audio.timestamp));
        xml.writeAttribute("fn"_L1, audio.filename);
    }
}

void writeStroke(QXmlStreamWriter& xml, const Stroke& s) {
    xml.writeStartElement("stroke"_L1);
    switch (s.tool) {
        case Stroke::Tool::Pen:
            xml.writeAttribute("tool"_L1, "pen"_L1);
            writeAudio(xml, s.audio);
            break;
        case Stroke::Tool::Highlighter:
            xml.writeAttribute("tool"_L1, "highlighter"_L1);
            break;
        case Stroke::Tool::Eraser:
            xml.writeAttribute("tool"_L1, "eraser"_L1);
            break;
    }
    xml.writeAttribute("color"_L1, colorString(s.color));

    QString width = num(s.width);
    if (s.hasPressure()) {
        width += u' ' + nums(s.widths);
    }
    xml.writeAttribute("width"_L1, width);

    if (s.fill != -1) {
        xml.writeAttribute("fill"_L1, QString::number(s.fill));
    }
    xml.writeAttribute("capStyle"_L1, s.cap == Qt::FlatCap   ? "butt"_L1 :
                                      s.cap == Qt::SquareCap ? "square"_L1 :
                                                               "round"_L1);
    if (!s.style.isEmpty()) {
        xml.writeAttribute("style"_L1, s.style);
    }

    QStringList coords;
    coords.reserve(s.points.size() * 2);
    for (const QPointF& p: s.points) {
        coords.append(num(p.x()));
        coords.append(num(p.y()));
    }
    xml.writeCharacters(coords.join(u' '));
    xml.writeEndElement();
}

void writeText(QXmlStreamWriter& xml, const TextElement& t) {
    if (t.text.isEmpty()) {
        return;  // Xournal++ discards empty texts as well
    }
    xml.writeStartElement("text"_L1);
    xml.writeAttribute("font"_L1, t.font);
    xml.writeAttribute("size"_L1, num(t.size));
    if (t.matrix) {
        xml.writeAttribute("matrix"_L1, matrixString(*t.matrix));
    } else {
        xml.writeAttribute("x"_L1, num(t.pos.x()));
        xml.writeAttribute("y"_L1, num(t.pos.y()));
    }
    xml.writeAttribute("color"_L1, colorString(t.color));
    if (t.wrap >= 0) {
        xml.writeAttribute("wrap"_L1, num(t.wrap));
    }
    if (!t.align.isEmpty() && t.align != u"left") {
        xml.writeAttribute("align"_L1, t.align);
    }
    if (t.justify) {
        xml.writeAttribute("justify"_L1, "true"_L1);
    }
    writeAudio(xml, t.audio);
    xml.writeCharacters(t.text);
    xml.writeEndElement();
}

void writeImage(QXmlStreamWriter& xml, const ImageElement& img) {
    xml.writeStartElement(img.tex ? "teximage"_L1 : "image"_L1);
    if (img.tex) {
        xml.writeAttribute("text"_L1, img.texSource);
    }
    if (img.matrix) {
        xml.writeAttribute("matrix"_L1, matrixString(*img.matrix));
    } else {
        xml.writeAttribute("left"_L1, num(img.rect.left()));
        xml.writeAttribute("top"_L1, num(img.rect.top()));
        xml.writeAttribute("right"_L1, num(img.rect.right()));
        xml.writeAttribute("bottom"_L1, num(img.rect.bottom()));
        if (img.naturalSize.isValid()) {
            xml.writeAttribute("natural_size"_L1, nums({img.naturalSize.width(), img.naturalSize.height()}));
        }
    }
    xml.writeCharacters(QString::fromLatin1(img.data.toBase64()));
    xml.writeEndElement();
}

void writeLink(QXmlStreamWriter& xml, const LinkElement& l) {
    xml.writeStartElement("link"_L1);
    xml.writeAttribute("align"_L1, l.align.isEmpty() ? u"left"_s : l.align);
    xml.writeAttribute("font"_L1, l.font);
    xml.writeAttribute("size"_L1, num(l.size));
    xml.writeAttribute("matrix"_L1, matrixString(l.matrix));
    xml.writeAttribute("color"_L1, colorString(l.color));
    xml.writeAttribute("url"_L1, l.url);
    xml.writeCharacters(l.text);
    xml.writeEndElement();
}

void writeLayer(QXmlStreamWriter& xml, const Layer& layer) {
    xml.writeStartElement("layer"_L1);
    if (!layer.name.isNull()) {
        xml.writeAttribute("name"_L1, layer.name);
    }
    for (const Element& element: layer.elements) {
        if (const auto* stroke = std::get_if<Stroke>(&element)) {
            writeStroke(xml, *stroke);
        } else if (const auto* text = std::get_if<TextElement>(&element)) {
            writeText(xml, *text);
        } else if (const auto* image = std::get_if<ImageElement>(&element)) {
            writeImage(xml, *image);
        } else if (const auto* link = std::get_if<LinkElement>(&element)) {
            writeLink(xml, *link);
        }
    }
    xml.writeEndElement();
}

void writeBackground(QXmlStreamWriter& xml, const Document& doc, const Background& bg, bool& pdfFileWritten) {
    // The original Xournal relies on the order of these attributes: do not change it
    xml.writeStartElement("background"_L1);
    if (!bg.name.isNull()) {
        xml.writeAttribute("name"_L1, bg.name);
    }
    switch (bg.type) {
        case Background::Type::Pdf:
            xml.writeAttribute("type"_L1, "pdf"_L1);
            if (!pdfFileWritten) {
                pdfFileWritten = true;
                xml.writeAttribute("domain"_L1, doc.pdfDomain.isEmpty() ? u"absolute"_s : doc.pdfDomain);
                xml.writeAttribute("filename"_L1, doc.pdfFilename);
            }
            xml.writeAttribute("pageno"_L1, QString::number(bg.pdfPage + 1));
            break;
        case Background::Type::Pixmap:
            xml.writeAttribute("type"_L1, "pixmap"_L1);
            xml.writeAttribute("domain"_L1, bg.domain);
            xml.writeAttribute("filename"_L1, bg.filename);
            break;
        case Background::Type::Solid:
            xml.writeAttribute("type"_L1, "solid"_L1);
            xml.writeAttribute("color"_L1, colorString(bg.color));
            xml.writeAttribute("style"_L1, bg.style.isEmpty() ? u"plain"_s : bg.style);
            if (!bg.config.isEmpty()) {
                xml.writeAttribute("config"_L1, bg.config);
            }
            break;
    }
    xml.writeEndElement();
}

/// Thumbnail of the first page, as shown by file managers. Empty if the page has a PDF background.
QByteArray previewPng(const Document& doc) {
    if (doc.pages.empty() || doc.pages.front().background.type == Background::Type::Pdf) {
        return {};
    }
    const Page& page = doc.pages.front();
    if (page.width <= 0 || page.height <= 0) {
        return {};
    }
    const double scale = PREVIEW_SIZE / std::max(page.width, page.height);
    QImage image((QSizeF(page.width, page.height) * scale).toSize().expandedTo(QSize(1, 1)),
                 QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter p(&image);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.scale(scale, scale);
    Renderer::renderPage(p, page, nullptr, QRectF(0, 0, page.width, page.height));
    p.end();

    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

bool gzip(const QByteArray& in, QByteArray& out) {
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK) {  // +16: gzip
        return false;
    }
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(in.data()));
    zs.avail_in = static_cast<uInt>(in.size());

    char buffer[1 << 16];
    int ret = Z_OK;
    do {
        zs.next_out = reinterpret_cast<Bytef*>(buffer);
        zs.avail_out = sizeof(buffer);
        ret = deflate(&zs, Z_FINISH);
        if (ret != Z_OK && ret != Z_STREAM_END) {
            deflateEnd(&zs);
            return false;
        }
        out.append(buffer, static_cast<qsizetype>(sizeof(buffer) - zs.avail_out));
    } while (ret != Z_STREAM_END);
    deflateEnd(&zs);
    return true;
}

/// Copies a file stored next to the old .xopp file to the new location
bool copyAttachment(const QString& from, const QString& to) {
    if (from == to || !QFile::exists(from) || QFile::exists(to)) {
        return true;
    }
    return QFile::copy(from, to);
}

}  // namespace

QByteArray writeXoppXml(const Document& doc) {
    QByteArray data;
    QXmlStreamWriter xml(&data);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(0);

    xml.writeStartDocument(u"1.0"_s, false);
    xml.writeStartElement("xournal"_L1);
    xml.writeAttribute("creator"_L1, u"qournal %1"_s.arg(QCoreApplication::applicationVersion()).trimmed());
    // Version 5 files cannot be read by Xournal++ before 1.3: use it only if needed
    xml.writeAttribute("fileversion"_L1, XoppCompat::needsFormat5(doc) ? u"5"_s : u"4"_s);
    xml.writeTextElement("title"_L1, u"Xournal++ document - see https://xournalpp.github.io/"_s);
    if (const QByteArray preview = previewPng(doc); !preview.isEmpty()) {
        xml.writeTextElement("preview"_L1, QString::fromLatin1(preview.toBase64()));
    }

    bool pdfFileWritten = false;
    for (const Page& page: doc.pages) {
        xml.writeStartElement("page"_L1);
        xml.writeAttribute("width"_L1, num(page.width));
        xml.writeAttribute("height"_L1, num(page.height));
        writeBackground(xml, doc, page.background, pdfFileWritten);
        if (page.layers.empty()) {
            // The original Xournal cannot read pages without a layer
            xml.writeEmptyElement("layer"_L1);
        }
        for (const Layer& layer: page.layers) {
            writeLayer(xml, layer);
        }
        xml.writeEndElement();
    }

    xml.writeEndElement();
    xml.writeEndDocument();
    return data;
}

bool saveXopp(const QString& path, const Document& doc, QString* error) {
    auto fail = [error](const QString& msg) {
        if (error) {
            *error = msg;
        }
        return false;
    };

    QByteArray compressed;
    if (!gzip(writeXoppXml(doc), compressed)) {
        return fail(QCoreApplication::translate("XoppWriter", "Could not compress the document"));
    }

    // Write to a temporary file first, so that a failure does not destroy the existing file.
    // That is not possible for non-file locations (e.g. content:// on Android).
    const bool local = !path.contains(u"://");
    if (local) {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(compressed) != compressed.size() || !file.commit()) {
            return fail(QCoreApplication::translate("XoppWriter", "Could not write \"%1\": %2")
                                .arg(path, file.errorString()));
        }
    } else {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(compressed) != compressed.size()) {
            return fail(QCoreApplication::translate("XoppWriter", "Could not write \"%1\": %2")
                                .arg(path, file.errorString()));
        }
    }

    // Images that became backgrounds in this session are written next to the document
    if (local) {
        for (const Page& page: doc.pages) {
            const Background& bg = page.background;
            const QString target = path + u'.' + bg.filename;
            const bool known = !doc.sourcePath.isEmpty() && QFile::exists(doc.sourcePath + u'.' + bg.filename);
            if (bg.type == Background::Type::Pixmap && bg.domain == u"attach" && !bg.pixmap.isNull() && !known &&
                !bg.pixmap.save(target, "PNG")) {
                return fail(QCoreApplication::translate("XoppWriter", "Could not write \"%1\"").arg(target));
            }
        }
    }

    if (local && !doc.sourcePath.isEmpty() && doc.sourcePath != path) {
        bool copied = true;
        if (doc.pdfDomain == u"attach") {
            copied &= copyAttachment(doc.sourcePath + u'.' + doc.pdfFilename, path + u'.' + doc.pdfFilename);
        }
        for (const Page& page: doc.pages) {
            const Background& bg = page.background;
            if (bg.type == Background::Type::Pixmap && bg.domain == u"attach") {
                copied &= copyAttachment(doc.sourcePath + u'.' + bg.filename, path + u'.' + bg.filename);
            }
        }
        if (!copied) {
            return fail(QCoreApplication::translate(
                                "XoppWriter", "The document was saved, but its attached background files could not be "
                                              "copied next to \"%1\"")
                                .arg(path));
        }
    }
    return true;
}
