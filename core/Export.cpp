#include "Export.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QPageSize>
#include <QPdfWriter>
#include <QRegularExpression>
#include <QSet>
#include <cmath>
#include <cstring>
#include <functional>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif
#ifdef HAVE_QTSVG
#include <QSvgGenerator>
#endif

#include "PdfFile.h"
#include "PdfPage.h"
#include "Renderer.h"

namespace {

bool fail(QString* error, const QString& message) {
    if (error) {
        *error = message;
    }
    return false;
}

/// The PDF of the background, opened once for an export
class BackgroundPdf {
public:
    explicit BackgroundPdf(const Document& doc): m_path(doc.pdfPath) {
#ifdef HAVE_QTPDF
        if (!doc.pdfPath.isEmpty()) {
            m_pdf.load(doc.pdfPath);
        }
#endif
    }

    /// Draws the page of the PDF as vectors, as Xournal++ does when it prints or exports to SVG. False if that
    /// cannot be done (the file cannot be read, or the page has something the drawing does not understand)
    bool drawVectors(QPainter& p, const Page& page) {
        if (!m_readerTried) {
            m_readerTried = true;
            QFile file(m_path);
            if (!m_path.isEmpty() && file.open(QIODevice::ReadOnly) && m_reader.load(file.readAll())) {
                m_pages = m_reader.pages();
                m_drawer = std::make_unique<Pdf::PageDrawer>(m_reader);
            }
        }
        const int index = page.background.pdfPage;
        if (!m_drawer || index < 0 || index >= static_cast<int>(m_pages.size())) {
            return false;
        }
        p.save();
        p.fillRect(QRectF(0, 0, page.width, page.height), Qt::white);
        const bool drawn = m_drawer->draw(p, m_pages[static_cast<size_t>(index)], QSizeF(page.width, page.height));
        p.restore();
        return drawn;
    }

