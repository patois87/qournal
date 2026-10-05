#include "AudioController.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>
#include <iterator>

#ifdef HAVE_QTMULTIMEDIA
#include <QAudioDevice>
#include <QCoreApplication>
#if QT_CONFIG(permissions)
#include <QPermissions>
#endif
#include <QAudioInput>
#include <QAudioOutput>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaPlayer>
#include <QMediaRecorder>
#ifdef HAVE_VORBIS
#include <QAudioSource>

#include "VorbisWriter.h"
#endif

struct AudioController::Backend {
    QMediaDevices devices;
    QMediaCaptureSession session;
    QAudioInput input;
    std::unique_ptr<QMediaRecorder> recorder;  ///< a new one for every recording
#ifdef HAVE_VORBIS
    // Recording as Ogg Vorbis: the samples of the microphone go to our own encoder
    std::unique_ptr<QAudioSource> source;
    std::unique_ptr<VorbisWriter> vorbis;
#endif
    QMediaPlayer player;
    QAudioOutput output;
};

namespace {

struct Format {
    QMediaFormat::FileFormat format;
    QMediaFormat::AudioCodec codec;
    const char* suffix;
};

// Ogg Vorbis is what Xournal++ records. Where it cannot be written, the formats Xournal++ can play (those of
// libsndfile) come before the others
const Format FORMATS[] = {
        {QMediaFormat::Ogg, QMediaFormat::AudioCodec::Vorbis, "ogg"},
        {QMediaFormat::Ogg, QMediaFormat::AudioCodec::Opus, "opus"},
        {QMediaFormat::MP3, QMediaFormat::AudioCodec::MP3, "mp3"},
        {QMediaFormat::FLAC, QMediaFormat::AudioCodec::FLAC, "flac"},
        {QMediaFormat::Wave, QMediaFormat::AudioCodec::Wave, "wav"},
        {QMediaFormat::Mpeg4Audio, QMediaFormat::AudioCodec::AAC, "m4a"},
};

constexpr size_t AAC_FORMAT = std::size(FORMATS) - 1;

/// Formats that turned out not to work although Qt lists them
bool unusable[std::size(FORMATS)] = {};

bool usable(size_t index) {
    QMediaFormat format(FORMATS[index].format);
    format.setAudioCodec(FORMATS[index].codec);
    return !unusable[index] && format.isSupported(QMediaFormat::Encode);
}

/**
 * The first format that can be written.
 * @param compact small files count more than files Xournal++ can play: AAC comes before the formats without or
 *                with little compression
 * @return nullptr if there is none
 */
const Format* recordingFormat(bool compact) {
    for (size_t index = 0; index < std::size(FORMATS); ++index) {
        const bool large = FORMATS[index].format == QMediaFormat::FLAC || FORMATS[index].format == QMediaFormat::Wave;
        if (compact && large && usable(AAC_FORMAT)) {
            return &FORMATS[AAC_FORMAT];
        }
        if (usable(index)) {
            return &FORMATS[index];
        }
    }
    return nullptr;
}

QAudioDevice deviceNamed(const QList<QAudioDevice>& devices, const QString& name, const QAudioDevice& fallback) {
    for (const QAudioDevice& device: devices) {
        if (device.description() == name) {
            return device;
        }
    }
    return fallback;
}

}  // namespace
#else
struct AudioController::Backend {};
#endif

AudioController::AudioController(QObject* parent): QObject(parent), m_folder(defaultFolder()) {
#ifdef HAVE_QTMULTIMEDIA
    m_backend = std::make_unique<Backend>();
    Backend& b = *m_backend;
    b.session.setAudioInput(&b.input);
    b.player.setAudioOutput(&b.output);

    connect(&b.devices, &QMediaDevices::audioInputsChanged, this, &AudioController::devicesChanged);
    connect(&b.devices, &QMediaDevices::audioOutputsChanged, this, &AudioController::devicesChanged);
    connect(&b.player, &QMediaPlayer::playbackStateChanged, this, &AudioController::playingChanged);
    connect(&b.player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString& text) {
        m_startPosition = -1;
        emit failed(tr("Unable to play audio recording %1").arg(m_backend->player.source().toLocalFile()) + u'\n' +
                    text);
    });
    connect(&b.player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        // The position can only be set once the file is loaded
        if (m_startPosition >= 0 && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            m_backend->player.setPosition(m_startPosition);
            m_startPosition = -1;
            m_backend->player.play();
        }
    });
