/*
 * Qournal
 *
 * The part of PageCanvas that exports and prints the document and keeps autosave files
 *
 * @license GNU GPLv2 or later
 */

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QStandardPaths>
#include <algorithm>

#ifdef HAVE_PRINTSUPPORT
#include <QPrinter>
#include <QPrinterInfo>
#endif

#include "PageCanvas.h"
#include "Platform.h"
#include "XoppCompat.h"
#include "XoppLoader.h"
#include "XoppWriter.h"

namespace {

const QString AUTOSAVE_SUFFIX = QStringLiteral(".autosave.xopp");

QString autosaveDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/autosaves");
}

/// The autosave file of a document: hidden, next to it, as Xournal++ names it
QString autosaveNextTo(const QString& path) {
    const QFileInfo info(path);
    return info.path() + QStringLiteral("/.") + info.completeBaseName() + AUTOSAVE_SUFFIX;
}

}  // namespace

// Files for Xournal++ 1.3.8 and earlier

QStringList PageCanvas::changesForXournalpp13() const { return XoppCompat::format4Changes(m_doc); }

bool PageCanvas::saveForXournalpp13(const QUrl& url) {
    finishInput();
    QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (url.isLocalFile() && QFileInfo(path).suffix().isEmpty()) {
        path += QStringLiteral(".xopp");
    }
    QString error;
    if (!saveXopp(path, XoppCompat::toFormat4(m_doc), &error)) {
        emit saveFailed(error);
        return false;
    }
    Platform::keepAccess(path);
    return true;
}

// Autosave

void PageCanvas::setAutosaveEnabled(bool enabled) {
    if (m_autosaveEnabled != enabled) {
        m_autosaveEnabled = enabled;
        emit autosaveChanged();
    }
}

void PageCanvas::setAutosaveInterval(int minutes) {
    minutes = std::max(minutes, 1);
    if (m_autosaveMinutes != minutes) {
        m_autosaveMinutes = minutes;
        m_autosaveTimer.start(minutes * 60 * 1000);
        emit autosaveChanged();
    }
}

QString PageCanvas::autosavePath() const {
    if (!m_filePath.isEmpty() && !m_filePath.contains(u"://")) {
        return autosaveNextTo(m_filePath);
    }
    return autosaveDirectory() + u'/' + m_autosaveId + AUTOSAVE_SUFFIX;
}

void PageCanvas::autosaveNow() {
    if (!m_autosaveEnabled || !m_modified || !m_autosaveDirty || inputActive()) {
        return;
    }
    const QString path = autosavePath();
    QDir().mkpath(QFileInfo(path).path());
    // Files the document refers to stay where they are: the autosave is read in place of the document
    Document copy = m_doc;
    copy.sourcePath = path;
    QString error;
    if (!saveXopp(path, copy, &error)) {
        qWarning("Autosave failed: %s", qPrintable(error));
        return;
    }
    if (m_autosaveFile != path) {
        removeAutosave();
        m_autosaveFile = path;
    }
    m_autosaveDirty = false;
}

void PageCanvas::emergencySave() {
    if (!m_modified) {
        return;
    }
    const QString path = autosavePath();
    QDir().mkpath(QFileInfo(path).path());
    Document copy = m_doc;
    copy.sourcePath = path;
    saveXopp(path, copy, nullptr);
}

void PageCanvas::removeAutosave() {
    if (!m_autosaveFile.isEmpty()) {
        QFile::remove(m_autosaveFile);
        m_autosaveFile.clear();
    }
}

QString PageCanvas::autosaveFor(const QUrl& url) const {
    if (!url.isLocalFile()) {
        return {};
    }
    const QFileInfo document(url.toLocalFile());
    const QFileInfo autosave(autosaveNextTo(document.filePath()));
    if (autosave.exists() && autosave.lastModified() >= document.lastModified()) {
        return autosave.filePath();
    }
    return {};
}