    QImage render(const Page& page, double dpi) {
#ifdef HAVE_QTPDF
        const int pdfPage = page.background.pdfPage;
        if (m_pdf.status() == QPdfDocument::Status::Ready && pdfPage >= 0 && pdfPage < m_pdf.pageCount()) {
            const QSize size = (QSizeF(page.width, page.height) * dpi / 72.0).toSize();
            // With the annotations of the PDF, as on the screen
            QPdfDocumentRenderOptions options;
            options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
            return m_pdf.render(pdfPage, size, options);
        }
#else
        Q_UNUSED(page)
        Q_UNUSED(dpi)
#endif
        return {};
    }

private:
    QString m_path;
#ifdef HAVE_QTPDF
    QPdfDocument m_pdf;
#endif
    bool m_readerTried = false;
    Pdf::Reader m_reader;
    std::vector<Pdf::PageInfo> m_pages;
    std::unique_ptr<Pdf::PageDrawer> m_drawer;
};

/// @param vectors the pages of a background PDF as vectors where that can be done, else as images
void paint(QPainter& p, BackgroundPdf& pdf, const Export::Sheet& sheet, const ExportOptions& options,
           double backgroundDpi, bool withBackground = true, bool vectors = false) {
    Page page = sheet.content;
    const QRectF rect(0, 0, page.width, page.height);
    p.save();
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    p.setClipRect(rect);
    if (withBackground && options.background != ExportOptions::Background::None) {
        if (options.background == ExportOptions::Background::NoRuling &&
            page.background.type == Background::Type::Solid) {
            page.background.style = QStringLiteral("plain");
        }
        page.backgroundVisible = true;
        const bool isPdf = page.background.type == Background::Type::Pdf;
        if (!(isPdf && vectors && pdf.drawVectors(p, page))) {
            QImage image;
            if (isPdf) {
                image = pdf.render(page, backgroundDpi);
            }
            Renderer::renderBackground(p, page, image.isNull() ? nullptr : &image);
        }
    }
    Renderer::renderLayers(p, page, rect, true);
    p.restore();
}

/// The four numbers of a rectangle of a PDF file, in order
bool pdfBox(const Pdf::Reader& reader, const Pdf::Dict& dict, const char* key, QRectF& box) {
    const Pdf::Value* value = dict.find(key);
    const Pdf::Value array = value ? reader.resolve(*value) : Pdf::Value();
    if (array.kind() != Pdf::Value::Kind::Array || array.toArray().size() != 4) {
        return false;
    }
    double n[4];
    for (size_t i = 0; i < 4; ++i) {
        n[i] = reader.resolve(array.toArray()[i]).toNumber();
    }
    box = QRectF(QPointF(std::min(n[0], n[2]), std::min(n[1], n[3])),
                 QPointF(std::max(n[0], n[2]), std::max(n[1], n[3])));
    return !box.isEmpty();
}

enum class Kept { Done, Unusable, Failed };

/// How many states of the graphics a content stream saves (q) and does not restore (Q): viewers forgive that, but
/// what is drawn after it would be drawn in them
int unclosedSaves(const QByteArray& content) {
    int open = 0;
    const qsizetype n = content.size();
    auto isSpace = [](char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t' || c == '\f' || c == '\0'; };
    auto isDelimiter = [](char c) { return std::strchr("()<>[]{}/%", c) != nullptr; };
    for (qsizetype i = 0; i < n;) {
        const char c = content[i];
        if (isSpace(c)) {
            ++i;
        } else if (c == '%') {
            while (i < n && content[i] != '\n' && content[i] != '\r') {
                ++i;
            }
        } else if (c == '(') {
            // A string, with nested parentheses and escapes
            int depth = 0;
            for (; i < n; ++i) {
                if (content[i] == '\\') {
                    ++i;
                } else if (content[i] == '(') {
                    ++depth;
                } else if (content[i] == ')' && --depth == 0) {
                    ++i;
                    break;
                }
            }
        } else if (c == '<' && i + 1 < n && content[i + 1] != '<') {
            while (i < n && content[i] != '>') {
                ++i;
            }
            ++i;
        } else if (isDelimiter(c)) {
            ++i;
            if (c == '/') {
                while (i < n && !isSpace(content[i]) && !isDelimiter(content[i])) {
                    ++i;
                }
            }
        } else {
            const qsizetype start = i;
            while (i < n && !isSpace(content[i]) && !isDelimiter(content[i])) {
                ++i;
            }
            const QByteArray word = content.mid(start, i - start);
            if (word == "q") {
                ++open;
            } else if (word == "Q") {
                open = std::max(open - 1, 0);
            } else if (word == "ID") {
                // The data of an inline image, up to EI
                const qsizetype end = content.indexOf("EI", i);
                i = end < 0 ? n : end + 2;
            }
        }
    }
    return open;
}

/// A number of a PDF in the content of a page
QByteArray pdfNumber(double value) { return QByteArray::number(std::abs(value) < 1e-9 ? 0.0 : value, 'f', 5); }

/**
 * Puts the annotations of a page of the PDF that only show something (ink, highlights, stamps, notes) into its
 * content, below the annotations of the document: as annotations they would be drawn over them. They are drawn as
 * viewers draw them, from their appearance. Links and form fields stay annotations, so that they can be used; so
 * do annotations without appearance or with optional content, which cannot be drawn this way.
 * @param objects gets the appearances as forms, named in the returned content
 * @param annots gets the annotations that stay annotations
 * @return the content that draws the others
 */
QByteArray flattenAnnotations(const Pdf::Reader& reader, const Pdf::Dict& page, Pdf::Dict& objects,
                              Pdf::Array& annots) {
    QByteArray content;
    const Pdf::Value* annotsValue = page.find("Annots");
    const Pdf::Value list = annotsValue ? reader.resolve(*annotsValue) : Pdf::Value();
    // The annotations that were put into the content: by their place in the list and by their objects
    std::vector<bool> drawn;
    QSet<int> drawnObjects;
    std::vector<std::pair<Pdf::Value, Pdf::Dict>> entries;
    for (const Pdf::Value& item: list.toArray()) {
        const Pdf::Value annotation = reader.resolve(item);
        if (annotation.kind() != Pdf::Value::Kind::Dict) {
            continue;
        }
        entries.emplace_back(item, annotation.toDict());
    }
    drawn.resize(entries.size());
    for (size_t index = 0; index < entries.size(); ++index) {
        // Not a structured binding: a lambda cannot capture one before C++20
        const Pdf::Value& item = entries[index].first;
        const Pdf::Dict& a = entries[index].second;
        const auto get = [&](const char* key) {
            const Pdf::Value* value = a.find(key);
            return value ? reader.resolve(*value) : Pdf::Value();
        };
        const QByteArray subtype = get("Subtype").text();
        const int flags = get("F").toInt();
        // Hidden (2) or not to be shown (32): they stay what they are, as do links and form fields
        const bool shown = !(flags & 2) && !(flags & 32);
        if (!shown || subtype == "Link" || subtype == "Widget" || subtype == "Popup" || a.find("OC")) {
            continue;
        }
        const Pdf::Value appearances = get("AP");
        const Pdf::Value* normalValue =
                appearances.kind() == Pdf::Value::Kind::Dict ? appearances.toDict().find("N") : nullptr;
        if (!normalValue) {
            continue;
        }
        // One appearance, or one per state
        std::optional<Pdf::Ref> appearance;
        const Pdf::Value normal = reader.resolve(*normalValue);
        if (normalValue->kind() == Pdf::Value::Kind::Ref && normal.toDict().find("BBox")) {
            appearance = normalValue->toRef();
        } else if (const Pdf::Value* chosen = normal.toDict().find(get("AS").text());
                   chosen && chosen->kind() == Pdf::Value::Kind::Ref) {
            appearance = chosen->toRef();
        }
        if (!appearance) {
            continue;
        }
        const Pdf::Dict form = reader.object(*appearance).toDict();
        QRectF rect;
        QRectF bbox;
        if (!pdfBox(reader, a, "Rect", rect) || !pdfBox(reader, form, "BBox", bbox)) {
            continue;
        }
        // The box of the appearance, transformed by its matrix, is fitted into the rectangle of the annotation
        QTransform matrix;
        const Pdf::Value* matrixValue = form.find("Matrix");
        const Pdf::Value m = matrixValue ? reader.resolve(*matrixValue) : Pdf::Value();
        if (m.toArray().size() == 6) {
            const Pdf::Array& n = m.toArray();
            matrix = QTransform(reader.resolve(n[0]).toNumber(), reader.resolve(n[1]).toNumber(),
                                reader.resolve(n[2]).toNumber(), reader.resolve(n[3]).toNumber(),
                                reader.resolve(n[4]).toNumber(), reader.resolve(n[5]).toNumber());
        }
        bbox = matrix.mapRect(bbox);
        if (bbox.width() <= 0 || bbox.height() <= 0) {
            continue;
        }
        const double sx = rect.width() / bbox.width();
        const double sy = rect.height() / bbox.height();
        const QByteArray name = "XojAnnot" + QByteArray::number(appearance->number);
        objects.set(name, Pdf::Value::ref(*appearance));
        content += "q " + pdfNumber(sx) + " 0 0 " + pdfNumber(sy) + ' ' + pdfNumber(rect.left() - bbox.left() * sx) +
                   ' ' + pdfNumber(rect.top() - bbox.top() * sy) + " cm /" + name + " Do Q\n";
        drawn[index] = true;
        if (item.kind() == Pdf::Value::Kind::Ref) {
            drawnObjects.insert(item.toRef().number);
        }
    }
    if (content.isEmpty()) {
        annots = list.toArray();
        return {};
    }
    for (size_t index = 0; index < entries.size(); ++index) {
        const auto& [item, a] = entries[index];
        // The note of an annotation that is now content goes with it
        const Pdf::Value* parent = a.find("Parent");
        const Pdf::Value* subtype = a.find("Subtype");
        const bool noteOfDrawn = subtype && reader.resolve(*subtype).text() == "Popup" && parent &&
                                 parent->kind() == Pdf::Value::Kind::Ref &&
                                 drawnObjects.contains(parent->toRef().number);
        if (!drawn[index] && !noteOfDrawn) {
            annots.push_back(item);
        }
    }
    return content;
}

/**
 * Writes the document as its background PDF with the annotations added: the pages of the PDF stay as they are,
 * with their text and their links. Pages without a page of the PDF are new pages.
 * @return Unusable if the PDF cannot be taken apart (it is encrypted or damaged): the pages are exported as
 *         images then
 */
Kept toPdfKeepingPages(const Document& doc, const std::vector<Export::Sheet>& sheets, const ExportOptions& options,
                       const QString& path, QString* error) {
    QFile source(doc.pdfPath);
    Pdf::Reader reader;
    if (!source.open(QIODevice::ReadOnly) || !reader.load(source.readAll())) {
        return Kept::Unusable;
    }
    const std::vector<Pdf::PageInfo> sourcePages = reader.pages();
    const auto root = reader.pagesRoot();
    if (sourcePages.empty() || !root) {
        return Kept::Unusable;
    }
    // Every page that is used has to be there before anything is written
    for (const Export::Sheet& sheet: sheets) {
        const Background& bg = sheet.content.background;
        if (bg.type == Background::Type::Pdf &&
            (bg.pdfPage < 0 || bg.pdfPage >= static_cast<int>(sourcePages.size()))) {
            return Kept::Unusable;
        }
    }

    // The annotations, written by the PDF writer of Qt: texts as text in their fonts, images compressed. Each of its
    // pages is put on the page of the file as a form
    QByteArray overlayData;
    {
        QBuffer buffer(&overlayData);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(72);
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setCreator(QStringLiteral("Qournal"));
        auto setPageSize = [&](const Page& page) {
            writer.setPageSize(
                    QPageSize(QSizeF(page.width, page.height), QPageSize::Point, QString(), QPageSize::ExactMatch));
        };
        setPageSize(sheets.front().content);
        BackgroundPdf noImages{Document()};
        QPainter p(&writer);
        for (size_t i = 0; i < sheets.size(); ++i) {
            if (i > 0) {
                setPageSize(sheets[i].content);
                writer.newPage();
            }
            const bool keepsPage = sheets[i].content.background.type == Background::Type::Pdf;
            paint(p, noImages, sheets[i], options, options.backgroundDpi, !keepsPage);
        }
    }
    Pdf::Reader overlay;
    if (!overlay.load(overlayData) || overlay.pages().size() != sheets.size()) {
        return Kept::Unusable;
    }
    const std::vector<Pdf::PageInfo> overlayPages = overlay.pages();

    Pdf::Update update(reader);
    // Objects of the annotations, copied into the file once: fonts, images, states of the graphics
    QHash<int, Pdf::Ref> copied;
    std::function<Pdf::Value(const Pdf::Value&)> copy = [&](const Pdf::Value& value) -> Pdf::Value {
        switch (value.kind()) {
            case Pdf::Value::Kind::Ref: {
                const int number = value.toRef().number;
                if (const auto it = copied.constFind(number); it != copied.constEnd()) {
                    return Pdf::Value::ref(*it);
                }
                const Pdf::Ref ref = update.newRef();
                copied.insert(number, ref);
                Pdf::Dict dict;
                QByteArray raw;
                if (overlay.rawStream(value.toRef(), dict, raw)) {
                    update.addStream(ref, copy(Pdf::Value::dict(dict)).toDict(), raw, false);
                } else {
                    update.add(ref, copy(overlay.object(value.toRef())));
                }
                return Pdf::Value::ref(ref);
            }
            case Pdf::Value::Kind::Array: {
                Pdf::Array array;
                for (const Pdf::Value& item: value.toArray()) {
                    array.push_back(copy(item));
                }
                return Pdf::Value::array(std::move(array));
            }
            case Pdf::Value::Kind::Dict: {
                Pdf::Dict dict;
                for (const auto& [key, item]: value.toDict().entries()) {
                    if (key != "Parent") {
                        dict.set(key, copy(item));
                    }
                }
                return Pdf::Value::dict(std::move(dict));
            }
            default:
                return value;
        }
    };
    Pdf::Array kids;
    QSet<int> usedPages;
    std::optional<Pdf::Ref> saveState;
    for (size_t index = 0; index < sheets.size(); ++index) {
        const Export::Sheet& sheet = sheets[index];
        const Page& page = sheet.content;
        const bool keepsPage = page.background.type == Background::Type::Pdf;
        const Pdf::Value number0 = Pdf::Value::number(0);
        QByteArray overlayStream;
        Pdf::Value overlayResources;
        {
            const Pdf::PageInfo& info = overlayPages[index];
            const Pdf::Value* contents = info.dict.find("Contents");
            Pdf::Array parts;
            if (contents && overlay.resolve(*contents).kind() == Pdf::Value::Kind::Array) {
                parts = overlay.resolve(*contents).toArray();
            } else if (contents) {
                parts.push_back(*contents);
            }
            for (const Pdf::Value& part: parts) {
                Pdf::Dict dict;
                QByteArray data;
                if (part.kind() == Pdf::Value::Kind::Ref && overlay.stream(part.toRef(), dict, data)) {
                    overlayStream += data + '\n';
                }
            }
            const Pdf::Value* resourcesValue = info.dict.find("Resources");
            overlayResources = resourcesValue ? copy(*resourcesValue) : Pdf::Value::dict(Pdf::Dict());
        }
        const bool hasAnnotations = std::any_of(page.layers.begin(), page.layers.end(),
                                                [](const Layer& layer) { return !layer.elements.empty(); });

        if (!keepsPage) {
            // A page of its own: the page of the annotations as it is
            const Pdf::Ref contentRef = update.newRef();
            update.addStream(contentRef, Pdf::Dict(), overlayStream);
            Pdf::Dict dict;
            dict.set("Type", Pdf::Value::name("Page"));
            dict.set("Parent", Pdf::Value::ref(*root));
            dict.set("MediaBox", Pdf::Value::array({number0, number0, Pdf::Value::number(page.width),
                                                    Pdf::Value::number(page.height)}));
            dict.set("Resources", overlayResources);
            dict.set("Contents", Pdf::Value::ref(contentRef));
            const Pdf::Ref ref = update.newRef();
            update.add(ref, Pdf::Value::dict(dict));
            kids.push_back(Pdf::Value::ref(ref));
            continue;
        }

        const Pdf::PageInfo& info = sourcePages[static_cast<size_t>(page.background.pdfPage)];
        Pdf::Dict dict = info.dict;
        dict.set("Parent", Pdf::Value::ref(*root));
        if (hasAnnotations) {
            // What is shown of the page: its crop box, turned by its rotation. The annotations are put into that
            // area, in the coordinates of the page
            QRectF box;
            // Viewers show the part of the crop box that is within the media box
            QRectF media;
            if (!pdfBox(reader, info.dict, "MediaBox", media)) {
                // Viewers show a page without a size as US Letter
                media = QRectF(0, 0, 612, 792);
                dict.set("MediaBox",
                         Pdf::Value::array({number0, number0, Pdf::Value::number(612), Pdf::Value::number(792)}));
            }
            if (!pdfBox(reader, info.dict, "CropBox", box)) {
                box = media;
            } else if (!media.isEmpty() && box.intersects(media)) {
                box = box.intersected(media);
            }
            const Pdf::Value* rotateValue = info.dict.find("Rotate");
            const int rotate = rotateValue ? ((reader.resolve(*rotateValue).toInt() % 360) + 360) % 360 : 0;
            const bool sideways = rotate == 90 || rotate == 270;
            const double sx = (sideways ? box.height() : box.width()) / page.width;
            const double sy = (sideways ? box.width() : box.height()) / page.height;
            double m[6];
            switch (rotate) {
                case 90:
                    m[0] = 0, m[1] = sx, m[2] = sy, m[3] = 0, m[4] = box.left(), m[5] = box.top();
                    break;
                case 180:
                    m[0] = -sx, m[1] = 0, m[2] = 0, m[3] = sy, m[4] = box.right(), m[5] = box.top();
                    break;
                case 270:
                    m[0] = 0, m[1] = -sx, m[2] = -sy, m[3] = 0, m[4] = box.right(), m[5] = box.bottom();
                    break;
                default:
                    m[0] = sx, m[1] = 0, m[2] = 0, m[3] = -sy, m[4] = box.left(), m[5] = box.bottom();
                    break;
            }
            // The page of the annotations has its origin at the bottom, the coordinates above at the top
            const QTransform toPage =
                    QTransform(1, 0, 0, -1, 0, page.height) * QTransform(m[0], m[1], m[2], m[3], m[4], m[5]);
            Pdf::Dict form;
            form.set("Type", Pdf::Value::name("XObject"));
            form.set("Subtype", Pdf::Value::name("Form"));
            form.set("BBox", Pdf::Value::array({number0, number0, Pdf::Value::number(page.width),
                                                Pdf::Value::number(page.height)}));
            form.set("Matrix", Pdf::Value::array({Pdf::Value::number(toPage.m11()), Pdf::Value::number(toPage.m12()),
                                                  Pdf::Value::number(toPage.m21()), Pdf::Value::number(toPage.m22()),
                                                  Pdf::Value::number(toPage.dx()), Pdf::Value::number(toPage.dy())}));
            form.set("Resources", overlayResources);
            // A transparency group, as the page of the annotations is
            Pdf::Dict group;
            group.set("S", Pdf::Value::name("Transparency"));
            form.set("Group", Pdf::Value::dict(group));
            const Pdf::Ref formRef = update.newRef();
            update.addStream(formRef, form, overlayStream);
            const QByteArray formName = "XojOverlay" + QByteArray::number(formRef.number);

            // The content of the page between "q" and "Q", so that what it leaves behind does not reach the
            // annotations, which are drawn after it
            if (!saveState) {
                saveState = update.newRef();
                update.addStream(*saveState, Pdf::Dict(), "q\n", false);
            }
            // The resources of the page, with the annotations added
            const Pdf::Value* resourcesValue = info.dict.find("Resources");
            Pdf::Value resources = resourcesValue ? reader.resolve(*resourcesValue) : Pdf::Value();
            Pdf::Dict resourcesDict = resources.kind() == Pdf::Value::Kind::Dict ? resources.toDict() : Pdf::Dict();
            const Pdf::Value* objectsValue = resourcesDict.find("XObject");
            const Pdf::Value objects = objectsValue ? reader.resolve(*objectsValue) : Pdf::Value();
            Pdf::Dict objectsDict = objects.kind() == Pdf::Value::Kind::Dict ? objects.toDict() : Pdf::Dict();
            objectsDict.set(formName, Pdf::Value::ref(formRef));
            Pdf::Array annots;
            const QByteArray flattened = flattenAnnotations(reader, info.dict, objectsDict, annots);
            if (!flattened.isEmpty()) {
                if (annots.empty()) {
                    dict.remove("Annots");
                } else {
                    dict.set("Annots", Pdf::Value::array(std::move(annots)));
                }
            }
            resourcesDict.set("XObject", Pdf::Value::dict(objectsDict));
            dict.set("Resources", Pdf::Value::dict(resourcesDict));

            Pdf::Array contents{Pdf::Value::ref(*saveState)};
            Pdf::Array originalParts;
            if (const Pdf::Value* original = info.dict.find("Contents")) {
                // An array of streams as an object of its own (kept in a variable: the loop must not go over a
                // part of a temporary value)
                const Pdf::Value referenced =
                        original->kind() == Pdf::Value::Kind::Ref ? reader.object(original->toRef()) : Pdf::Value();
                if (referenced.kind() == Pdf::Value::Kind::Array) {
                    originalParts = referenced.toArray();
                } else if (original->kind() == Pdf::Value::Kind::Array) {
                    originalParts = original->toArray();
                } else {
                    originalParts.push_back(*original);
                }
                contents.insert(contents.end(), originalParts.begin(), originalParts.end());
            }
            // As many restores as the content leaves open, and the one of the q in front of it
            int open = 0;
            for (const Pdf::Value& part: originalParts) {
                Pdf::Dict partDict;
                QByteArray data;
                if (part.kind() == Pdf::Value::Kind::Ref && reader.stream(part.toRef(), partDict, data)) {
                    open += unclosedSaves(data + '\n');
                }
            }
            const Pdf::Ref overlayRef = update.newRef();
            update.addStream(overlayRef, Pdf::Dict(),
                             "\n" + QByteArray("Q\n").repeated(open + 1) + flattened + "q\n/" + formName + " Do\nQ\n",
                             false);
            contents.push_back(Pdf::Value::ref(overlayRef));
            dict.set("Contents", Pdf::Value::array(std::move(contents)));
        }
        // The page keeps its number, so that links and the outline of the PDF still lead to it. A page that is
        // in the document twice is a second object, and so is a page that a broken file has as its page tree:
        // the tree is written in its place
        Pdf::Ref ref = info.ref;
        if (usedPages.contains(info.ref.number) || info.ref.number == root->number) {
            ref = update.newRef();
        }
        usedPages.insert(info.ref.number);
        update.add(ref, Pdf::Value::dict(dict));
        kids.push_back(Pdf::Value::ref(ref));
    }

    // The page tree: one node with the pages of the export, in place of the root of the tree of the file
    Pdf::Dict pages;
    pages.set("Type", Pdf::Value::name("Pages"));
    pages.set("Count", Pdf::Value::number(static_cast<double>(kids.size())));
    pages.set("Kids", Pdf::Value::array(std::move(kids)));
    update.add(*root, Pdf::Value::dict(pages));

    const QByteArray data = update.finish();
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        fail(error, QCoreApplication::translate("Export", "Could not write \"%1\": %2").arg(path, file.errorString()));
        return Kept::Failed;
    }
    return Kept::Done;
}

}  // namespace