#endif
}

AudioController::~AudioController() { stopRecording(); }

bool AudioController::available() const { return m_backend != nullptr; }

QString AudioController::defaultFolder() {
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/audio");
}

void AudioController::setFolder(const QString& newFolder) {
    QString folder = newFolder.trimmed();
    if (folder.startsWith(u"file:")) {
        folder = QUrl(folder).toLocalFile();
    }
    if (folder.isEmpty()) {
        folder = defaultFolder();
    }
    if (m_folder != folder) {
        m_folder = folder;
        emit folderChanged();
    }
}

void AudioController::setSeekTime(int seconds) {
    seconds = std::max(seconds, 1);
    if (m_seekTime != seconds) {
        m_seekTime = seconds;
        emit seekTimeChanged();
    }
}

QStringList AudioController::inputDevices() const {
    QStringList names;
#ifdef HAVE_QTMULTIMEDIA
    for (const QAudioDevice& device: QMediaDevices::audioInputs()) {
        names.append(device.description());
    }
#endif
    return names;
}

QStringList AudioController::outputDevices() const {
    QStringList names;
#ifdef HAVE_QTMULTIMEDIA
    for (const QAudioDevice& device: QMediaDevices::audioOutputs()) {
        names.append(device.description());
    }
#endif
    return names;
}

void AudioController::setInputDevice(const QString& name) {
    if (m_inputDevice != name) {
        m_inputDevice = name;
#ifdef HAVE_QTMULTIMEDIA
        m_backend->input.setDevice(deviceNamed(QMediaDevices::audioInputs(), name, QMediaDevices::defaultAudioInput()));
#endif
        emit deviceChanged();
    }
}

void AudioController::setOutputDevice(const QString& name) {
    if (m_outputDevice != name) {
        m_outputDevice = name;
#ifdef HAVE_QTMULTIMEDIA
        m_backend->output.setDevice(
                deviceNamed(QMediaDevices::audioOutputs(), name, QMediaDevices::defaultAudioOutput()));
#endif
        emit deviceChanged();
    }
}

bool AudioController::recordsVorbis() const {
#ifdef HAVE_VORBIS
    return true;
#else
    return false;
#endif
}

void AudioController::setCompact(bool compact) {
    if (m_compact != compact) {
        m_compact = compact;
        emit compactChanged();
    }
}

void AudioController::setGain(double gain) {
    gain = std::clamp(gain, 0.0, 1.0);
    if (m_gain != gain) {
        m_gain = gain;
#ifdef HAVE_QTMULTIMEDIA
        m_backend->input.setVolume(static_cast<float>(gain));
#endif
        emit gainChanged();
    }
}

QString AudioController::recordingSuffix() {
    // What a recording would be written as now, with the formats that failed before left out
#ifdef HAVE_VORBIS
    return QStringLiteral("ogg");
#endif
#ifdef HAVE_QTMULTIMEDIA
    if (const Format* format = recordingFormat(false)) {
        return QString::fromLatin1(format->suffix);
    }
#endif
    return {};
}

bool AudioController::startRecording() {
    if (recording()) {
        return false;
    }
#ifdef HAVE_QTMULTIMEDIA
#if QT_CONFIG(permissions)
    // Android, iOS and macOS ask the user the first time; the recording starts when the answer is yes
    const QMicrophonePermission microphone;
    switch (qApp->checkPermission(microphone)) {
        case Qt::PermissionStatus::Undetermined:
            qApp->requestPermission(microphone, this, [this](const QPermission& permission) {
                if (permission.status() == Qt::PermissionStatus::Granted) {
                    startRecording();
                } else {
                    emit failed(tr("The application is not allowed to use the microphone"));
                }
            });
            return false;
        case Qt::PermissionStatus::Denied:
            emit failed(tr("The application is not allowed to use the microphone"));
            return false;
        case Qt::PermissionStatus::Granted:
            break;
    }
#endif
    if (QMediaDevices::audioInputs().isEmpty()) {
        emit failed(tr("No microphone was found"));
        return false;
    }
    if (!QDir().mkpath(m_folder)) {
        emit failed(tr("Audio folder not set or invalid! Recording won't work!\nPlease set the recording folder "
                       "under \"Preferences > Audio recording\""));
        return false;
    }
    if (!record()) {
        emit failed(tr("This system has no encoder for audio recordings"));
        return false;
    }
    return true;
#else
    emit failed(tr("This build cannot record audio: it was built without Qt Multimedia"));
    return false;
#endif
}

