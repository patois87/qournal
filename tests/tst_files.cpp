/*
 * Qournal
 *
 * Tests for what the canvas does with files besides loading and saving: export, printing, autosave and the list of
 * recent files. They run without a display (QT_QPA_PLATFORM=offscreen, QT_QUICK_BACKEND=software).
 *
 * @license GNU GPLv2 or later
 */

#include <QPainter>
#include <QPdfWriter>
#include <QProcess>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <csignal>

#ifdef HAVE_QTPDF
#include <QPdfDocument>
#endif

#include "CanvasFixture.h"
#include "CrashHandler.h"
#include "RecentFiles.h"
#include "XoppCompat.h"
#include "XoppLoader.h"

namespace {

QStringList autosavesIn(const QString& dir) {
    return QDir(dir).entryList({QStringLiteral("*.autosave.xopp")}, QDir::Files | QDir::Hidden);
}

QString cacheAutosaves() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/autosaves");
}

int strokeCount(const QString& path) {
    Document doc;
    if (!loadXopp(path, doc, nullptr)) {
        return -1;
    }
    int count = 0;
    for (const Page& page: doc.pages) {
        for (const Layer& layer: page.layers) {
            count += static_cast<int>(layer.elements.size());
        }
    }
    return count;
}

}  // namespace

class TestFiles: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        // Nothing of the user is read or written
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("qournal-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_files"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QVERIFY(m_settingsDir.isValid());
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settingsDir.path());
    }

    void init() {
        QSettings().clear();
        QDir(cacheAutosaves()).removeRecursively();
    }

    void cleanupTestCase() { QDir(cacheAutosaves()).removeRecursively(); }

    void exportFromCanvas() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QTemporaryDir dir;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QCOMPARE(f.strokes().size(), 1);
        QSignalSpy failed(c, &PageCanvas::exportFailed);

        const QString png = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(c->exportDocument(QUrl::fromLocalFile(png), {{QStringLiteral("dpi"), 72},
                                                             {QStringLiteral("background"), QStringLiteral("none")}}));
        const QImage image(png);
        QCOMPARE(image.size(), QSize(595, 842));
        COMPARE_COLOR(image.pixelColor(200, 100), PEN_COLOR);
        QCOMPARE(image.pixelColor(200, 300).alpha(), 0);

#ifdef HAVE_QTPDF
        c->insertPage(1);
        const QString pdf = dir.filePath(QStringLiteral("doc.pdf"));
        QVERIFY(c->exportDocument(QUrl::fromLocalFile(pdf), {}));
        QPdfDocument exported;
        QCOMPARE(exported.load(pdf), QPdfDocument::Error::None);
        QCOMPARE(exported.pageCount(), 2);
        QVERIFY(c->exportDocument(QUrl::fromLocalFile(pdf), {{ QStringLiteral("pages"), QStringLiteral("2") }}));
        QPdfDocument one;
        QCOMPARE(one.load(pdf), QPdfDocument::Error::None);
        QCOMPARE(one.pageCount(), 1);
