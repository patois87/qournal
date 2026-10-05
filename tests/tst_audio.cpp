/*
 * Qournal
 *
 * Tests for the audio recordings: what strokes and texts remember while a recording runs, the tool that plays
 * them, and the controller that records and plays. They run without a display (QT_QPA_PLATFORM=offscreen,
 * QT_QUICK_BACKEND=software); where the machine has no audio devices, recording and playback are skipped.
 *
 * @license GNU GPLv2 or later
 */

#include <QElapsedTimer>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtEndian>

#include "AudioController.h"
#include "CanvasFixture.h"
#include "XoppLoader.h"

namespace {

/// A WAV file with a quiet tone: 16 bit, mono, 8000 Hz
void writeWav(const QString& path, int seconds) {
    const int rate = 8000;
    QByteArray samples;
    for (int i = 0; i < rate * seconds; ++i) {
        const qint16 value =
                qToLittleEndian<qint16>(static_cast<qint16>(300 * std::sin(i * 2 * 3.14159265358979 * 440 / rate)));
        samples.append(reinterpret_cast<const char*>(&value), 2);
    }
    auto number = [](quint32 value, int bytes) {
        const quint32 little = qToLittleEndian(value);
        return QByteArray(reinterpret_cast<const char*>(&little), bytes);
    };
    QByteArray data("RIFF");
    data += number(static_cast<quint32>(36 + samples.size()), 4) + "WAVEfmt " + number(16, 4) + number(1, 2) +
            number(1, 2) + number(rate, 4) + number(rate * 2, 4) + number(2, 2) + number(16, 2) + "data" +
            number(static_cast<quint32>(samples.size()), 4) + samples;
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

}  // namespace

class TestAudio: public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { QStandardPaths::setTestModeEnabled(true); }