bool ElementRange::parse(const QString& range, int count, QList<int>& result, QString* error) {
    static const QRegularExpression separators(QStringLiteral("[,;:]"));
    static const QRegularExpression entry(QStringLiteral(R"(^\s*(\d*)\s*(-?)\s*(\d*)\s*$)"));
    QList<int> parsed;
    for (const QString& token: range.split(separators)) {
        const auto match = entry.match(token);
        const bool isRange = match.hasMatch() && !match.captured(2).isEmpty();
        if (!match.hasMatch() || (!isRange && (match.captured(1).isEmpty() || !match.captured(3).isEmpty()))) {
            return fail(error,
                        QCoreApplication::translate("Export", "\"%1\" is not a range: use numbers like \"1-3,5,7-\"")
                                .arg(token.trimmed()));
        }
        // "n", "n-", "-m", "n-m" or "-"
        bool ok1 = true, ok2 = true;
        const int first = match.captured(1).isEmpty() ? 1 : match.captured(1).toInt(&ok1);
        const int last = !isRange ? first : match.captured(3).isEmpty() ? count : match.captured(3).toInt(&ok2);
        if (!ok1 || !ok2 || first > count || last > count) {
            return fail(error, QCoreApplication::translate("Export", "\"%1\" is beyond the last one, which is %2")
                                       .arg(token.trimmed())
                                       .arg(count));
        }
        if (first == 0) {
            return fail(error, QCoreApplication::translate("Export", "The numbers start with 1"));
        }
        if (last < first) {
            return fail(error,
                        QCoreApplication::translate("Export", "\"%1\": the first number is larger than the second")
                                .arg(token.trimmed()));
        }
        for (int i = first; i <= last; ++i) {
            parsed.append(i - 1);
        }
    }
    result = parsed;
    return true;
}

