/*
 * Qournal
 *
 * Records audio while the user writes and plays it back from the time a stroke or text was made.
 * The recordings are files in a folder of their own; the document only notes their names, as in Xournal++.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>

#include <QtQml/qqmlregistration.h>

class AudioController: public QObject {
    Q_OBJECT
    QML_ELEMENT

    /// Whether this build can record and play audio (it needs Qt Multimedia)
    Q_PROPERTY(bool available READ available CONSTANT)
    /// Where the recordings are stored and looked for
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    /// The name of the file that is being recorded, without the folder; empty if nothing is recorded
    Q_PROPERTY(QString recordingFile READ recordingFile NOTIFY recordingChanged)
    /// A recording is played or paused
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool paused READ paused NOTIFY playingChanged)
    /// Seconds that seekForwards() and seekBackwards() skip
    Q_PROPERTY(int seekTime READ seekTime WRITE setSeekTime NOTIFY seekTimeChanged)
    /// The names of the devices; an empty name of the chosen device stands for the default of the system
    Q_PROPERTY(QStringList inputDevices READ inputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QStringList outputDevices READ outputDevices NOTIFY devicesChanged)
    Q_PROPERTY(QString inputDevice READ inputDevice WRITE setInputDevice NOTIFY deviceChanged)
    Q_PROPERTY(QString outputDevice READ outputDevice WRITE setOutputDevice NOTIFY deviceChanged)
    /**
     * Xournal++ records Ogg Vorbis. Where that cannot be written, the recordings are files Xournal++ can play
     * (WAV), unless this is set: then they are small files (AAC) that only this application plays.
     */
    Q_PROPERTY(bool compact READ compact WRITE setCompact NOTIFY compactChanged)
    /// Whether this build records Ogg Vorbis itself; then "compact" has no effect
    Q_PROPERTY(bool recordsVorbis READ recordsVorbis CONSTANT)
    /// Factor for the level of the recording, 1 for none
    Q_PROPERTY(double gain READ gain WRITE setGain NOTIFY gainChanged)

public:
    explicit AudioController(QObject* parent = nullptr);
    ~AudioController() override;

    bool available() const;
    QString folder() const { return m_folder; }
    /// An empty folder stands for the default one, in the data of the application
    void setFolder(const QString& folder);
    static QString defaultFolder();
    bool recording() const { return !m_recordingFile.isEmpty(); }
    QString recordingFile() const { return m_recordingFile; }
    bool playing() const;
    bool paused() const;
    int seekTime() const { return m_seekTime; }
    void setSeekTime(int seconds);
    QStringList inputDevices() const;
    QStringList outputDevices() const;
    QString inputDevice() const { return m_inputDevice; }
    void setInputDevice(const QString& name);
    QString outputDevice() const { return m_outputDevice; }
    void setOutputDevice(const QString& name);
    bool compact() const { return m_compact; }
    bool recordsVorbis() const;
    void setCompact(bool compact);
    double gain() const { return m_gain; }
    void setGain(double gain);

    /// Starts a recording into a new file of the folder, named by the date and time. failed() tells why not
    Q_INVOKABLE bool startRecording();
    Q_INVOKABLE void stopRecording();

    /**
     * The file a document means by the name it noted: the name itself if it is a path, else the file in the folder
     * of the recordings or next to the document.
     * @return nothing if there is no such file
     */
    Q_INVOKABLE QString locate(const QString& filename, const QString& documentPath = {}) const;
    /// Plays a recording from a time in milliseconds on, see locate()
    Q_INVOKABLE bool play(const QString& filename, qint64 timestamp, const QString& documentPath = {});
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seekForwards();
    Q_INVOKABLE void seekBackwards();
    /// Position of the playback in milliseconds
    Q_INVOKABLE qint64 position() const;

    /// The extension of the files that are recorded (not compact), e.g. "ogg"; empty if this build cannot record
    static QString recordingSuffix();

signals:
    void folderChanged();
    void recordingChanged();
    void playingChanged();
    void seekTimeChanged();
    void devicesChanged();
    void deviceChanged();
    void gainChanged();
    void compactChanged();
    /// Recording or playback did not work
    void failed(const QString& message);

private:
    struct Backend;

    void seek(qint64 milliseconds);
    /// Starts the recorder with the first format that works. @return false if there is none
    bool record();

    std::unique_ptr<Backend> m_backend;  ///< the objects of Qt Multimedia, if it is there
    QString m_folder;
    QString m_recordingFile;
    int m_seekTime = 5;
    QString m_inputDevice;
    QString m_outputDevice;
    double m_gain = 1.0;
    bool m_compact = false;
    int m_formatIndex = -1;       ///< the format of the recording in progress
    qint64 m_startPosition = -1;  ///< where the playback starts once the file is loaded
};