#endif
        QCOMPARE(failed.count(), 0);

        // A range that does not fit is reported, and nothing is written
        const QString wrong = dir.filePath(QStringLiteral("wrong.png"));
        QVERIFY(!c->exportDocument(QUrl::fromLocalFile(wrong), {{QStringLiteral("pages"), QStringLiteral("7-9")}}));
        QCOMPARE(failed.count(), 1);
        QVERIFY(!QFile::exists(wrong));
        QVERIFY(!c->exportDocument(QUrl::fromLocalFile(wrong), {{QStringLiteral("layers"), QStringLiteral("x")}}));
        QCOMPARE(failed.count(), 2);
    }

    void copyForXournalpp13() {
        Fixture f;
        PageCanvas* c = f.canvas;
        const QString source = QStringLiteral(TEST_DATA_DIR "/links-fileversion-5.xopp");
        c->openFile(QUrl::fromLocalFile(source));
        QVERIFY(c->hasFile());
        QVERIFY(!c->changesForXournalpp13().isEmpty());

        QTemporaryDir dir;
        const QString copy = dir.filePath(QStringLiteral("old"));
        QVERIFY(c->saveForXournalpp13(QUrl::fromLocalFile(copy)));
        // The document keeps its file and its links
        QCOMPARE(c->filePath(), source);
        QVERIFY(XoppCompat::needsFormat5(c->document()));
        Document saved;
        QVERIFY(loadXopp(copy + QStringLiteral(".xopp"), saved, nullptr));
        QVERIFY(!XoppCompat::needsFormat5(saved));
    }

    void locationsWithoutExtension() {
        // On Android a document is a content:// URI, which need not end in ".pdf" or ".png"
        Fixture f;
        PageCanvas* c = f.canvas;
        QTemporaryDir dir;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->insertPage(1);

        const QString image = dir.filePath(QStringLiteral("image"));
        QVERIFY(c->exportDocument(QUrl::fromLocalFile(image), {{QStringLiteral("format"), QStringLiteral("png")},
                                                               {QStringLiteral("pages"), QStringLiteral("1")},
                                                               {QStringLiteral("dpi"), 72}}));
        QCOMPARE(QImage(image, "PNG").size(), QSize(595, 842));

        const QString pdf = dir.filePath(QStringLiteral("document"));
        QVERIFY(c->exportDocument(QUrl::fromLocalFile(pdf), {{QStringLiteral("format"), QStringLiteral("pdf")}}));
        QFile exported(pdf);
        QVERIFY(exported.open(QIODevice::ReadOnly));
        QVERIFY(exported.read(5) == "%PDF-");
        exported.close();

        // Several pages as images need a folder, which such a location does not have
        QSignalSpy failed(c, &PageCanvas::exportFailed);
        QVERIFY(!c->exportDocument(QUrl(QStringLiteral("content://provider/document/7")),
                                   {{QStringLiteral("format"), QStringLiteral("png")}}));
        QCOMPARE(failed.count(), 1);

#ifdef HAVE_QTPDF
        // A PDF is recognised by its content
        QSignalSpy loadFailed(c, &PageCanvas::loadFailed);
        c->openFile(QUrl::fromLocalFile(pdf));
        QCOMPARE(loadFailed.count(), 0);
        QCOMPARE(c->pageCount(), 2);
        QCOMPARE(c->pdfPageCount(), 2);
        QVERIFY(!c->hasFile());  // a PDF to annotate, not a document of its own
#endif
    }

    void printToFile() {
        Fixture f;
        PageCanvas* c = f.canvas;
        if (!c->canPrint()) {
            QVERIFY(!c->print(QString(), QString(), QStringLiteral("unused.pdf")));
            QSKIP("Built without Qt Print Support");
        }
#ifdef HAVE_QTPDF
        QTemporaryDir dir;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->insertPage(1);
        c->insertPage(2);
        const QString path = dir.filePath(QStringLiteral("printed.pdf"));
        QVERIFY(c->print(QString(), QStringLiteral("1-2"), path));

        QPdfDocument pdf;
        QCOMPARE(pdf.load(path), QPdfDocument::Error::None);
        QCOMPARE(pdf.pageCount(), 2);
        const QSizeF size = pdf.pagePointSize(0);
        QVERIFY2(std::abs(size.width() - 595) < 2 && std::abs(size.height() - 842) < 2,
                 qPrintable(QStringLiteral("%1 x %2").arg(size.width()).arg(size.height())));
        // The page fills the sheet
        const QImage page = pdf.render(0, QSize(595, 842));
        COMPARE_COLOR(page.pixelColor(200, 100), PEN_COLOR);
        COMPARE_COLOR(page.pixelColor(200, 300), Qt::white);

        // On the paper of the printer (A4 for a PDF file): a small page is fitted, in the middle of the sheet
        c->setPageProperties(0, {{QStringLiteral("width"), 595.0}, {QStringLiteral("height"), 421.0}});
        const QString fitted = dir.filePath(QStringLiteral("fitted.pdf"));
        QVERIFY(c->printWith({{QStringLiteral("outputFile"), fitted},
                              {QStringLiteral("pages"), QStringLiteral("1")},
                              {QStringLiteral("paper"), QStringLiteral("printer")},
                              {QStringLiteral("copies"), 2},
                              { QStringLiteral("duplex"),
                                QStringLiteral("longSide") }}));
        QPdfDocument sheet;
        QCOMPARE(sheet.load(fitted), QPdfDocument::Error::None);
        const QSizeF paper = sheet.pagePointSize(0);
        QVERIFY2(std::abs(paper.width() - 842) < 2 && std::abs(paper.height() - 595) < 2,  // landscape, as the page
                 qPrintable(QStringLiteral("%1 x %2").arg(paper.width()).arg(paper.height())));
        // The page is 595 x 421: scaled by 842 / 595, so the stroke at (200, 100) is at (283, 141)
        const QImage fittedImage = sheet.render(0, QSize(842, 595));
        COMPARE_COLOR(fittedImage.pixelColor(283, 141), PEN_COLOR);

        QSignalSpy failed(c, &PageCanvas::exportFailed);
        QVERIFY(!c->print(QString(), QStringLiteral("9"), path));
        QCOMPARE(failed.count(), 1);

        // The pages of a PDF are printed as vectors, as Xournal++ prints them: no image in the printed file
        const QString source = dir.filePath(QStringLiteral("source.pdf"));
        {
            QPdfWriter writer(source);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 500), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            QFont font(QStringLiteral("DejaVu Sans"));
            font.setPixelSize(40);
            p.setFont(font);
            p.drawText(QPointF(40, 100), QStringLiteral("Vectors"));
            p.fillRect(QRectF(40, 200, 200, 100), Qt::black);
        }
        c->openFile(QUrl::fromLocalFile(source));
        const QString printedPdf = dir.filePath(QStringLiteral("printedPdf.pdf"));
        QVERIFY(c->print(QString(), QString(), printedPdf));
        QFile printedFile(printedPdf);
        QVERIFY(printedFile.open(QIODevice::ReadOnly));
        const QByteArray printedData = printedFile.readAll();
        QVERIFY(!printedData.contains("/Subtype /Image") && !printedData.contains("/Subtype/Image"));
        QPdfDocument printedDoc;
        QCOMPARE(printedDoc.load(printedPdf), QPdfDocument::Error::None);
        const QImage printedImage = printedDoc.render(0, printedDoc.pagePointSize(0).toSize());
        // The black box, scaled from 400 x 500 onto the sheet
        const double scale = std::min(printedImage.width() / 400.0, printedImage.height() / 500.0);
        const QPointF offset((printedImage.width() - 400 * scale) / 2, (printedImage.height() - 500 * scale) / 2);
        COMPARE_COLOR(printedImage.pixelColor((offset + QPointF(140, 250) * scale).toPoint()), Qt::black);