std::vector<Export::Sheet> Export::sheets(const Document& doc, const ExportOptions& options) {
    std::vector<Sheet> result;
    QList<int> pages = options.pages;
    if (pages.isEmpty()) {
        for (int i = 0; i < static_cast<int>(doc.pages.size()); ++i) {
            pages.append(i);
        }
    }
    for (int index: std::as_const(pages)) {
        if (index < 0 || index >= static_cast<int>(doc.pages.size())) {
            continue;
        }
        Page page = doc.pages[static_cast<size_t>(index)];
        // The layers that are exported: the given ones, or else the visible ones
        std::vector<int> shown;
        for (int l = 0; l < static_cast<int>(page.layers.size()); ++l) {
            const bool wanted =
                    options.layers.isEmpty() ? page.layers[static_cast<size_t>(l)].visible : options.layers.contains(l);
            page.layers[static_cast<size_t>(l)].visible = wanted;
            if (wanted) {
                shown.push_back(l);
            }
        }
        if (!options.progressiveLayers || shown.size() <= 1) {
            result.push_back({index, page});
            continue;
        }
        // One sheet per layer: each shows the layers up to it
        for (size_t upTo = 0; upTo < shown.size(); ++upTo) {
            Page step = page;
            for (size_t i = upTo + 1; i < shown.size(); ++i) {
                step.layers[static_cast<size_t>(shown[i])].visible = false;
            }
            result.push_back({index, step});
        }
    }
    return result;
}

