/*
 * Qournal
 *
 * Tests against Xournal++ itself: files written here are opened by Xournal++, and the test files of Xournal++
 * are drawn here as Xournal++ draws them. Xournal++ exports both to PDF (--create-pdf), which is compared with
 * what this application draws.
 *
 * Runs only if the environment variable XOURNALPP names the program of Xournal++ (e.g. the AppRun of an unpacked
 * AppImage). Without a screen, GTK can draw to a Broadway server: GDK_BACKEND=broadway BROADWAY_DISPLAY=:7 with
 * "broadwayd :7" running.
 *
 * @license GNU GPLv2 or later
 */

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfWriter>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include "Document.h"
#include "Export.h"
#include "Format5Document.h"
#include "XoppCompat.h"
#include "XoppLoader.h"
#include "XoppWriter.h"

namespace {

QString dataFile(const QString& name) { return QStringLiteral(TEST_DATA_DIR "/") + name; }

/// The share of pixels that clearly differ, after both images were made smaller
double difference(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) {
        return 1;
    }
    const QImage x = a.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    const QImage y = b.scaled(a.size() / 2, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                             .convertToFormat(QImage::Format_RGB32);
    qint64 different = 0;
    for (int row = 0; row < x.height(); ++row) {
        const auto* p = reinterpret_cast<const QRgb*>(x.constScanLine(row));
        const auto* q = reinterpret_cast<const QRgb*>(y.constScanLine(row));
        for (int col = 0; col < x.width(); ++col) {
            const int d = std::max({std::abs(qRed(p[col]) - qRed(q[col])), std::abs(qGreen(p[col]) - qGreen(q[col])),
                                    std::abs(qBlue(p[col]) - qBlue(q[col]))});
            different += d > 80;
        }
    }
    return static_cast<double>(different) / (x.width() * x.height());
}

QImage onWhite(const QImage& image) {
    QImage result(image.size(), QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::white);
    QPainter p(&result);
    p.drawImage(0, 0, image);
    return result;
}

constexpr double SCALE = 2;  ///< pixels per point of the images that are compared

/// The pages of a file as Xournal++ draws them: exported to PDF by Xournal++, drawn by Qt PDF
QList<QImage> xournalppPages(const QString& file, const QString& folder, QString* error) {
    const QString program = qEnvironmentVariable("XOURNALPP");
    const QString pdf = folder + u'/' + QFileInfo(file).completeBaseName() + QStringLiteral(".xournalpp.pdf");
    QFile::remove(pdf);
    QProcess process;
    process.start(program, {file, QStringLiteral("--create-pdf=") + pdf});
    if (!process.waitForFinished(120000) || !QFile::exists(pdf)) {
        *error = QString::fromUtf8(process.readAllStandardError()) + QString::fromUtf8(process.readAllStandardOutput());
        return {};
    }
    QPdfDocument document;
    document.load(pdf);
    QList<QImage> pages;
    for (int i = 0; i < document.pageCount(); ++i) {
        pages.append(onWhite(document.render(i, (document.pagePointSize(i) * SCALE).toSize())));
    }
    return pages;
}

/// The pages of a document as this application draws them
QList<QImage> ownPages(const Document& doc, const QString& folder) {
    ExportOptions options;
    options.dpi = 72 * SCALE;
    QStringList written;
    QString error;
    if (!Export::toImages(doc, folder + QStringLiteral("/own.png"), options, &written, &error)) {
        return {};
    }
    QList<QImage> pages;
    for (const QString& file: written) {
        pages.append(onWhite(QImage(file)));
        QFile::remove(file);
    }
    return pages;
}

QStringList testFiles() {
    QStringList files;
    for (const QString& name: QDir(QStringLiteral(TEST_DATA_DIR)).entryList({QStringLiteral("*.xopp")})) {
        files.append(name);
    }
    return files;
}

}  // namespace