bool AudioController::record() {
#ifdef HAVE_VORBIS
    {
        // Ogg Vorbis, as Xournal++ records: 16 bit samples of the microphone, encoded here
        Backend& b = *m_backend;
        const QAudioDevice device =
                deviceNamed(QMediaDevices::audioInputs(), m_inputDevice, QMediaDevices::defaultAudioInput());
        QAudioFormat format;
        format.setSampleRate(44100);
        format.setChannelCount(1);
        format.setSampleFormat(QAudioFormat::Int16);
        if (!device.isFormatSupported(format)) {
            format = device.preferredFormat();
        }
        const QString name =
                QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) + QStringLiteral(".ogg");
        auto writer = std::make_unique<VorbisWriter>(m_folder + u'/' + name, format);
        if (writer->start()) {
            b.vorbis = std::move(writer);
            b.source = std::make_unique<QAudioSource>(device, format);
            b.source->setVolume(static_cast<float>(m_gain));
            b.source->start(b.vorbis.get());
            if (b.source->error() == QAudio::NoError) {
                m_formatIndex = -1;
                m_recordingFile = name;
                emit recordingChanged();
                return true;
            }
            // The microphone cannot be opened this way: the recorder of Qt is tried
            b.source.reset();
            b.vorbis->finish();
            b.vorbis.reset();
            QFile::remove(m_folder + u'/' + name);
        }
    }
#endif
#ifdef HAVE_QTMULTIMEDIA
    const Format* format = recordingFormat(m_compact);
    if (!format) {
        m_formatIndex = -1;
        return false;
    }
    m_formatIndex = static_cast<int>(format - FORMATS);
    // The date and time name the file, as in Xournal++
    const QString name = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss")) + u'.' +
                         QString::fromLatin1(format->suffix);
    Backend& b = *m_backend;
    QMediaFormat mediaFormat(format->format);
    mediaFormat.setAudioCodec(format->codec);
    // A recorder that failed does not record again
    b.recorder = std::make_unique<QMediaRecorder>();
    b.session.setRecorder(b.recorder.get());
    connect(b.recorder.get(), &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString& text) {
        const QString file = m_recordingFile;
        if (file.isEmpty()) {
            return;
        }
        // A format that cannot be written although Qt lists it fails at once: the next one is tried, once the
        // recorder has finished with this one
        if (m_backend->recorder->duration() == 0 && m_formatIndex >= 0) {
            unusable[m_formatIndex] = true;
            QFile::remove(m_folder + u'/' + file);
            QMetaObject::invokeMethod(
                    this,
                    [this, file, text] {
                        if (m_recordingFile == file && !record()) {
                            m_recordingFile.clear();
                            emit recordingChanged();
                            emit failed(tr("The recording %1 failed: %2").arg(file, text));
                        }
                    },
                    Qt::QueuedConnection);
            return;
        }
        m_recordingFile.clear();
        emit recordingChanged();
        emit failed(tr("The recording %1 failed: %2").arg(file, text));
    });
    b.recorder->setMediaFormat(mediaFormat);
    // Speech: one channel, and without compression a sample rate that keeps the files small
    b.recorder->setAudioChannelCount(1);
    b.recorder->setAudioSampleRate(format->format == QMediaFormat::Wave ? 22050 : -1);
    b.recorder->setOutputLocation(QUrl::fromLocalFile(m_folder + u'/' + name));
    m_recordingFile = name;
    emit recordingChanged();
    // An error is reported by errorOccurred(), now or a moment later
    b.recorder->record();
    return true;