void Export::paintSheet(QPainter& p, const Document& doc, const Sheet& sheet, const ExportOptions& options,
                        double backgroundDpi) {
    BackgroundPdf pdf(doc);
    paint(p, pdf, sheet, options, backgroundDpi, true, true);
}

bool Export::toPdf(const Document& doc, const QString& path, const ExportOptions& options, QString* error) {
    const std::vector<Sheet> pages = sheets(doc, options);
    if (pages.empty()) {
        return fail(error, QCoreApplication::translate("Export", "There is nothing to export"));
    }
    // With a background PDF, the export is that PDF with the annotations added: its pages stay as they are
    const bool hasPdfPages = std::any_of(pages.begin(), pages.end(), [](const Sheet& sheet) {
        return sheet.content.background.type == Background::Type::Pdf;
    });
    if (hasPdfPages && options.background != ExportOptions::Background::None && !doc.pdfPath.isEmpty() &&
        !options.rasterizePdfPages) {
        const Kept kept = toPdfKeepingPages(doc, pages, options, path, error);
        if (kept != Kept::Unusable) {
            return kept == Kept::Done;
        }
    }
    BackgroundPdf pdf(doc);

    // Written to memory first: the file is only touched if everything worked
    QByteArray data;
    {
        QBuffer buffer(&data);
        buffer.open(QIODevice::WriteOnly);
        QPdfWriter writer(&buffer);
        writer.setResolution(72);  // one unit of the painter is one point, as on the pages
        writer.setPageMargins(QMarginsF(0, 0, 0, 0));
        writer.setCreator(QStringLiteral("Qournal"));
        auto setPageSize = [&](const Page& page) {
            writer.setPageSize(
                    QPageSize(QSizeF(page.width, page.height), QPageSize::Point, QString(), QPageSize::ExactMatch));
        };
        setPageSize(pages.front().content);
        QPainter p(&writer);
        for (size_t i = 0; i < pages.size(); ++i) {
            if (i > 0) {
                setPageSize(pages[i].content);
                writer.newPage();
            }
            paint(p, pdf, pages[i], options, options.backgroundDpi);
        }
    }

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(data) != data.size()) {
        return fail(error,
                    QCoreApplication::translate("Export", "Could not write \"%1\": %2").arg(path, file.errorString()));
    }
    return true;
}

