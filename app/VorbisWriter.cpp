#include "VorbisWriter.h"

#include <QRandomGenerator>
#include <cstring>

#include <vorbis/vorbisenc.h>

namespace {

constexpr float QUALITY = 0.4F;  // about 64 kbit/s for one channel at 44.1 kHz: enough for speech

}  // namespace

struct VorbisWriter::Encoder {
    vorbis_info info;
    vorbis_comment comment;
    vorbis_dsp_state dsp;
    vorbis_block block;
    ogg_stream_state stream;
};

VorbisWriter::VorbisWriter(const QString& path, const QAudioFormat& format, QObject* parent):
        QIODevice(parent), m_file(path), m_format(format) {}

VorbisWriter::~VorbisWriter() { finish(); }

bool VorbisWriter::start() {
    const bool usable =
            (m_format.sampleFormat() == QAudioFormat::Int16 || m_format.sampleFormat() == QAudioFormat::Float) &&
            m_format.channelCount() >= 1 && m_format.sampleRate() > 0;
    if (!usable) {
        m_error = tr("The microphone delivers a format that cannot be recorded");
        return false;
    }
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_error = m_file.errorString();
        return false;
    }
    auto encoder = std::make_unique<Encoder>();
    vorbis_info_init(&encoder->info);
    // Speech: one channel, whatever the microphone has
    if (vorbis_encode_init_vbr(&encoder->info, 1, m_format.sampleRate(), QUALITY) != 0) {
        vorbis_info_clear(&encoder->info);
        m_file.close();
        m_file.remove();
        m_error = tr("The encoder does not accept the sample rate %1").arg(m_format.sampleRate());
        return false;
    }
    vorbis_comment_init(&encoder->comment);
    vorbis_comment_add_tag(&encoder->comment, "ENCODER", "Qournal");
    vorbis_analysis_init(&encoder->dsp, &encoder->info);
    vorbis_block_init(&encoder->dsp, &encoder->block);
    ogg_stream_init(&encoder->stream, static_cast<int>(QRandomGenerator::global()->generate() & 0x7fffffff));
    m_encoder = std::move(encoder);

    ogg_packet header;
    ogg_packet headerComment;
    ogg_packet headerCode;
    vorbis_analysis_headerout(&m_encoder->dsp, &m_encoder->comment, &header, &headerComment, &headerCode);
    ogg_stream_packetin(&m_encoder->stream, &header);
    ogg_stream_packetin(&m_encoder->stream, &headerComment);
    ogg_stream_packetin(&m_encoder->stream, &headerCode);
    writePages(true);  // the sound starts on a page of its own
    return open(QIODevice::WriteOnly);
}

void VorbisWriter::writePages(bool flush) {
    ogg_page page;
    while ((flush ? ogg_stream_flush(&m_encoder->stream, &page) : ogg_stream_pageout(&m_encoder->stream, &page)) != 0) {
        m_file.write(reinterpret_cast<const char*>(page.header), page.header_len);
        m_file.write(reinterpret_cast<const char*>(page.body), page.body_len);
    }
}

void VorbisWriter::encode(float** channels, int frames) {
    Q_UNUSED(channels)
    vorbis_analysis_wrote(&m_encoder->dsp, frames);
    while (vorbis_analysis_blockout(&m_encoder->dsp, &m_encoder->block) == 1) {
        vorbis_analysis(&m_encoder->block, nullptr);
        vorbis_bitrate_addblock(&m_encoder->block);
        ogg_packet packet;
        while (vorbis_bitrate_flushpacket(&m_encoder->dsp, &packet) != 0) {
            ogg_stream_packetin(&m_encoder->stream, &packet);
            writePages(false);
        }
    }
}

qint64 VorbisWriter::writeData(const char* data, qint64 length) {
    if (!m_encoder) {
        return -1;
    }
    m_rest.append(data, length);
    const int bytesPerFrame = m_format.bytesPerFrame();
    const int frames = static_cast<int>(m_rest.size() / bytesPerFrame);
    if (frames > 0) {
        float** buffer = vorbis_analysis_buffer(&m_encoder->dsp, frames);
        const int channelCount = m_format.channelCount();
        const char* in = m_rest.constData();
        for (int i = 0; i < frames; ++i) {
            // The channels of the microphone are mixed into one
            float sum = 0;
            for (int ch = 0; ch < channelCount; ++ch) {
                const char* sample = in + i * bytesPerFrame + ch * m_format.bytesPerSample();
                if (m_format.sampleFormat() == QAudioFormat::Int16) {
                    qint16 value;
                    std::memcpy(&value, sample, sizeof(value));
                    sum += static_cast<float>(value) / 32768.0F;
                } else {
                    float value;
                    std::memcpy(&value, sample, sizeof(value));
                    sum += value;
                }
            }
            buffer[0][i] = sum / static_cast<float>(channelCount);
        }
        encode(buffer, frames);
        m_rest.remove(0, static_cast<qsizetype>(frames) * bytesPerFrame);
    }
    return length;
}

void VorbisWriter::finish() {
    if (!m_encoder) {
        return;
    }
    if (isOpen()) {
        close();
    }
    encode(nullptr, 0);  // tells the encoder that this is the end
    writePages(true);
    ogg_stream_clear(&m_encoder->stream);
    vorbis_block_clear(&m_encoder->block);
    vorbis_dsp_clear(&m_encoder->dsp);
    vorbis_comment_clear(&m_encoder->comment);
    vorbis_info_clear(&m_encoder->info);
    m_encoder.reset();
    m_file.close();
}