#else
    return false;
#endif
}

void AudioController::stopRecording() {
    if (!recording()) {
        return;
    }
#ifdef HAVE_VORBIS
    if (m_backend->source) {
        m_backend->source->stop();
        m_backend->source.reset();
        m_backend->vorbis->finish();
        m_backend->vorbis.reset();
    } else
#endif
#ifdef HAVE_QTMULTIMEDIA
            if (m_backend->recorder) {
        m_backend->recorder->stop();
    }
#endif
    m_recordingFile.clear();
    emit recordingChanged();
}

QString AudioController::locate(const QString& filename, const QString& documentPath) const {
    if (filename.isEmpty()) {
        return {};
    }
    QStringList candidates;
    if (QFileInfo(filename).isAbsolute()) {
        candidates.append(filename);
    }
    const QString name = QFileInfo(filename).fileName();
    candidates.append(m_folder + u'/' + name);
    if (!documentPath.isEmpty()) {
        candidates.append(QFileInfo(documentPath).path() + u'/' + name);
    }
    for (const QString& candidate: std::as_const(candidates)) {
        if (QFileInfo(candidate).isFile()) {
            return candidate;
        }
    }
    return {};
}

bool AudioController::play(const QString& filename, qint64 timestamp, const QString& documentPath) {
    const QString path = locate(filename, documentPath);
    if (path.isEmpty()) {
        emit failed(
                tr("Unable to play audio recording %1").arg(filename) + u'\n' +
                tr("The file is neither in the folder of the recordings (%1) nor next to the document.").arg(m_folder));
        return false;
    }
#ifdef HAVE_QTMULTIMEDIA
    Backend& b = *m_backend;
    b.player.stop();
    m_startPosition = std::max<qint64>(timestamp, 0);
    const QUrl url = QUrl::fromLocalFile(path);
    if (b.player.source() == url && b.player.mediaStatus() != QMediaPlayer::InvalidMedia &&
        b.player.mediaStatus() != QMediaPlayer::NoMedia && b.player.mediaStatus() != QMediaPlayer::LoadingMedia) {
        // Already loaded
        b.player.setPosition(m_startPosition);
        m_startPosition = -1;
        b.player.play();
    } else {
        b.player.setSource(url);
    }
    return true;
#else
    Q_UNUSED(timestamp)
    emit failed(tr("This build cannot play audio: it was built without Qt Multimedia"));
    return false;
#endif
}

bool AudioController::playing() const {
#ifdef HAVE_QTMULTIMEDIA
    return m_backend->player.playbackState() != QMediaPlayer::StoppedState;
#else
    return false;
#endif
}

bool AudioController::paused() const {
#ifdef HAVE_QTMULTIMEDIA
    return m_backend->player.playbackState() == QMediaPlayer::PausedState;
#else
    return false;
#endif
}

void AudioController::pause() {
#ifdef HAVE_QTMULTIMEDIA
    m_backend->player.pause();
#endif
}

void AudioController::resume() {
#ifdef HAVE_QTMULTIMEDIA
    if (paused()) {
        m_backend->player.play();
    }
#endif
}

void AudioController::stop() {
#ifdef HAVE_QTMULTIMEDIA
    m_startPosition = -1;
    m_backend->player.stop();
#endif
}

qint64 AudioController::position() const {
#ifdef HAVE_QTMULTIMEDIA
    return m_backend->player.position();
#else
    return 0;
#endif
}

void AudioController::seek(qint64 milliseconds) {
#ifdef HAVE_QTMULTIMEDIA
    QMediaPlayer& player = m_backend->player;
    if (playing()) {
        player.setPosition(
                std::clamp<qint64>(player.position() + milliseconds, 0, std::max<qint64>(player.duration(), 0)));
    }
#else
    Q_UNUSED(milliseconds)
#endif
}

void AudioController::seekForwards() { seek(m_seekTime * 1000LL); }

void AudioController::seekBackwards() { seek(-m_seekTime * 1000LL); }