#endif
    }

    void autosaveOfNewDocument() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QVERIFY(c->autosaveEnabled());

        // Nothing to save yet
        c->autosaveNow();
        QVERIFY(autosavesIn(cacheAutosaves()).isEmpty());

        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        c->autosaveNow();
        QCOMPARE(autosavesIn(cacheAutosaves()).size(), 1);
        const QString autosave = c->autosavePath();
        QCOMPARE(strokeCount(autosave), 1);
        QVERIFY(c->modified());                   // the autosave does not count as saving
        QVERIFY(c->orphanAutosaves().isEmpty());  // it belongs to the open document

        // Unchanged since: not written again
        const QDateTime written = QFileInfo(autosave).lastModified();
        QTest::qWait(20);
        c->autosaveNow();
        QCOMPARE(QFileInfo(autosave).lastModified(), written);

        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        c->autosaveNow();
        QCOMPARE(strokeCount(autosave), 2);

        // Saving the document makes the autosave obsolete
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("saved.xopp"));
        QSignalSpy saved(c, &PageCanvas::fileSaved);
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        QCOMPARE(saved.count(), 1);
        QCOMPARE(saved[0][0].toString(), path);
        QVERIFY(!QFile::exists(autosave));
        QVERIFY(autosavesIn(cacheAutosaves()).isEmpty());

        // From now on it lies next to the document, hidden
        f.strokeOnPage(0, {QPointF(100, 300), QPointF(300, 300)});
        c->autosaveNow();
        QCOMPARE(c->autosavePath(), dir.filePath(QStringLiteral(".saved.autosave.xopp")));
        QCOMPARE(autosavesIn(dir.path()), QStringList{QStringLiteral(".saved.autosave.xopp")});
        QCOMPARE(strokeCount(c->autosavePath()), 3);
        QCOMPARE(strokeCount(path), 2);

        // Disabled: nothing is written
        QVERIFY(c->save());
        QVERIFY(autosavesIn(dir.path()).isEmpty());
        c->setAutosaveEnabled(false);
        f.strokeOnPage(0, {QPointF(100, 400), QPointF(300, 400)});
        c->autosaveNow();
        QVERIFY(autosavesIn(dir.path()).isEmpty());
    }

    void autosaveIsRemovedOnRegularEnd() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.xopp"));
        {
            Fixture f;
            QVERIFY(f.canvas->saveAs(QUrl::fromLocalFile(path)));
            f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
            f.canvas->autosaveNow();
            QCOMPARE(autosavesIn(dir.path()).size(), 1);
            // A new document: the changes were given up
            f.canvas->newDocument();
            QVERIFY(autosavesIn(dir.path()).isEmpty());

            f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
            f.canvas->autosaveNow();
            QCOMPARE(autosavesIn(cacheAutosaves()).size(), 1);
        }
        // Closing the window removes it
        QVERIFY(autosavesIn(cacheAutosaves()).isEmpty());
    }

    void recoverAfterCrash() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.xopp"));
        const QUrl url = QUrl::fromLocalFile(path);
        const QString autosave = dir.filePath(QStringLiteral(".doc.autosave.xopp"));
        {
            Fixture f;
            QVERIFY(f.canvas->saveAs(url));
            QVERIFY(f.canvas->autosaveFor(url).isEmpty());
            f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
            f.canvas->autosaveNow();
            // The session ends without removing it, as a crash does
            QVERIFY(QFile::copy(autosave, autosave + QStringLiteral(".kept")));
        }
        QVERIFY(!QFile::exists(autosave));
        QVERIFY(QFile::rename(autosave + QStringLiteral(".kept"), autosave));

        Fixture f;
        PageCanvas* c = f.canvas;
        QCOMPARE(c->autosaveFor(url), autosave);
        QSignalSpy opened(c, &PageCanvas::fileOpened);
        QVERIFY(c->recoverAutosave(autosave, url));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(f.strokes().size(), 1);
        // It is the document, with changes that are not saved
        QCOMPARE(c->filePath(), path);
        QVERIFY(c->hasFile());
        QVERIFY(c->modified());
        QVERIFY(QFile::exists(autosave));
        QCOMPARE(strokeCount(path), 0);

        QVERIFY(c->save());
        QCOMPARE(strokeCount(path), 1);
        QVERIFY(!QFile::exists(autosave));
        QVERIFY(c->autosaveFor(url).isEmpty());
    }

    void outdatedAndDiscardedAutosaves() {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("doc.xopp"));
        const QUrl url = QUrl::fromLocalFile(path);
        const QString autosave = dir.filePath(QStringLiteral(".doc.autosave.xopp"));
        Fixture f;
        PageCanvas* c = f.canvas;
        QVERIFY(c->saveAs(url));
        QVERIFY(QFile::copy(path, autosave));

        // Older than the document: not offered
        QFile file(autosave);
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QFileInfo(path).lastModified().addSecs(-60), QFileDevice::FileModificationTime));
        file.close();
        QVERIFY(c->autosaveFor(url).isEmpty());

        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.setFileTime(QFileInfo(path).lastModified().addSecs(60), QFileDevice::FileModificationTime));
        file.close();
        QCOMPARE(c->autosaveFor(url), autosave);

        c->discardAutosave(autosave);
        QVERIFY(!QFile::exists(autosave));
        QVERIFY(c->autosaveFor(url).isEmpty());

        // Only autosave files are deleted this way
        c->discardAutosave(path);
        QVERIFY(QFile::exists(path));

        // An autosave that cannot be read is reported
        QSignalSpy failed(c, &PageCanvas::loadFailed);
        QVERIFY(!c->recoverAutosave(autosave, url));
        QCOMPARE(failed.count(), 1);
    }

    void orphanOfUnsavedDocument() {
        QString orphan;
        {
            Fixture f;
            f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
            f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
            f.canvas->autosaveNow();
            orphan = cacheAutosaves() + QStringLiteral("/crashed.autosave.xopp");
            QVERIFY(QFile::copy(f.canvas->autosavePath(), orphan));
        }
        Fixture f;
        PageCanvas* c = f.canvas;
        QCOMPARE(c->orphanAutosaves(), QStringList{orphan});
        QVERIFY(c->recoverAutosave(orphan, QUrl()));
        QCOMPARE(f.strokes().size(), 2);
        QVERIFY(!c->hasFile());
        QVERIFY(c->modified());
        // It is the autosave of this document now, until it is saved
        QVERIFY(c->orphanAutosaves().isEmpty());
        QVERIFY(QFile::exists(orphan));

        QTemporaryDir dir;
        QVERIFY(c->saveAs(QUrl::fromLocalFile(dir.filePath(QStringLiteral("recovered.xopp")))));
        QVERIFY(!QFile::exists(orphan));
    }

    void crash() {
#if QT_CONFIG(process)
        QTemporaryDir dir;
        const QString logs = CrashHandler::logFolder();
        QDir(logs).removeRecursively();
        auto crashWith = [&](const QString& document) {
            QProcess process;
            QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
            environment.insert(QStringLiteral("XOJ_TEST_CRASH"), document);
            process.setProcessEnvironment(environment);
            process.start(QCoreApplication::applicationFilePath(), QStringList());
            if (!process.waitForFinished(60000)) {
                return false;
            }
#ifdef Q_OS_WIN
            // What Windows does with a signal nobody handles is to end the process with the exit code 3
            return process.exitStatus() == QProcess::CrashExit || process.exitCode() == 3;
#else
            return process.exitStatus() == QProcess::CrashExit;
#endif
        };

        // A document that was never saved: found at the next start
        QVERIFY(crashWith(QString()));
        {
            Fixture f;
            const QStringList orphans = f.canvas->orphanAutosaves();
            QCOMPARE(orphans.size(), 1);
            QCOMPARE(strokeCount(orphans.first()), 2);
            QVERIFY(f.canvas->recoverAutosave(orphans.first(), QUrl()));
            QCOMPARE(f.strokes().size(), 2);
        }
        // A note about the crash is in the log folder
        const QStringList notes = QDir(logs).entryList({QStringLiteral("errorlog.*.log")}, QDir::Files);
        QCOMPARE(notes.size(), 1);
        QFile note(logs + u'/' + notes.first());
        QVERIFY(note.open(QIODevice::ReadOnly));
        const QByteArray text = note.readAll();
        QVERIFY(text.contains("SIGSEGV"));
#if defined(Q_OS_LINUX) || defined(Q_OS_WIN)
        // With the calls that led to the crash
        const QByteArray stack = text.mid(text.indexOf("Stack:"));
        QVERIFY2(stack.count('\n') > 3, stack.constData());
#endif
        // The next start tells about it, once
        const QStringList reported = CrashHandler::takeNewLogs();
        QCOMPARE(reported.size(), 1);
        QVERIFY(reported.first().endsWith(notes.first()));
        QVERIFY(CrashHandler::takeNewLogs().isEmpty());

        // A document with a file: its autosave is next to it, and offered when it is opened
        const QString path = dir.filePath(QStringLiteral("doc.xopp"));
        QVERIFY(crashWith(path));
        QCOMPARE(strokeCount(path), 0);
        Fixture f;
        const QString autosave = f.canvas->autosaveFor(QUrl::fromLocalFile(path));
        QVERIFY(!autosave.isEmpty());
        QCOMPARE(strokeCount(autosave), 2);
#else
        QSKIP("Needs processes");
#endif
    }

    void recentFiles() {
        QTemporaryDir dir;
        QStringList paths;
        for (int i = 0; i < RecentFiles::MAX_FILES + 2; ++i) {
            paths.append(dir.filePath(QStringLiteral("file %1.xopp").arg(i)));
            QFile file(paths.last());
            QVERIFY(file.open(QIODevice::WriteOnly));
        }

        RecentFiles recent;
        // The URL of an entry: a path of Windows is not a network drive "//c/...", and a URL stays one
        for (const QString& path:
             {QStringLiteral("/home/a b/x#1.xopp"), QStringLiteral("C:/Users/Some User/Desktop/notes.xopp")}) {
            const QUrl url = recent.locationUrl(path);
            QVERIFY(url.isLocalFile());
            QVERIFY(url.host().isEmpty());
#ifndef Q_OS_WIN
            if (path.at(1) == u':') {
                // A path with a drive is not one of this system; the name of the drive is not a host, at least
                QVERIFY(url.toLocalFile().endsWith(path));
                continue;
            }
#endif
            QCOMPARE(url.toLocalFile(), path);
        }
        QCOMPARE(recent.locationUrl(QStringLiteral("content://provider/document/7")),
                 QUrl(QStringLiteral("content://provider/document/7")));
        QSignalSpy changed(&recent, &RecentFiles::changed);
        QVERIFY(recent.files().isEmpty());
        recent.add(paths[0]);
        recent.add(paths[1]);
        recent.add(paths[0]);  // moves to the front, without a second entry
        QCOMPARE(recent.files(), (QStringList{paths[0], paths[1]}));
        QCOMPARE(changed.count(), 3);
        QCOMPARE(recent.fileName(paths[0]), QStringLiteral("file 0.xopp"));

        // The page a document was left at
        QCOMPARE(recent.pageOf(paths[0]), 0);
        recent.setPage(paths[0], 7);
        recent.setPage(paths[1], 3);
        QCOMPARE(recent.pageOf(paths[0]), 7);

        // Another instance sees the same: it is stored in the settings
        QCOMPARE(RecentFiles().files(), (QStringList{paths[0], paths[1]}));
        QCOMPARE(RecentFiles().pageOf(paths[1]), 3);

        // Files that are gone are left out
        QVERIFY(QFile::remove(paths[1]));
        QCOMPARE(recent.files(), QStringList{paths[0]});

        // The list is limited; what falls out forgets its page
        for (int i = 2; i < paths.size(); ++i) {
            recent.add(paths[i]);
        }
        QCOMPARE(recent.files().size(), RecentFiles::MAX_FILES);
        QCOMPARE(recent.files().first(), paths.last());
        QVERIFY(!recent.files().contains(paths[0]));
        QCOMPARE(recent.pageOf(paths[0]), 0);

        recent.clear();
        QVERIFY(recent.files().isEmpty());
        QCOMPARE(recent.pageOf(paths[1]), 0);
    }

private:
    QTemporaryDir m_settingsDir;
};

// The test of a crash runs this program a second time, with a document that has unsaved changes, and lets it crash
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("XOJ_TEST_CRASH")) {
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("qournal-test"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_files"));
        CrashHandler::install();
        Fixture f;
        const QString document = qEnvironmentVariable("XOJ_TEST_CRASH");
        if (!document.isEmpty()) {
            f.canvas->saveAs(QUrl::fromLocalFile(document));
        }
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        std::raise(SIGSEGV);
        return 0;
    }
    TestFiles test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_files.moc"
