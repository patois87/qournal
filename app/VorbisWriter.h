/*
 * Qournal
 *
 * Writes the sound it is given into an Ogg Vorbis file, the format Xournal++ records in. An audio source of Qt
 * writes into it as into any device.
 *
 * @license GNU GPLv2 or later
 */

#pragma once

#include <QAudioFormat>
#include <QFile>
#include <QIODevice>
#include <memory>

class VorbisWriter: public QIODevice {
public:
    /// @param format of the samples that will be written: 16 bit integers or floats, any number of channels
    VorbisWriter(const QString& path, const QAudioFormat& format, QObject* parent = nullptr);
    ~VorbisWriter() override;

    /// Opens the file and writes the headers. @return false if the file or the encoder cannot be set up
    bool start();
    /// Encodes what is left and closes the file
    void finish();
    QString errorText() const { return m_error; }

protected:
    qint64 readData(char*, qint64) override { return -1; }
    qint64 writeData(const char* data, qint64 length) override;

private:
    struct Encoder;

    /// Writes the pages that are complete, or all of them
    void writePages(bool flush);
    void encode(float** channels, int frames);

    QFile m_file;
    QAudioFormat m_format;
    std::unique_ptr<Encoder> m_encoder;
    QByteArray m_rest;  ///< bytes of a frame that is not complete yet
    QString m_error;
};