bool Export::svgSupported() {
#ifdef HAVE_QTSVG
    return true;
#else
    return false;
#endif
}

bool Export::toImages(const Document& doc, const QString& path, const ExportOptions& options, QStringList* written,
                      QString* error) {
    const std::vector<Sheet> pages = sheets(doc, options);
    if (pages.empty()) {
        return fail(error, QCoreApplication::translate("Export", "There is nothing to export"));
    }
    const QFileInfo info(path);
    const QString format = options.format.isEmpty() ? info.suffix() : options.format;
    const bool svg = format.compare(u"svg", Qt::CaseInsensitive) == 0;
    // A location that is not a file (content:// on Android) stands for one document: there is no folder to put
    // further files into
    if (pages.size() > 1 && path.contains(u"://")) {
        return fail(
                error,
                QCoreApplication::translate(
                        "Export", "Several pages cannot be exported as images to this place: choose one page, or PDF"));
    }
    if (svg && !svgSupported()) {
        return fail(error, QCoreApplication::translate(
                                   "Export", "This build cannot write SVG files: it was built without Qt SVG"));
    }
    BackgroundPdf pdf(doc);

    for (size_t i = 0; i < pages.size(); ++i) {
        const Page& page = pages[i].content;
        // Several sheets: "name-1.png", "name-2.png", ...
        QString file = path;
        if (pages.size() > 1) {
            const QString suffix = info.suffix().isEmpty() ? QString() : u'.' + info.suffix();
            file = info.path() + u'/' + info.completeBaseName() + u'-' + QString::number(i + 1) + suffix;
        }

        if (svg) {
#ifdef HAVE_QTSVG
            QSvgGenerator generator;
            generator.setFileName(file);
            generator.setSize(QSizeF(page.width, page.height).toSize());
            generator.setViewBox(QRectF(0, 0, page.width, page.height));
            generator.setResolution(72);
            generator.setTitle(QFileInfo(doc.sourcePath).completeBaseName());
            QPainter p;
            if (!p.begin(&generator)) {
                return fail(error, QCoreApplication::translate("Export", "Could not write \"%1\"").arg(file));
            }
            paint(p, pdf, pages[i], options, options.backgroundDpi, true, true);
            p.end();
#endif
        } else {
            // The size in pixels: by the width, the height or the resolution
            double scale = options.dpi / 72.0;
            if (options.width > 0) {
                scale = options.width / page.width;
            } else if (options.height > 0) {
                scale = options.height / page.height;
            }
            const QSize size(std::max(1, static_cast<int>(std::lround(page.width * scale))),
                             std::max(1, static_cast<int>(std::lround(page.height * scale))));
            QImage image(size, QImage::Format_ARGB32_Premultiplied);
            image.fill(Qt::transparent);
            {
                QPainter p(&image);
                p.scale(scale, scale);
                paint(p, pdf, pages[i], options, 72.0 * scale);
            }
            QFile out(file);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || !image.save(&out, "PNG")) {
                return fail(error, QCoreApplication::translate("Export", "Could not write \"%1\"").arg(file));
            }
        }
        if (written) {
            written->append(file);
        }
    }
    return true;
}
