/*
 * Qournal
 *
 * Checks one PDF file the way documents use them: the PDF export with kept pages, and the drawing of its pages as
 * vectors. Prints one line with the results; tools/check_pdf_corpus.sh runs it for every file of a folder, so that
 * a crash or a hang concerns one file only.
 *
 *     pdfcorpus <file.pdf>
 *
 * @license GNU GPLv2 or later
 */

#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <cstdio>

#include "Document.h"
#include "Export.h"
#include "PdfFile.h"
#include "PdfPage.h"

namespace {

constexpr int CHECKED_PAGES = 3;
const QColor MARK(0xff, 0x00, 0x00);

double difference(const QImage& a, const QImage& b, int skipFrom, int skipTo) {
    if (a.size() != b.size()) {
        return 1;
    }
    qint64 different = 0;
    qint64 counted = 0;
    for (int y = 0; y < a.height(); ++y) {
        if (y >= skipFrom && y <= skipTo) {
            continue;
        }
        for (int x = 0; x < a.width(); ++x) {
            const QRgb p = a.pixel(x, y);
            const QRgb q = b.pixel(x, y);
            const int d = std::max(
                    {std::abs(qRed(p) - qRed(q)), std::abs(qGreen(p) - qGreen(q)), std::abs(qBlue(p) - qBlue(q))});
            different += d > 80;
            ++counted;
        }
    }
    return counted ? static_cast<double>(different) / counted : 0;
}

QImage onWhite(const QImage& image) {
    QImage result(image.size(), QImage::Format_RGB32);
    result.fill(Qt::white);
    QPainter p(&result);
    p.drawImage(0, 0, image);
    return result;
}

QPdfDocumentRenderOptions annotations() {
    QPdfDocumentRenderOptions options;
    options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    return options;
}

}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (argc < 2) {
        return 2;
    }
    const QString path = QString::fromLocal8Bit(argv[1]);
    QStringList result;
    QPdfDocument source;
    if (source.load(path) != QPdfDocument::Error::None || source.pageCount() == 0) {
        std::printf("%s\tqtpdf-cannot-open\n", qPrintable(path));
        return 0;
    }
    const int pages = source.pageCount();

    // A document on the PDF, with a red line on every page
    Document doc;
    doc.pdfPath = path;
    for (int i = 0; i < pages; ++i) {
        Page page;
        const QSizeF size = source.pagePointSize(i);
        page.width = size.width();
        page.height = size.height();
        page.background.type = Background::Type::Pdf;
        page.background.pdfPage = i;
        Stroke stroke;
        stroke.color = MARK;
        stroke.width = 6;
        stroke.points = {QPointF(page.width * 0.2, page.height * 0.5), QPointF(page.width * 0.8, page.height * 0.5)};
        stroke.updateBounds();
        Layer layer;
        layer.elements.push_back(stroke);
        page.layers.push_back(layer);
        doc.pages.push_back(page);
    }

    QFile file(path);
    Pdf::Reader reader;
    const bool readable = file.open(QIODevice::ReadOnly) && reader.load(file.readAll());
    result.append(readable ? QStringLiteral("reader-ok") : QStringLiteral("reader-refused:") + reader.error());
    if (qEnvironmentVariableIsSet("PDFCORPUS_OBJECT")) {
        const int n = qEnvironmentVariable("PDFCORPUS_OBJECT").toInt();
        std::printf("object %d: %s\n", n, reader.object(Pdf::Ref{n, 0}).serialize().left(300).constData());
    }

    QTemporaryDir dir;
    const QString out = dir.filePath(QStringLiteral("out.pdf"));
    QString error;
    if (!Export::toPdf(doc, out, ExportOptions(), &error)) {
        result.append(QStringLiteral("export-failed:") + error);
    } else {
        if (qEnvironmentVariableIsSet("PDFCORPUS_KEEP")) {
            QFile::remove(qEnvironmentVariable("PDFCORPUS_KEEP"));
            QFile::copy(out, qEnvironmentVariable("PDFCORPUS_KEEP"));
        }
        QFile written(out);
        QFile original(path);
        const bool kept = written.open(QIODevice::ReadOnly) && original.open(QIODevice::ReadOnly) &&
                          written.readAll().startsWith(original.readAll());
        result.append(kept ? QStringLiteral("kept") : QStringLiteral("as-images"));
        QPdfDocument exported;
        if (exported.load(out) != QPdfDocument::Error::None) {
            result.append(QStringLiteral("EXPORT-UNREADABLE"));
        } else if (exported.pageCount() != pages) {
            result.append(QStringLiteral("PAGES-%1-OF-%2").arg(exported.pageCount()).arg(pages));
        } else {
            for (int i = 0; i < std::min(pages, CHECKED_PAGES); ++i) {
                const QSize pixels = (source.pagePointSize(i) * 1.5).toSize();
                if (pixels.isEmpty()) {
                    continue;
                }
                const QImage before = onWhite(source.render(i, pixels, annotations()));
                const QImage after = onWhite(exported.render(i, pixels, annotations()));
                const int markY = pixels.height() / 2;
                const QColor marked = after.pixelColor(pixels.width() / 2, markY);
                if (!(marked.red() > 200 && marked.green() < 80 && marked.blue() < 80)) {
                    result.append(QStringLiteral("NO-MARK-p%1:%2").arg(i + 1).arg(marked.name()));
                }
                const double d = difference(before, after, markY - 8, markY + 8);
                if (d > 0.01) {
                    result.append(QStringLiteral("CHANGED-p%1:%2").arg(i + 1).arg(d, 0, 'f', 3));
                }
                // Every word of the page is still there (annotations that became content may add some)
                const QStringList wordsBefore = source.getAllText(i).text().split(
                        QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
                const QString textAfter = exported.getAllText(i).text();
                const bool textLost = std::any_of(wordsBefore.begin(), wordsBefore.end(),
                                                  [&](const QString& word) { return !textAfter.contains(word); });
                if (kept && textLost) {
                    result.append(QStringLiteral("TEXT-LOST-p%1").arg(i + 1));
                }
            }
        }
    }

    // The pages as vectors, as for printing and SVG
    if (readable) {
        Pdf::PageDrawer drawer(reader);
        const auto infos = reader.pages();
        int drawn = 0;
        QStringList refused;
        for (int i = 0; i < std::min<int>(static_cast<int>(infos.size()), CHECKED_PAGES); ++i) {
            const QSizeF size = source.pagePointSize(i);
            const QSize pixels = (size * 1.5).toSize();
            if (pixels.isEmpty()) {
                continue;
            }
            QImage image(pixels, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter p(&image);
            p.scale(1.5, 1.5);
            if (!drawer.draw(p, infos[static_cast<size_t>(i)], size)) {
                refused.append(drawer.why());
                continue;
            }
            p.end();
            ++drawn;
            const double d = difference(image, onWhite(source.render(i, pixels, annotations())), -1, -1);
            if (d > 0.02) {
                result.append(QStringLiteral("VECTORS-DIFFER-p%1:%2").arg(i + 1).arg(d, 0, 'f', 3));
                if (qEnvironmentVariableIsSet("PDFCORPUS_IMAGES")) {
                    const QString base = qEnvironmentVariable("PDFCORPUS_IMAGES") + u'/' +
                                         QFileInfo(path).completeBaseName() + QStringLiteral("-p%1").arg(i + 1);
                    image.save(base + QStringLiteral("-vectors.png"));
                    onWhite(source.render(i, pixels, annotations())).save(base + QStringLiteral("-qtpdf.png"));
                }
            }
        }
        result.append(QStringLiteral("vectors:%1").arg(drawn));
        if (!refused.isEmpty()) {
            refused.removeDuplicates();
            result.append(QStringLiteral("refused:") + refused.join(u'|'));
        }
    }
    std::printf("%s\t%s\n", qPrintable(path), qPrintable(result.join(u'\t')));
    return 0;
}