class TestXournalpp: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        if (qEnvironmentVariable("XOURNALPP").isEmpty()) {
            QSKIP("XOURNALPP is not set: no Xournal++ to test against");
        }
    }

    /// Files saved here are opened by Xournal++ as the originals are: everything in them is understood
    void savedFilesOpenInXournalpp_data() {
        QTest::addColumn<QString>("file");
        for (const QString& name: testFiles()) {
            QTest::newRow(qPrintable(name)) << name;
        }
    }

    void savedFilesOpenInXournalpp() {
        QFETCH(QString, file);
        QTemporaryDir dir;
        // Next to the files they refer to (images, backgrounds)
        for (const QString& name: QDir(QStringLiteral(TEST_DATA_DIR)).entryList(QDir::Files)) {
            QFile::copy(dataFile(name), dir.filePath(name));
        }
        Document doc;
        QString error;
        QVERIFY2(loadXopp(dir.filePath(file), doc, &error), qPrintable(error));
        const QString saved = dir.filePath(QStringLiteral("saved-") + file);
        QVERIFY2(saveXopp(saved, doc, &error), qPrintable(error));

        const QList<QImage> original = xournalppPages(dir.filePath(file), dir.path(), &error);
        if (original.isEmpty()) {
            // A file of a newer version, or one whose PDF is not there: Xournal++ cannot export it itself
            QSKIP("Xournal++ cannot export the original");
        }
        const QList<QImage> ours = xournalppPages(saved, dir.path(), &error);
        QVERIFY2(!ours.isEmpty(), qPrintable(error));
        QCOMPARE(ours.size(), original.size());
        for (qsizetype i = 0; i < ours.size(); ++i) {
            const double d = difference(ours[i], original[i]);
            qInfo("%s page %lld: %.5f", qPrintable(file), static_cast<long long>(i + 1), d);
            if (qEnvironmentVariableIsSet("XOURNALPP_IMAGES")) {
                const QString keep = qEnvironmentVariable("XOURNALPP_IMAGES") + u'/' + file;
                ours[i].save(keep + QStringLiteral("-%1-saved.png").arg(i + 1));
                original[i].save(keep + QStringLiteral("-%1-original.png").arg(i + 1));
            }
            QVERIFY2(d < 0.002, qPrintable(QStringLiteral("page %1 differs: %2").arg(i + 1).arg(d)));
        }
    }

    /// A document with what can be made here: every background, line styles, the highlighter, pressure, filling,
    /// a PDF and an image as backgrounds. Saved here, opened by Xournal++, it looks there as it looks here
    void ownDocument() {
        QTemporaryDir dir;
        const QString pdf = dir.filePath(QStringLiteral("background.pdf"));
        {
            QPdfWriter writer(pdf);
            writer.setResolution(72);
            writer.setPageSize(QPageSize(QSizeF(400, 500), QPageSize::Point));
            writer.setPageMargins(QMarginsF(0, 0, 0, 0));
            QPainter p(&writer);
            p.fillRect(QRectF(40, 40, 200, 100), QColor(0x20, 0x80, 0xd0));
        }
        QImage pixmap(80, 60, QImage::Format_RGB32);
        pixmap.fill(QColor(0xff, 0xd0, 0x40));
        QVERIFY(pixmap.save(dir.filePath(QStringLiteral("background.png"))));

        QByteArray pages;
        const char* styles[] = {"plain",  "lined",  "ruled",          "graph",
                                "dotted", "staves", "isometricgraph", "isometricdotted"};
        for (const char* style: styles) {
            pages += "<page width=\"400\" height=\"500\"><background type=\"solid\" color=\"#fafaf0ff\" style=\"" +
                     QByteArray(style) +
                     "\"/><layer>"
                     "<stroke tool=\"pen\" color=\"#c00000ff\" width=\"2 1 3 5 2\">50 300 120 320 180 280 260 330 "
                     "330 300</stroke>"
                     "<stroke tool=\"pen\" color=\"#0000c0ff\" width=\"3\" style=\"dash\">50 360 330 360</stroke>"
                     "<stroke tool=\"pen\" color=\"#008000ff\" width=\"2\" style=\"dot\">50 380 330 380</stroke>"
                     "<stroke tool=\"pen\" color=\"#000000ff\" width=\"2\" style=\"cust: 8 3 1 3\">50 400 330 400"
                     "</stroke>"
                     "<stroke tool=\"highlighter\" color=\"#ffff007f\" width=\"12\">50 430 330 430</stroke>"
                     "<stroke tool=\"pen\" color=\"#800080ff\" width=\"2\" fill=\"128\">60 60 160 60 160 160 60 160 "
                     "60 60</stroke>"
                     "<text font=\"Sans\" size=\"16\" x=\"200\" y=\"80\" color=\"#000000ff\">Text, äöü</text>"
                     "</layer></page>";
        }
        pages += "<page width=\"400\" height=\"500\"><background type=\"pdf\" domain=\"absolute\" filename=\"" +
                 pdf.toUtf8() +
                 "\" pageno=\"1\"/><layer><stroke tool=\"pen\" color=\"#c00000ff\" width=\"2\">"
                 "50 300 330 300</stroke></layer><layer name=\"Second\"><stroke tool=\"pen\" color=\"#0000c0ff\" "
                 "width=\"2\">50 320 330 320</stroke></layer></page>";
        pages += "<page width=\"400\" height=\"500\"><background type=\"pixmap\" domain=\"absolute\" filename=\"" +
                 dir.filePath(QStringLiteral("background.png")).toUtf8() +
                 "\"/><layer><stroke tool=\"pen\" color=\"#000000ff\" width=\"2\">50 300 330 300</stroke></layer>"
                 "</page>";
        const QByteArray xml = "<?xml version=\"1.0\" standalone=\"no\"?>\n<xournal creator=\"test\" "
                               "fileversion=\"4\">" +
                               pages + "</xournal>\n";
        const QString source = dir.filePath(QStringLiteral("source.xml"));
        QFile file(source);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(xml);
        file.close();

        Document doc;
        QString error;
        QVERIFY2(loadXopp(source, doc, &error), qPrintable(error));
        QCOMPARE(doc.pages.size(), std::size(styles) + 2);
        const QString saved = dir.filePath(QStringLiteral("own.xopp"));
        QVERIFY2(saveXopp(saved, doc, &error), qPrintable(error));
        Document again;
        QVERIFY2(loadXopp(saved, again, &error), qPrintable(error));

        const QList<QImage> theirs = xournalppPages(saved, dir.path(), &error);
        QVERIFY2(!theirs.isEmpty(), qPrintable(error));
        const QList<QImage> ours = ownPages(again, dir.path());
        QCOMPARE(ours.size(), theirs.size());
        for (qsizetype i = 0; i < ours.size(); ++i) {
            const double d = difference(ours[i], theirs[i]);
            qInfo("page %lld: %.5f", static_cast<long long>(i + 1), d);
            if (qEnvironmentVariableIsSet("XOURNALPP_IMAGES")) {
                const QString keep = qEnvironmentVariable("XOURNALPP_IMAGES") + QStringLiteral("/own");
                ours[i].save(keep + QStringLiteral("-%1-ours.png").arg(i + 1));
                theirs[i].save(keep + QStringLiteral("-%1-theirs.png").arg(i + 1));
            }
            QVERIFY2(d < 0.01, qPrintable(QStringLiteral("page %1 differs: %2").arg(i + 1).arg(d)));
        }
    }

    /// A copy for Xournal++ 1.3 of a document with links and turned texts and images: Xournal++ opens it, and it
    /// looks there as the document looks here
    void copyForXournalpp13() {
        QTemporaryDir dir;
        const Document doc = format5Document();
        const QString saved = dir.filePath(QStringLiteral("old.xopp"));
        QString error;
        QVERIFY2(saveXopp(saved, XoppCompat::toFormat4(doc), &error), qPrintable(error));
        const QList<QImage> theirs = xournalppPages(saved, dir.path(), &error);
        QVERIFY2(!theirs.isEmpty(), qPrintable(error));
        const QList<QImage> ours = ownPages(doc, dir.path());
        QCOMPARE(ours.size(), theirs.size());
        const double d = difference(ours[0], theirs[0]);
        qInfo("difference: %.5f", d);
        if (qEnvironmentVariableIsSet("XOURNALPP_IMAGES")) {
            const QString keep = qEnvironmentVariable("XOURNALPP_IMAGES") + QStringLiteral("/old");
            ours[0].save(keep + QStringLiteral("-ours.png"));
            theirs[0].save(keep + QStringLiteral("-theirs.png"));
        }
        QVERIFY2(d < 0.01, qPrintable(QString::number(d)));
    }

    /// The test files of Xournal++ look here as they look in Xournal++
    void drawnAsInXournalpp_data() { savedFilesOpenInXournalpp_data(); }

    void drawnAsInXournalpp() {
        QFETCH(QString, file);
        QTemporaryDir dir;
        for (const QString& name: QDir(QStringLiteral(TEST_DATA_DIR)).entryList(QDir::Files)) {
            QFile::copy(dataFile(name), dir.filePath(name));
        }
        Document doc;
        QString error;
        QVERIFY2(loadXopp(dir.filePath(file), doc, &error), qPrintable(error));
        const QList<QImage> theirs = xournalppPages(dir.filePath(file), dir.path(), &error);
        if (theirs.isEmpty()) {
            QSKIP("Xournal++ cannot export the original");
        }
        const QList<QImage> ours = ownPages(doc, dir.path());
        QCOMPARE(ours.size(), theirs.size());
        for (qsizetype i = 0; i < ours.size(); ++i) {
            const double d = difference(ours[i], theirs[i]);
            qInfo("%s page %lld: %.5f", qPrintable(file), static_cast<long long>(i + 1), d);
            if (qEnvironmentVariableIsSet("XOURNALPP_IMAGES")) {
                const QString keep = qEnvironmentVariable("XOURNALPP_IMAGES") + u'/' + file;
                ours[i].save(keep + QStringLiteral("-%1-ours.png").arg(i + 1));
                theirs[i].save(keep + QStringLiteral("-%1-theirs.png").arg(i + 1));
            }
            QVERIFY2(d < 0.01, qPrintable(QStringLiteral("page %1 differs: %2").arg(i + 1).arg(d)));
        }
    }
};

QTEST_MAIN(TestXournalpp)
#include "tst_xournalpp.moc"