QStringList PageCanvas::orphanAutosaves() const {
    QStringList files;
    const QDir dir(autosaveDirectory());
    for (const QFileInfo& info: dir.entryInfoList({u'*' + AUTOSAVE_SUFFIX}, QDir::Files, QDir::Time)) {
        // Not the one of the document that is open
        if (info.filePath() != m_autosaveFile) {
            files.append(info.filePath());
        }
    }
    return files;
}

bool PageCanvas::recoverAutosave(const QString& autosave, const QUrl& original) {
    QFile file(autosave);
    if (!file.open(QIODevice::ReadOnly)) {
        emit loadFailed(tr("Could not open \"%1\": %2").arg(autosave, file.errorString()));
        return false;
    }
    // The files it refers to are found from the place of the document
    const QString originalPath = original.isLocalFile() ? original.toLocalFile() : QString();
    Document doc;
    QString error;
    if (!loadXoppData(file.readAll(), originalPath, doc, &error)) {
        emit loadFailed(error);
        return false;
    }
    file.close();

    const QString title = originalPath.isEmpty() ? tr("Recovered document") : QFileInfo(originalPath).fileName();
    setDocument(std::move(doc), title, originalPath);
    // It has changes that are not in the file of the document; the autosave stays until they are saved
    m_autosaveFile = autosave;
    setModified(true);
    m_autosaveDirty = false;
    if (!originalPath.isEmpty()) {
        emit fileOpened(originalPath);
    }
    return true;
}

void PageCanvas::discardAutosave(const QString& autosave) {
    if (autosave.endsWith(AUTOSAVE_SUFFIX)) {
        QFile::remove(autosave);
    }
}

// Export

bool PageCanvas::canExportSvg() const { return Export::svgSupported(); }

bool PageCanvas::exportOptions(const QVariantMap& map, ExportOptions& options) {
    QString error;
    const QString pages = map.value(QStringLiteral("pages")).toString().trimmed();
    if (!pages.isEmpty() && !ElementRange::parse(pages, pageCount(), options.pages, &error)) {
        emit exportFailed(tr("Pages: %1").arg(error));
        return false;
    }
    const QString layers = map.value(QStringLiteral("layers")).toString().trimmed();
    if (!layers.isEmpty()) {
        size_t maxLayers = 1;
        for (const Page& page: m_doc.pages) {
            maxLayers = std::max(maxLayers, page.layers.size());
        }
        if (!ElementRange::parse(layers, static_cast<int>(maxLayers), options.layers, &error)) {
            emit exportFailed(tr("Layers: %1").arg(error));
            return false;
        }
    }
    const QString background = map.value(QStringLiteral("background")).toString();
    options.background = background == u"none"     ? ExportOptions::Background::None :
                         background == u"noRuling" ? ExportOptions::Background::NoRuling :
                                                     ExportOptions::Background::All;
    options.progressiveLayers = map.value(QStringLiteral("progressive")).toBool();
    if (map.contains(QStringLiteral("dpi")) && map.value(QStringLiteral("dpi")).toDouble() > 0) {
        options.dpi = map.value(QStringLiteral("dpi")).toDouble();
    }
    options.width = map.value(QStringLiteral("width")).toInt();
    options.height = map.value(QStringLiteral("height")).toInt();
    return true;
}

bool PageCanvas::exportDocument(const QUrl& url, const QVariantMap& map) {
    finishInput();
    ExportOptions options;
    if (!exportOptions(map, options)) {
        return false;
    }
    const QString path = url.isLocalFile() ? url.toLocalFile() : url.toString();
    QString error;
    options.format = map.value(QStringLiteral("format")).toString().toLower();
    const bool pdf = options.format.isEmpty() ? path.endsWith(u".pdf", Qt::CaseInsensitive) : options.format == u"pdf";
    const bool ok =
            pdf ? Export::toPdf(m_doc, path, options, &error) : Export::toImages(m_doc, path, options, nullptr, &error);
    if (!ok) {
        emit exportFailed(error);
    }
    return ok;
}