    void strokesAndTextsRememberTheRecording() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});
        QVERIFY(f.strokes()[0].audio.filename.isEmpty());

        QSignalSpy changed(c, &PageCanvas::audioRecordingChanged);
        // The times are compared with a clock of the test: the machine may be slow
        QElapsedTimer clock;
        clock.start();
        c->setAudioRecording(QStringLiteral("2026-10-02_10-00-00.ogg"));
        QCOMPARE(changed.count(), 1);
        f.strokeOnPage(0, {QPointF(100, 150), QPointF(300, 150)});
        const qint64 afterFirst = clock.elapsed();
        QTest::qWait(120);
        // The time a stroke is begun at counts, not the time it is finished at
        const qint64 beforePress = clock.elapsed();
        f.tablet(QEvent::TabletPress, f.onPage(0, QPointF(100, 200)), 0.5);
        const qint64 afterPress = clock.elapsed();
        QTest::qWait(80);
        f.tablet(QEvent::TabletMove, f.onPage(0, QPointF(300, 200)), 0.5);
        f.tablet(QEvent::TabletRelease, f.onPage(0, QPointF(300, 200)), 0);
        const qint64 afterRelease = clock.elapsed();

        QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 3);
        QCOMPARE(strokes[1].audio.filename, QStringLiteral("2026-10-02_10-00-00.ogg"));
        QVERIFY(strokes[1].audio.timestamp >= 0 && strokes[1].audio.timestamp <= afterFirst);
        QCOMPARE(strokes[2].audio.filename, strokes[1].audio.filename);
        const qint64 timestamp = strokes[2].audio.timestamp;
        QVERIFY2(timestamp >= beforePress - 2 && timestamp <= afterPress + 2 && afterRelease - timestamp >= 75,
                 qPrintable(QStringLiteral("%1, pressed between %2 and %3, released at %4")
                                    .arg(timestamp)
                                    .arg(beforePress)
                                    .arg(afterPress)
                                    .arg(afterRelease)));

        // Shapes of the pen too, but not the highlighter: as in Xournal++
        c->setDrawingType(PageCanvas::Rectangle);
        f.strokeOnPage(0, {QPointF(100, 250), QPointF(300, 300)});
        c->setDrawingType(PageCanvas::Freehand);
        c->setTool(PageCanvas::Highlighter);
        f.strokeOnPage(0, {QPointF(100, 350), QPointF(300, 350)});
        strokes = f.strokes();
        QCOMPARE(strokes.size(), 5);
        QVERIFY(!strokes[3].audio.filename.isEmpty());
        QVERIFY(strokes[4].audio.filename.isEmpty());

        // A text that is written meanwhile
        c->setTool(PageCanvas::Text);
        f.tap(f.onPage(0, QPointF(350, 100)));
        QVERIFY(c->textEditing());
        c->setTextEditText(QStringLiteral("spoken"));
        c->finishTextEdit();
        const auto& elements = c->document().pages[0].layers[0].elements;
        const auto* text = std::get_if<TextElement>(&elements.back());
        QVERIFY(text);
        QCOMPARE(text->audio.filename, QStringLiteral("2026-10-02_10-00-00.ogg"));
        QVERIFY(text->audio.timestamp >= afterRelease);

        // The file keeps it
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("audio.xopp"));
        QVERIFY(c->saveAs(QUrl::fromLocalFile(path)));
        Document loaded;
        QVERIFY(loadXopp(path, loaded, nullptr));
        const auto& loadedElements = loaded.pages[0].layers[0].elements;
        QCOMPARE(loadedElements.size(), elements.size());
        QCOMPARE(std::get<Stroke>(loadedElements[2]).audio.timestamp, strokes[2].audio.timestamp);
        QCOMPARE(std::get<Stroke>(loadedElements[2]).audio.filename, strokes[2].audio.filename);
        QCOMPARE(std::get<TextElement>(loadedElements.back()).audio.timestamp, text->audio.timestamp);

        // After the recording, strokes are plain again; a new recording starts its time anew
        c->setAudioRecording(QString());
        c->setTool(PageCanvas::Pen);
        f.strokeOnPage(0, {QPointF(100, 400), QPointF(300, 400)});
        QVERIFY(f.strokes().last().audio.filename.isEmpty());
        c->setAudioRecording(QStringLiteral("second.ogg"));
        f.strokeOnPage(0, {QPointF(100, 420), QPointF(300, 420)});
        QCOMPARE(f.strokes().last().audio.filename, QStringLiteral("second.ogg"));
        QVERIFY(f.strokes().last().audio.timestamp < text->audio.timestamp);  // counted from its own start
    }

    /// The list of the recordings of the document: per recording the pages, at the first moment they refer to it
    void recordingsList() {
        Fixture f;
        PageCanvas* c = f.canvas;
        QVERIFY(c->recordings().isEmpty());
        c->insertPage(1);
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});  // without recording
        c->setAudioRecording(QStringLiteral("first.ogg"));
        QTest::qWait(20);
        c->setCurrentPage(1);
        f.strokeOnPage(1, {QPointF(100, 100), QPointF(300, 100)});
        QTest::qWait(20);
        c->setCurrentPage(0);
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        f.strokeOnPage(0, {QPointF(100, 300), QPointF(300, 300)});
        c->setAudioRecording(QStringLiteral("second.ogg"));
        f.strokeOnPage(0, {QPointF(100, 400), QPointF(300, 400)});
        c->setAudioRecording(QString());

        const QVariantList list = c->recordings();
        QCOMPARE(list.size(), 2);
        const QVariantMap first = list[0].toMap();
        QCOMPARE(first.value(QStringLiteral("file")).toString(), QStringLiteral("first.ogg"));
        QCOMPARE(first.value(QStringLiteral("elements")).toInt(), 3);
        const QVariantList marks = first.value(QStringLiteral("marks")).toList();
        QCOMPARE(marks.size(), 2);
        // In the order of the recording: page 2 came first
        QCOMPARE(marks[0].toMap().value(QStringLiteral("page")).toInt(), 1);
        QCOMPARE(marks[1].toMap().value(QStringLiteral("page")).toInt(), 0);
        QVERIFY(marks[0].toMap().value(QStringLiteral("timestamp")).toLongLong() <
                marks[1].toMap().value(QStringLiteral("timestamp")).toLongLong());
        QCOMPARE(list[1].toMap().value(QStringLiteral("elements")).toInt(), 1);
    }

    void playTool() {
        Fixture f;
        PageCanvas* c = f.canvas;
        f.strokeOnPage(0, {QPointF(100, 100), QPointF(300, 100)});  // without recording
        c->setAudioRecording(QStringLiteral("first.ogg"));
        QTest::qWait(30);
        f.strokeOnPage(0, {QPointF(100, 200), QPointF(300, 200)});
        c->setAudioRecording(QStringLiteral("second.ogg"));
        f.strokeOnPage(0, {QPointF(200, 150), QPointF(200, 250)});  // crosses the one before, on top of it
        c->setAudioRecording(QString());
        const QList<Stroke> strokes = f.strokes();
        QCOMPARE(strokes.size(), 3);

        QSignalSpy play(c, &PageCanvas::audioPlayRequested);
        c->setTool(PageCanvas::PlayObject);
        f.tap(f.onPage(0, QPointF(150, 200)));
        QCOMPARE(play.count(), 1);
        QCOMPARE(play[0][0].toString(), QStringLiteral("first.ogg"));
        QCOMPARE(play[0][1].toLongLong(), strokes[1].audio.timestamp);
        QVERIFY(play[0][1].toLongLong() >= 30);

        // Where two cross, the one in front
        f.tap(f.onPage(0, QPointF(200, 200)));
        QCOMPARE(play.count(), 2);
        QCOMPARE(play[1][0].toString(), QStringLiteral("second.ogg"));

        // Nothing for a stroke without recording, for the empty page, or for a hidden layer
        f.tap(f.onPage(0, QPointF(150, 100)));
        f.tap(f.onPage(0, QPointF(350, 350)));
        QCOMPARE(play.count(), 2);
        c->setLayerVisible(0, false);
        f.tap(f.onPage(0, QPointF(150, 200)));
        QCOMPARE(play.count(), 2);
        // The tool does not change the document
        QVERIFY(c->canUndo());
        QCOMPARE(f.strokes().size(), 3);
    }

    void folderAndFiles() {
        QTemporaryDir recordings;
        QTemporaryDir documents;
        AudioController audio;
        QCOMPARE(audio.folder(), AudioController::defaultFolder());
        QSignalSpy folderChanged(&audio, &AudioController::folderChanged);
        audio.setFolder(QUrl::fromLocalFile(recordings.path()).toString());  // as a file dialog gives it
        QCOMPARE(audio.folder(), recordings.path());
        QCOMPARE(folderChanged.count(), 1);

        const QString document = documents.filePath(QStringLiteral("notes.xopp"));
        const QString inFolder = recordings.filePath(QStringLiteral("a.ogg"));
        const QString nextToDocument = documents.filePath(QStringLiteral("b.ogg"));
        for (const QString& path: {inFolder, nextToDocument}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        QCOMPARE(audio.locate(QStringLiteral("a.ogg")), inFolder);
        QCOMPARE(audio.locate(QStringLiteral("b.ogg")), QString());
        QCOMPARE(audio.locate(QStringLiteral("b.ogg"), document), nextToDocument);
        QCOMPARE(audio.locate(nextToDocument), nextToDocument);
        // A path of another machine: the name is looked for here
        QCOMPARE(audio.locate(QStringLiteral("/somewhere/else/a.ogg")), inFolder);
        QCOMPARE(audio.locate(QStringLiteral("missing.ogg"), document), QString());
        QCOMPARE(audio.locate(QString()), QString());

        QSignalSpy failed(&audio, &AudioController::failed);
        QVERIFY(!audio.play(QStringLiteral("missing.ogg"), 0, document));
        QCOMPARE(failed.count(), 1);
        QVERIFY(failed[0][0].toString().contains(QStringLiteral("missing.ogg")));
        QVERIFY(!audio.playing());

        audio.setFolder(QString());
        QCOMPARE(audio.folder(), AudioController::defaultFolder());
        audio.setSeekTime(-3);
        QCOMPARE(audio.seekTime(), 1);
        audio.setSeekTime(10);
        QCOMPARE(audio.seekTime(), 10);
    }

    void playback() {
        AudioController audio;
#ifndef HAVE_QTMULTIMEDIA
        QVERIFY(!audio.available());
        QTemporaryDir dir;
        audio.setFolder(dir.path());
        writeWav(dir.filePath(QStringLiteral("tone.wav")), 1);
        QSignalSpy failed(&audio, &AudioController::failed);
        QVERIFY(!audio.play(QStringLiteral("tone.wav"), 0));
        QCOMPARE(failed.count(), 1);
        QSKIP("Built without Qt Multimedia");
#else
        QVERIFY(audio.available());
        QTemporaryDir dir;
        audio.setFolder(dir.path());
        writeWav(dir.filePath(QStringLiteral("tone.wav")), 6);
        QSignalSpy failed(&audio, &AudioController::failed);
        QSignalSpy playingChanged(&audio, &AudioController::playingChanged);

        QVERIFY(audio.play(QStringLiteral("tone.wav"), 2000));
        if (!QTest::qWaitFor([&] { return audio.playing() || failed.count() > 0; }, 5000) || failed.count() > 0) {
            QSKIP("This machine cannot play audio");
        }
        QVERIFY(playingChanged.count() > 0);
        // It starts at the time of the stroke, not at the beginning
        QTRY_VERIFY2_WITH_TIMEOUT(audio.position() >= 2000, qPrintable(QString::number(audio.position())), 3000);
        QVERIFY(audio.position() < 4000);

        audio.pause();
        QVERIFY(audio.paused());
        QVERIFY(audio.playing());
        const qint64 paused = audio.position();
        QTest::qWait(200);
        QVERIFY(std::abs(audio.position() - paused) < 100);

        audio.setSeekTime(1);
        audio.seekForwards();
        QTRY_VERIFY_WITH_TIMEOUT(audio.position() >= paused + 900, 2000);
        audio.seekBackwards();
        audio.seekBackwards();
        QTRY_VERIFY_WITH_TIMEOUT(audio.position() <= paused - 900, 2000);
        audio.resume();
        QVERIFY(!audio.paused());

        // Another stroke of the same recording
        QVERIFY(audio.play(QStringLiteral("tone.wav"), 500));
        QTRY_VERIFY_WITH_TIMEOUT(audio.playing() && audio.position() >= 500 && audio.position() < 2000, 3000);

        audio.stop();
        QVERIFY(!audio.playing());
        QCOMPARE(failed.count(), 0);
#endif
    }

    void recording() {
        AudioController audio;
        QTemporaryDir dir;
        audio.setFolder(dir.filePath(QStringLiteral("recordings")));  // is created
        QSignalSpy failed(&audio, &AudioController::failed);
        QSignalSpy changed(&audio, &AudioController::recordingChanged);
        QVERIFY(!audio.recording());
        audio.stopRecording();  // nothing to stop
        QCOMPARE(changed.count(), 0);

        if (!audio.startRecording()) {
            // Without Qt Multimedia, a microphone or an encoder: the reason is told, and nothing is recorded
            QCOMPARE(failed.count(), 1);
            QVERIFY(!audio.recording());
            QVERIFY(audio.recordingFile().isEmpty());
            QSKIP(qPrintable(QStringLiteral("This machine cannot record audio: ") + failed[0][0].toString()));
        }
        QVERIFY(audio.recording());
        // A format that does not work here is replaced by the next one within a moment
        QTest::qWait(500);
        if (!audio.recording()) {
            QCOMPARE(failed.count(), 1);
            QSKIP(qPrintable(QStringLiteral("Recording failed here: ") + failed[0][0].toString()));
        }
        changed.clear();
        const QString name = audio.recordingFile();
        const QRegularExpression pattern(QStringLiteral(R"(^\d{4}-\d\d-\d\d_\d\d-\d\d-\d\d\.)") +
                                         AudioController::recordingSuffix() + u'$');
        QVERIFY(QStringList({QStringLiteral("ogg"), QStringLiteral("opus"), QStringLiteral("mp3"),
                             QStringLiteral("flac"), QStringLiteral("wav"), QStringLiteral("m4a")})
                        .contains(AudioController::recordingSuffix()));
        QVERIFY2(pattern.match(name).hasMatch(), qPrintable(name));
        QVERIFY(!audio.startRecording());  // one at a time

        QTest::qWait(700);
        audio.stopRecording();
        QVERIFY(!audio.recording());
        QCOMPARE(changed.count(), 1);
        if (failed.count() > 0) {
            QSKIP(qPrintable(QStringLiteral("Recording failed here: ") + failed[0][0].toString()));
        }
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo(audio.folder() + u'/' + name).size() > 0, 3000);
#ifdef HAVE_VORBIS
        // Ogg Vorbis, the format of Xournal++
        QVERIFY(name.endsWith(QStringLiteral(".ogg")));
        QFile recorded(audio.folder() + u'/' + name);
        QVERIFY(recorded.open(QIODevice::ReadOnly));
        const QByteArray start = recorded.read(64);
        QVERIFY(start.startsWith("OggS"));
        QVERIFY(start.contains("vorbis"));
        QVERIFY(recorded.size() > 3000);  // the headers alone are about that; more means sound was encoded
#endif
        qInfo("recorded %s", qPrintable(name));
        QCOMPARE(audio.locate(name), audio.folder() + u'/' + name);
        // Only this recording is in the folder: attempts with other formats left nothing behind
        QCOMPARE(QDir(audio.folder()).entryList(QDir::Files), QStringList{name});

        // What was recorded can be played
        QVERIFY(audio.play(name, 0));
        QTRY_VERIFY_WITH_TIMEOUT(audio.playing() || failed.count() > 0, 5000);
        QCOMPARE(failed.count(), 0);
        audio.stop();
    }
};

QTEST_MAIN(TestAudio)
#include "tst_audio.moc"