// Printing

bool PageCanvas::canPrint() const {
#ifdef HAVE_PRINTSUPPORT
    return true;
#else
    return false;
#endif
}

QStringList PageCanvas::printers() const {
#ifdef HAVE_PRINTSUPPORT
    return QPrinterInfo::availablePrinterNames();
#else
    return {};
#endif
}

bool PageCanvas::print(const QString& printerName, const QString& pages, const QString& outputFile) {
    return printWith({{QStringLiteral("printer"), printerName},
                      {QStringLiteral("pages"), pages},
                      {QStringLiteral("outputFile"), outputFile}});
}

bool PageCanvas::printWith(const QVariantMap& choices) {
#ifdef HAVE_PRINTSUPPORT
    finishInput();
    const QString printerName = choices.value(QStringLiteral("printer")).toString();
    const QString outputFile = choices.value(QStringLiteral("outputFile")).toString();
    ExportOptions options;
    if (!exportOptions({{QStringLiteral("pages"), choices.value(QStringLiteral("pages"))}}, options)) {
        return false;
    }
    const std::vector<Export::Sheet> sheets = Export::sheets(m_doc, options);
    if (sheets.empty()) {
        return false;
    }

    QPrinter printer(QPrinter::HighResolution);
    if (!outputFile.isEmpty()) {
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(outputFile);
    } else if (!printerName.isEmpty()) {
        printer.setPrinterName(printerName);
    }
    if (outputFile.isEmpty() && !printer.isValid()) {
        emit exportFailed(tr("No printer is available"));
        return false;
    }
    printer.setDocName(m_title);
    printer.setFullPage(true);
    printer.setCopyCount(std::clamp(choices.value(QStringLiteral("copies"), 1).toInt(), 1, 999));
    const QString duplex = choices.value(QStringLiteral("duplex")).toString();
    printer.setDuplex(duplex == u"longSide"  ? QPrinter::DuplexLongSide :
                      duplex == u"shortSide" ? QPrinter::DuplexShortSide :
                                               QPrinter::DuplexNone);
    printer.setColorMode(choices.value(QStringLiteral("grayscale")).toBool() ? QPrinter::GrayScale : QPrinter::Color);
    // Sheets of the size of the pages, or the paper of the printer
    const bool ownPaper = choices.value(QStringLiteral("paper")).toString() == u"printer";
    auto setPageSize = [&](const Page& page) {
        if (ownPaper) {
            printer.setPageOrientation(page.width > page.height ? QPageLayout::Landscape : QPageLayout::Portrait);
        } else {
            printer.setPageSize(QPageSize(QSizeF(page.width, page.height), QPageSize::Point));
        }
        printer.setPageMargins(QMarginsF(0, 0, 0, 0));
    };
    setPageSize(sheets.front().content);

    QPainter p;
    if (!p.begin(&printer)) {
        emit exportFailed(tr("Could not start printing"));
        return false;
    }
    for (size_t i = 0; i < sheets.size(); ++i) {
        const Page& page = sheets[i].content;
        if (i > 0) {
            setPageSize(page);
            printer.newPage();
        }
        // The page is as large as fits on the sheet, in its middle
        const QRectF paper = printer.pageLayout().fullRectPixels(printer.resolution());
        const double scale = std::min(paper.width() / page.width, paper.height() / page.height);
        p.save();
        p.translate((paper.width() - page.width * scale) / 2, (paper.height() - page.height * scale) / 2);
        p.scale(scale, scale);
        Export::paintSheet(p, m_doc, sheets[i], options, std::min(printer.resolution(), 300));
        p.restore();
    }
    p.end();
    return true;
#else
    Q_UNUSED(choices)
    emit exportFailed(tr("This build cannot print: it was built without Qt Print Support"));
    return false;
#endif
}
